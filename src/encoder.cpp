/****************************************************************************
 * RATGDO Encoder Support
 *
 * Copyright (c) 2023-26 homekit-ratgdo contributors
 * Licensed under terms of the GPL-3.0 License.
 *
 * Ported from esphome-ratgdo components/ratgdo/ratgdo.cpp
 * (RATGDOStore::isr_encoder / RATGDOComponent::on_encoder_update /
 *  RATGDOComponent::check_encoder_stopped / reset_encoder_calibration)
 *
 * Encoder A = DRY_CONTACT_OPEN_PIN
 * Encoder B = DRY_CONTACT_CLOSE_PIN
 */
#ifdef RATGDO_ENCODER

// Arduino includes
#include <Ticker.h>

// RATGDO project includes
#include "ratgdo.h"
#include "config.h"
#include "comms.h"
#include "homekit.h"
#include "encoder.h"

static const char *TAG = "ratgdo-encoder";

static bool encoder_setup_done = false;
bool encoder_enabled = false;

// ─── ISR storage (IRAM) ──────────────────────────────────────────────────────
// Keep these as simple integers — no C++ objects in IRAM section on ESP32.

static volatile int16_t enc_delta = 0;      // accumulated net step delta (drained in loop)
static volatile uint8_t enc_prev_state = 0; // previous quadrature state (A<<1|B)
static volatile int8_t enc_cycle_count = 0; // net sub-step accumulator (emits at ±4)

// ─── Calibration & state ─────────────────────────────────────────────────────

static int16_t enc_last_ = 0; // most recent raw step count
static int16_t enc_min_ = 0;  // step at CLOSED boundary
static int16_t enc_max_ = 0;  // step at OPEN boundary
static bool enc_min_cal_ = false;
static bool enc_max_cal_ = false;

enum direction_t
{
  DIR_CLOSING = -1,
  DIR_NONE = 0,
  DIR_OPENING = 1
};

#define DIRECTION_STR(s) (s == direction_t::DIR_CLOSING) ? "Closing" : (s == direction_t::DIR_OPENING) ? "Opening" \
                                                                                                       : "None"

// Direction tracking (for stopped-watchdog and reverse detection)
static bool reverse_encoder = false;           // userConfig->getEncoderReversed()
static direction_t enc_travel_dir_ = DIR_NONE; // dominant direction this move (+1/-1)
static int8_t enc_reverse_count_ = 0;
static direction_t enc_last_dir_ = DIR_NONE;

// Wrong-direction detection
static direction_t enc_intended_dir_ = DIR_NONE; // +1 = open commanded, -1 = close commanded

// Direction-correction retry state
static bool enc_dir_correction_pending_ = false;
static direction_t enc_dir_correction_intended_ = DIR_NONE;

static constexpr uint32_t ENC_STOPPED_WATCHDOG_MS = 2000; // Maximum expected gap between encoder pulses during door travel,
                                                          // plus a safety margin. If no pulse arrives within this window the
                                                          // door is declared stopped.

static constexpr int8_t ENC_DIRECTION_CHANGE_THRESHOLD = 3; // Number of consecutive ISR pulses in the opposite direction
                                                            // required to confirm a real direction reversal mid-travel.

static Ticker directionChange = Ticker();

// Grace period for the opener to broadcast a state change after the encoder detects movement.
// If movement continues without an opener update beyond this threshold, it is attributed to manual operation.
static constexpr uint32_t PROTOCOL_STALE_MS = 500;
static uint32_t encoder_motion_onset_ms_ = 0;

static _millis_t enc_last_pulse_ms_ = 0;
static bool enc_watchdog_armed_ = false;

// ─── NVS persistence ─────────────────────────────────────────────────────────

static constexpr char nvram_enc_cal[] = "enc_cal";

struct EncCalBlob
{
  int16_t min;
  int16_t max;
  int16_t last;
  bool min_cal;
  bool max_cal;
};

static void enc_save_cal()
{
  EncCalBlob b = {enc_min_, enc_max_, enc_last_, enc_min_cal_, enc_max_cal_};
  write_door_data(nvram_enc_cal, &b, sizeof(b));
}

static void enc_load_cal()
{
  EncCalBlob b = {};
  if (read_door_data(nvram_enc_cal, &b, sizeof(b)))
  {
    enc_min_ = b.min;
    enc_max_ = b.max;
    enc_last_ = b.last;
    enc_min_cal_ = b.min_cal;
    enc_max_cal_ = b.max_cal;
    ESP_LOGI(TAG, "Calibration loaded: min=%d max=%d last=%d min_cal=%d max_cal=%d", enc_min_, enc_max_, enc_last_, enc_min_cal_, enc_max_cal_);
  }
  else
  {
    ESP_LOGI(TAG, "No saved calibration");
  }
}

// ─── ISR ─────────────────────────────────────────────────────────────────────

static void IRAM_ATTR isr_encoder()
{
  // Quadrature encoder lookup table
  // Index = (prev_state << 2) | curr_state, where state = (a << 1) | b
  // Maps every state transition to +1 (CW) or -1 (CCW)
  // Invalid transitions (skip-2 states from excessive bounce) map to 0
  static const int8_t ENC_TABLE[16] = {
      0,
      -1,
      +1,
      0, // prev=00 → {00,01,10,11}
      +1,
      0,
      0,
      -1, // prev=01 → {00,01,10,11}
      -1,
      0,
      0,
      +1, // prev=10 → {00,01,10,11}
      0,
      +1,
      -1,
      0, // prev=11 → {00,01,10,11}
  };
  bool a = digitalRead(DRY_CONTACT_OPEN_PIN);
  bool b = digitalRead(DRY_CONTACT_CLOSE_PIN);
  uint8_t curr = (static_cast<uint8_t>(a) << 1) | static_cast<uint8_t>(b);
  int8_t step = ENC_TABLE[(enc_prev_state << 2) | curr];
  enc_prev_state = curr;

  if (step == 0)
    return; // invalid/skip-2 transition; update prev_state but don't count

  // Net running sum: accumulate signed steps and emit when the dominant
  // direction has built up 4 net counts. This tolerates an occasional
  // wrong-direction transition from noise.
  enc_cycle_count += step;
  if (enc_cycle_count >= 4)
  {
    enc_delta += 1;
    enc_cycle_count = 0;
  }
  else if (enc_cycle_count <= -4)
  {
    enc_delta -= 1;
    enc_cycle_count = 0;
  }
}

// ─── Notify helpers ──────────────────────────────────────────────────────────
static void encoder_received(GarageDoorCurrentState door_state)
{
  garage_door.encoder_door_state = door_state;

  GarageDoorCurrentState proto_state = garage_door.protocol_door_state;

  // Update encoder_door_position based on the received door_state if it is fully open or fully closed.
  if (door_state == GarageDoorCurrentState::CURR_CLOSED)
    garage_door.encoder_door_position = 0;
  else if (door_state == GarageDoorCurrentState::CURR_OPEN)
    garage_door.encoder_door_position = 100;
  // else will be OPENING, CLOSING or STOPPED, we do not update encoder_door_position here.

  if (proto_state == (GarageDoorCurrentState)0xFF)
  {
    update_door_state(door_state);
    return;
  }

  if ((door_state == GarageDoorCurrentState::CURR_OPENING || door_state == GarageDoorCurrentState::CURR_CLOSING) &&
      (proto_state == GarageDoorCurrentState::CURR_OPEN || proto_state == GarageDoorCurrentState::CURR_CLOSED || proto_state == GarageDoorCurrentState::CURR_STOPPED))
  {
    if (encoder_motion_onset_ms_ == 0)
    {
      encoder_motion_onset_ms_ = _millis();
    }
    else if (_millis() - encoder_motion_onset_ms_ > PROTOCOL_STALE_MS)
    {
      if (!garage_door.manuallyOperated)
      {
        garage_door.manuallyOperated = true;
        notify_homekit_manually_operated(true);
      }
      update_door_state(door_state);
    }
  }
  else
  {
    encoder_motion_onset_ms_ = 0;
    if (door_state == GarageDoorCurrentState::CURR_STOPPED || door_state == GarageDoorCurrentState::CURR_OPEN || door_state == GarageDoorCurrentState::CURR_CLOSED)
    {
      if (garage_door.manuallyOperated)
      {
        update_door_state(door_state);
      }
    }
  }
}

void protocol_received_state(GarageDoorCurrentState door_state)
{

  if (door_state != garage_door.protocol_door_state)
  {
    ESP_LOGI(TAG, "Protocol door state changing from %s to %s (%s)", DOOR_STATE(garage_door.protocol_door_state), DOOR_STATE(door_state), timeString());
    garage_door.protocol_door_state = door_state;

    // If fully closed update the position, do not update for any other state as we do not know the position.
    if (door_state == GarageDoorCurrentState::CURR_CLOSED)
      garage_door.encoder_door_position = 0;
  }

  if (garage_door.manuallyOperated)
  {
    // If we thought the door was manually operated, but the protocol reports a state change, then check if we can reset the manually operated state.
    if (door_state == GarageDoorCurrentState::CURR_OPENING || door_state == GarageDoorCurrentState::CURR_CLOSING)
    {
      garage_door.manuallyOperated = false;
      notify_homekit_manually_operated(false);
    }
    else
    {
      if (door_state != garage_door.encoder_door_state)
      {
        // Drop update, rely on encoder until we see motion from protocol
        return;
      }
      else
      {
        garage_door.manuallyOperated = false;
        notify_homekit_manually_operated(false);
      }
    }
  }
  update_door_state(door_state);
}

// ─── on_encoder_update ───────────────────────────────────────────────────────

static void on_encoder_update(int16_t raw)
{
  int16_t delta = static_cast<int16_t>(raw - enc_last_);
  enc_last_ = raw;

  if (delta == 0)
    return;

  // Track direction so check_encoder_stopped knows which boundary we hit.
  enc_last_dir_ = (delta > 0) ? DIR_OPENING : DIR_CLOSING;

  // Latch the travel direction from the first step of each move.
  // Subsequent steps opposite to the dominant direction are counted; only after
  // ENC_DIRECTION_CHANGE_THRESHOLD consecutive opposite steps is enc_travel_dir_
  // updated, filtering oscillations
  if (enc_travel_dir_ == DIR_NONE)
  {
    enc_travel_dir_ = enc_last_dir_; // first step of a new move
    enc_reverse_count_ = 0;
  }
  else if (enc_last_dir_ != enc_travel_dir_)
  {
    if (++enc_reverse_count_ >= ENC_DIRECTION_CHANGE_THRESHOLD)
    {
      enc_travel_dir_ = enc_last_dir_; // confirmed real reversal
      enc_reverse_count_ = 0;
    }
  }
  else
  {
    enc_reverse_count_ = 0; // step agrees with dominant direction; reset counter
  }

  ESP_LOGD(TAG, "Step=%d min=%d max=%d", raw, enc_min_, enc_max_);

  if (enc_min_cal_ && enc_max_cal_ && enc_max_ != enc_min_)
  {
    int16_t dist_closed = static_cast<int16_t>(std::abs(raw - enc_min_));
    int16_t dist_open = static_cast<int16_t>(std::abs(raw - enc_max_));
    float pos;
    if (dist_closed <= 1 && dist_closed <= dist_open)
    {
      pos = reverse_encoder ? 1.0f : 0.0f;
    }
    else if (dist_open <= 1 && dist_open < dist_closed)
    {
      pos = reverse_encoder ? 0.0f : 1.0f;
    }
    else
    {
      pos = (float)(raw - enc_min_) / (float)(enc_max_ - enc_min_);
      if (reverse_encoder)
        pos = 1.0f - pos;
    }
    // (position not exposed to HomeKit — only OPEN/CLOSED/OPENING/CLOSING/STOPPED)
    garage_door.encoder_door_position = static_cast<uint32_t>(std::round(std::clamp(pos, 0.0f, 1.0f) * 100.0f));
    ESP_LOGD(TAG, "Position: %d%% (dist_closed=%d dist_open=%d)", garage_door.encoder_door_position, dist_closed, dist_open);

    // Derive stable_motion from enc_travel_dir_ (the confirmed dominant direction)
    // rather than enc_last_dir_ so that oscillation noise does not flip the
    // reported door state or cancel the move-to-position timer.
    // enc_travel_dir_ only changes after ENC_DIRECTION_CHANGE_THRESHOLD
    // consecutive opposite steps.
    const direction_t effective_dir = (enc_travel_dir_ != DIR_NONE) ? enc_travel_dir_ : enc_last_dir_;
    const GarageDoorCurrentState stable_motion = (effective_dir == DIR_OPENING) ? (reverse_encoder ? GarageDoorCurrentState::CURR_CLOSING
                                                                                                   : GarageDoorCurrentState::CURR_OPENING)
                                                                                : (reverse_encoder ? GarageDoorCurrentState::CURR_OPENING
                                                                                                   : GarageDoorCurrentState::CURR_CLOSING);

    // const GarageDoorCurrentState instant_motion = (enc_last_dir_ == DIR_OPENING) ? (reverse_encoder ? GarageDoorCurrentState::CURR_CLOSING
    //                                                                                           : GarageDoorCurrentState::CURR_OPENING)
    //                                                                        : (reverse_encoder ? GarageDoorCurrentState::CURR_OPENING
    //                                                                                           : GarageDoorCurrentState::CURR_CLOSING);
    const GarageDoorCurrentState instant_motion = stable_motion;
    // See discussion in https://github.com/ratgdo/homekit-ratgdo32/issues/195
    // rather than remove the separate instant_motion value, I just set it to the same as stable_motion.
    // Doing this just in case we ever need to put it back.

    // Check if the door moved in the opposite direction from what was commanded.
    if (enc_intended_dir_ != DIR_NONE)
    {
      static uint16_t wrong_dir_count = 0;
      bool correct = (instant_motion == GarageDoorCurrentState::CURR_OPENING) == (enc_intended_dir_ == DIR_OPENING);
      if (!enc_watchdog_armed_ && wrong_dir_count != 0)
      {
        wrong_dir_count = 0; // reset if we are staring out from a stopped state
        ESP_LOGD(TAG, "Reset wrong direction detection counter");
      }

      // if (!correct && ++wrong_dir_count > 1)
      if (!correct && ++wrong_dir_count > 0)
      // See discussion in https://github.com/ratgdo/homekit-ratgdo32/issues/195
      // I changed the wrong_dir_count from >1 to >0 which effectively results in always resolving to true (++ increment takes place before the compare)
      // I did this rather than remove all the wrong_dir_count code just in case we ever need to put it back.
      {
        wrong_dir_count = 0; // reset the counter after handling the correction
        direction_t intended = enc_intended_dir_;
        enc_intended_dir_ = DIR_NONE; // clear — correction is firing
        ESP_LOGD(TAG, "Wrong direction detected (wanted %s, got %s); stopping door to correct", DIRECTION_STR(intended), DOOR_STATE(instant_motion));

        directionChange.detach(); // just in case!
        directionChange.once_ms(500, []()
                                { stop_door(); });
        // Defer the retry to check_encoder_stopped()
        enc_dir_correction_pending_ = true;
        enc_dir_correction_intended_ = intended;
      }
      else if (correct)
      {
        wrong_dir_count = 0;
        encoder_received(stable_motion);
        // If correct direction: do NOT clear enc_intended_dir_ here.
        // It stays set so a mid-travel reversal (confirmed after
        // ENC_DIRECTION_CHANGE_THRESHOLD opposite ticks) can still trigger
        // the correction. check_encoder_stopped() clears it when the move ends.
      }
      else
      {
        ESP_LOGD(TAG, "Wrong direction detected (wanted %s, got %s); waiting for second pulse to confirm", DIRECTION_STR(enc_intended_dir_), DOOR_STATE(instant_motion));
      }
    }
    else
    {
      encoder_received(stable_motion);
    }
  }

  // (Re-)arm stopped watchdog
  enc_last_pulse_ms_ = _millis();
  enc_watchdog_armed_ = true;
}

// ─── check_encoder_stopped ───────────────────────────────────────────────────

static void check_encoder_stopped()
{
  ESP_LOGD(TAG, "STOPPED: step=%d min=%d max=%d dir=%d", enc_last_, enc_min_, enc_max_, enc_travel_dir_);
  bool update_pref = false;

  // If a wrong-direction correction was pending, retry the intended action now that
  // the encoder has confirmed the door has actually stopped.
  if (enc_dir_correction_pending_)
  {
    enc_dir_correction_pending_ = false;
    direction_t intended = enc_dir_correction_intended_;
    enc_dir_correction_intended_ = DIR_NONE;
    if (intended == DIR_OPENING)
    {
      ESP_LOGI(TAG, "Direction correction retry: send open");
      open_door();
    }
    else if (intended == DIR_CLOSING)
    {
      ESP_LOGI(TAG, "Direction correction retry: send close");
      close_door(true); // ignore TTC for direction-correction retry
    }
    else
      ESP_LOGE(TAG, "Bad value for direction correction");

    // bail out now... do not do any calibration or boundary snapping until the door has actually moved in the intended direction.
    return;
  }

  // Use the latched travel direction rather than enc_last_dir_ so that
  // magnet-hover oscillations at a limit do not corrupt boundary classification.
  const bool decreasing = (enc_travel_dir_ == DIR_CLOSING);

  // Clear enc_travel_dir_ now so the next move starts with a fresh latch.
  enc_travel_dir_ = DIR_NONE;
  enc_reverse_count_ = 0;
  // Clear enc_intended_dir_ so a stale intent from a previous ratgdo command
  // cannot trigger the wrong-direction correction on a subsequent wall-control command
  enc_intended_dir_ = DIR_NONE;

  const GarageDoorCurrentState boundary_state = decreasing ? (reverse_encoder ? GarageDoorCurrentState::CURR_OPEN
                                                                              : GarageDoorCurrentState::CURR_CLOSED)
                                                           : (reverse_encoder ? GarageDoorCurrentState::CURR_CLOSED
                                                                              : GarageDoorCurrentState::CURR_OPEN);

  if ((!enc_min_cal_ && decreasing) || (!enc_max_cal_ && !decreasing))
  {
    // First time seeing this boundary direction — calibrate it.
    if (decreasing)
    {
      enc_min_ = enc_last_;
      enc_min_cal_ = true;
    }
    else
    {
      enc_max_ = enc_last_;
      enc_max_cal_ = true;
    }
    update_pref = true;
    encoder_received(boundary_state);
    ESP_LOGD(TAG, "Stopped: initial %s boundary set to %d door state set to %s", decreasing ? "min" : "max", enc_last_, DOOR_STATE(boundary_state));
  }
  else if (!enc_min_cal_ || !enc_max_cal_)
  {
    // Hit the same direction twice before the other end was seen — update this end.
    if (decreasing)
    {
      enc_min_ = enc_last_;
      enc_min_cal_ = true;
    }
    else
    {
      enc_max_ = enc_last_;
      enc_max_cal_ = true;
    }
    update_pref = true;
    encoder_received(boundary_state);
    ESP_LOGD(TAG, "Stopped: re-set %s boundary set to %d door state set to %s", decreasing ? "lower(min)" : "upper(max)", enc_last_, DOOR_STATE(boundary_state));
  }
  else
  {
    // Both boundaries calibrated.
    // Check if we stopped within 1 pulse of a limit.
    int16_t target_closed = reverse_encoder ? enc_max_ : enc_min_;
    int16_t target_open = reverse_encoder ? enc_min_ : enc_max_;
    int16_t dist_closed = static_cast<int16_t>(std::abs(enc_last_ - target_closed));
    int16_t dist_open = static_cast<int16_t>(std::abs(enc_last_ - target_open));

    // Define approaching flags dynamically
    const bool approaching_closed = (decreasing == !reverse_encoder);
    const bool approaching_open = (decreasing == reverse_encoder);

    // A door moving 'beyond' a limit is just moving towards it and overshooting.
    // So beyond_open means it is approaching OPEN and overshot.
    const bool beyond_open = approaching_open && (decreasing ? (enc_last_ < enc_min_) : (enc_last_ > enc_max_));
    const bool beyond_closed = approaching_closed && (decreasing ? (enc_last_ < enc_min_) : (enc_last_ > enc_max_));

    if (dist_closed <= 1 && dist_closed <= dist_open && approaching_closed)
    {
      // Snap CLOSED boundary — only when approaching CLOSED.
      if (decreasing)
        enc_min_ = enc_last_;
      else
        enc_max_ = enc_last_;
      update_pref = true;
      ESP_LOGD(TAG, "CLOSED boundary snapped to %d (min=%d max=%d)", enc_last_, enc_min_, enc_max_);
      encoder_received(GarageDoorCurrentState::CURR_CLOSED);
    }
    else if (dist_open <= 1 && dist_open < dist_closed && approaching_open)
    {
      // Snap OPEN boundary — only when approaching OPEN.
      if (decreasing)
        enc_min_ = enc_last_;
      else
        enc_max_ = enc_last_;
      update_pref = true;
      ESP_LOGD(TAG, "OPEN boundary snapped to %d (min=%d max=%d)", enc_last_, enc_min_, enc_max_);
      encoder_received(GarageDoorCurrentState::CURR_OPEN);
    }
    else if (beyond_open)
    {
      // Door stopped past the known OPEN boundary — extend it and snap OPEN.
      if (decreasing)
        enc_min_ = enc_last_;
      else
        enc_max_ = enc_last_;
      update_pref = true;
      ESP_LOGD(TAG, "OPEN boundary extended to %d (min=%d max=%d)", enc_last_, enc_min_, enc_max_);
      encoder_received(GarageDoorCurrentState::CURR_OPEN);
    }
    else if (beyond_closed)
    {
      // Door stopped past the known CLOSED boundary — extend it and snap CLOSED.
      if (decreasing)
        enc_min_ = enc_last_;
      else
        enc_max_ = enc_last_;
      update_pref = true;
      ESP_LOGD(TAG, "CLOSED boundary extended to %d (min=%d max=%d)", enc_last_, enc_min_, enc_max_);
      encoder_received(GarageDoorCurrentState::CURR_CLOSED);
    }
    else
    {
      encoder_received(GarageDoorCurrentState::CURR_STOPPED);
    }
  }

  if (update_pref)
  {
    enc_save_cal();
  }
}

// ─── Public API ──────────────────────────────────────────────────────────────

void setup_encoder()
{
  if (encoder_setup_done)
    return;

  if (!userConfig->getEncoderEnabled())
  {
    enable_service_homekit_manually_operated(false);
    return;
  }
  encoder_enabled = true;
  reverse_encoder = userConfig->getEncoderReversed();

  enc_load_cal();

  pinMode(DRY_CONTACT_OPEN_PIN, INPUT_PULLUP);
  pinMode(DRY_CONTACT_CLOSE_PIN, INPUT_PULLUP);

  // Initialise prev_state from actual pin levels to avoid a spurious first tick
  bool pa = digitalRead(DRY_CONTACT_OPEN_PIN);
  bool pb = digitalRead(DRY_CONTACT_CLOSE_PIN);
  enc_prev_state = (uint8_t)(((uint8_t)pa << 1) | (uint8_t)pb);
  enc_delta = 0;

  ESP_LOGD(TAG, "Initial state: A=%d B=%d prev_state=%02x", pa, pb, enc_prev_state);

  attachInterrupt(digitalPinToInterrupt(DRY_CONTACT_OPEN_PIN), isr_encoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(DRY_CONTACT_CLOSE_PIN), isr_encoder, CHANGE);

  // Derive initial door state from saved calibration if available
  if (enc_min_cal_ && enc_max_cal_ && enc_max_ != enc_min_)
  {
    int16_t target_closed = reverse_encoder ? enc_max_ : enc_min_;
    int16_t target_open = reverse_encoder ? enc_min_ : enc_max_;
    int16_t d_closed = (int16_t)abs(enc_last_ - target_closed);
    int16_t d_open = (int16_t)abs(enc_last_ - target_open);
    GarageDoorCurrentState startup_state;
    if (d_closed <= 1 && d_closed <= d_open)
    {
      startup_state = GarageDoorCurrentState::CURR_CLOSED;
      garage_door.encoder_door_position = 0;
    }
    else if (d_open <= 1 && d_open < d_closed)
    {
      startup_state = GarageDoorCurrentState::CURR_OPEN;
      garage_door.encoder_door_position = 100;
    }
    else
    {
      startup_state = GarageDoorCurrentState::CURR_STOPPED;
      garage_door.encoder_door_position = 0xFF; // unknown
    }
    // Set both variables: doorState is the comms-loop source of truth;
    // garage_door.current_state is read by the web UI JSON builder.
    doorState = startup_state;
    garage_door.current_state = startup_state;
    ESP_LOGI(TAG, "Startup state: %s", DOOR_STATE(startup_state));
  }
  else
  {
    ESP_LOGI(TAG, "Not yet calibrated; door state unknown");
  }

  enc_watchdog_armed_ = false;
  ESP_LOGI(TAG, "ISR attached: A=GPIO%d B=GPIO%d reversed=%d", DRY_CONTACT_OPEN_PIN, DRY_CONTACT_CLOSE_PIN, reverse_encoder);

  enable_service_homekit_manually_operated(true);
  encoder_setup_done = true;
}

void encoder_loop()
{
  if (!encoder_setup_done)
    return;

  // Drain ISR delta every ~100 ms
  static _millis_t last_drain_ms = 0;
  _millis_t now = _millis();

  if (now - last_drain_ms >= 100)
  {
    last_drain_ms = now;
    int16_t delta;
    noInterrupts();
    delta = enc_delta;
    enc_delta = 0;
    interrupts();
    if (delta != 0)
      on_encoder_update((int16_t)(enc_last_ + delta));
  }

  // Stopped watchdog
  // Re-read _millis() here rather than reusing the stale `now` captured at the
  // top of this function, since on_encoder_update() above may have taken
  // long enough (e.g. Debug-level logging) that enc_last_pulse_ms_ ends up
  // greater than the stale `now`, underflowing this unsigned subtraction.
  if (enc_watchdog_armed_ && (_millis() - enc_last_pulse_ms_ >= ENC_STOPPED_WATCHDOG_MS))
  {
    enc_watchdog_armed_ = false;
    check_encoder_stopped();
  }
}

void reset_encoder_cal()
{
  noInterrupts();
  enc_delta = 0;
  enc_cycle_count = 0;
  interrupts();

  enc_last_ = 0;
  enc_min_ = 0;
  enc_max_ = 0;
  enc_min_cal_ = false;
  enc_max_cal_ = false;
  enc_travel_dir_ = DIR_NONE;
  enc_reverse_count_ = 0;
  enc_intended_dir_ = DIR_NONE;
  enc_dir_correction_pending_ = false;
  enc_watchdog_armed_ = false;

  EncCalBlob b = {};
  write_door_data(nvram_enc_cal, &b, sizeof(b));
  ESP_LOGI(TAG, "Calibration cleared; will re-learn on next full open/close cycle");
}

void encoder_set_intended_open() { enc_intended_dir_ = DIR_OPENING; }
void encoder_set_intended_close() { enc_intended_dir_ = DIR_CLOSING; }

int16_t encoder_last_step() { return enc_last_; }
#endif // RATGDO_ENCODER
