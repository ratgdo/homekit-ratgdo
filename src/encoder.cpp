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

// Direction tracking (for stopped-watchdog and reverse detection)
static bool reversed = false;      // userConfig->getEncoderReversed()
static int8_t enc_travel_dir_ = 0; // dominant direction this move (+1/-1)
static int8_t enc_reverse_count_ = 0;
static int8_t enc_last_dir_ = 0;

// Wrong-direction detection
static int8_t enc_intended_dir_ =
    0; // +1 = open commanded, -1 = close commanded

// Direction-correction retry state
static bool enc_dir_corr_pending_ = false;
static int8_t enc_dir_corr_intended_ = 0;

// Watchdog: ms with no pulse before door is declared stopped
static constexpr _millis_t ENC_STOPPED_MS = 3000;
// Consecutive opposite-direction pulses before travel direction reversal is confirmed
static constexpr int8_t ENC_DIR_CHANGE_THRESHOLD = 3;

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
    ESP_LOGI(TAG,  "Calibration loaded: min=%d max=%d last=%d min_cal=%d max_cal=%d", enc_min_, enc_max_, enc_last_, enc_min_cal_, enc_max_cal_);
  }
  else
  {
    ESP_LOGI(TAG, "No saved calibration");
  }
}

// ─── ISR ─────────────────────────────────────────────────────────────────────

static void IRAM_ATTR isr_encoder()
{
  // Quadrature lookup table: index = (prev<<2)|curr, value = step (+1/0/-1)
  static const int8_t ENC_TABLE[16] = {
      0,
      -1,
      +1,
      0, // prev=00 → {00,01,10,11}
      +1,
      0,
      0,
      -1, // prev=01
      -1,
      0,
      0,
      +1, // prev=10
      0,
      +1,
      -1,
      0, // prev=11
  };
  bool a = digitalRead(DRY_CONTACT_OPEN_PIN);
  bool b = digitalRead(DRY_CONTACT_CLOSE_PIN);
  uint8_t curr = (uint8_t)(((uint8_t)a << 1) | (uint8_t)b);
  int8_t step = ENC_TABLE[(enc_prev_state << 2) | curr];
  enc_prev_state = curr;

  if (step == 0)
    return; // invalid/skip-2 transition

  // Net accumulator: emit a verified step at ±4 to filter noise
  enc_cycle_count += step;
  if (enc_cycle_count >= 4)
  {
    enc_delta++;
    enc_cycle_count = 0;
  }
  else if (enc_cycle_count <= -4)
  {
    enc_delta--;
    enc_cycle_count = 0;
  }
}

// ─── Notify helpers ──────────────────────────────────────────────────────────
static void encoder_received(GarageDoorCurrentState door_state)
{
  garage_door.encoder_door_state = door_state;

  GarageDoorCurrentState proto_state = garage_door.protocol_door_state;

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
  int16_t delta = (int16_t)(raw - enc_last_);
  enc_last_ = raw;
  if (delta == 0)
    return;

  enc_last_dir_ = (delta > 0) ? 1 : -1;

  // Latch dominant travel direction; require ENC_DIR_CHANGE_THRESHOLD
  // consecutive opposite ticks before updating it.
  if (enc_travel_dir_ == 0)
  {
    enc_travel_dir_ = enc_last_dir_;
    enc_reverse_count_ = 0;
  }
  else if (enc_last_dir_ != enc_travel_dir_)
  {
    if (++enc_reverse_count_ >= ENC_DIR_CHANGE_THRESHOLD)
    {
      enc_travel_dir_ = enc_last_dir_;
      enc_reverse_count_ = 0;
    }
  }
  else
  {
    enc_reverse_count_ = 0;
  }

  ESP_LOGD(TAG, "Step=%d min=%d max=%d", raw, enc_min_, enc_max_);

  if (enc_min_cal_ && enc_max_cal_ && enc_max_ != enc_min_)
  {
    float pos = (float)(raw - enc_min_) / (float)(enc_max_ - enc_min_);
    if (reversed)
      pos = 1.0f - pos;
    if (pos < 0.0f)
      pos = 0.0f;
    if (pos > 1.0f)
      pos = 1.0f;
    // (position not exposed to HomeKit — only OPEN/CLOSED/OPENING/CLOSING)

    int8_t effective_dir = (enc_travel_dir_ != 0) ? enc_travel_dir_ : enc_last_dir_;
    GarageDoorCurrentState in_motion = (effective_dir > 0) ? (reversed ? GarageDoorCurrentState::CURR_CLOSING
                                                                       : GarageDoorCurrentState::CURR_OPENING)
                                                           : (reversed ? GarageDoorCurrentState::CURR_OPENING
                                                                       : GarageDoorCurrentState::CURR_CLOSING);

    if (garage_door.current_state != in_motion)
    {
      // Wrong-direction detection
      if (enc_intended_dir_ != 0)
      {
        bool correct = (in_motion == GarageDoorCurrentState::CURR_OPENING) == (enc_intended_dir_ > 0);
        if (!correct)
        {
          int8_t intended = enc_intended_dir_;
          enc_intended_dir_ = 0;
          ESP_LOGW(TAG, "Wrong direction (wanted %s, got %s); will correct when stopped", intended > 0 ? "Opening" : "Closing", DOOR_STATE(in_motion));
          enc_dir_corr_pending_ = true;
          enc_dir_corr_intended_ = intended;
        }
      }
      encoder_received(in_motion);
    }
  }

  // (Re-)arm stopped watchdog
  enc_last_pulse_ms_ = _millis();
  enc_watchdog_armed_ = true;
}

// ─── check_encoder_stopped ───────────────────────────────────────────────────

static void check_encoder_stopped()
{
  ESP_LOGD(TAG, "Stopped: step=%d min=%d max=%d dir=%d", enc_last_, enc_min_, enc_max_, enc_travel_dir_);

  bool decreasing = (enc_travel_dir_ < 0);

  enc_travel_dir_ = 0;
  enc_reverse_count_ = 0;
  // Clear stale intent so a subsequent wall-control move in the opposite
  // direction is never misidentified as wrong-direction and corrected.
  enc_intended_dir_ = 0;

  const GarageDoorCurrentState boundary_state =
      decreasing ? (reversed ? GarageDoorCurrentState::CURR_OPEN
                             : GarageDoorCurrentState::CURR_CLOSED)
                 : (reversed ? GarageDoorCurrentState::CURR_CLOSED
                             : GarageDoorCurrentState::CURR_OPEN);

  bool save = false;

  if ((!enc_min_cal_ && decreasing) || (!enc_max_cal_ && !decreasing))
  {
    // First time seeing this boundary — calibrate
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
    save = true;
    encoder_received(boundary_state);
    ESP_LOGD(TAG, "Stopped: initial %s boundary set to %d door state set to %s", decreasing ? "min" : "max", enc_last_, DOOR_STATE(boundary_state));
  }
  else if (!enc_min_cal_ || !enc_max_cal_)
  {
    // Saw same direction again before hitting the other end
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
    save = true;
    encoder_received(boundary_state);
    ESP_LOGD(TAG, "Stopped: %s boundary set to %d door state set to %s", decreasing ? "min" : "max", enc_last_, DOOR_STATE(boundary_state));
  }
  else
  {
    // Both boundaries calibrated — snap/extend logic
    int16_t target_closed = reversed ? enc_max_ : enc_min_;
    int16_t target_open = reversed ? enc_min_ : enc_max_;
    int16_t d_closed = (int16_t)abs(enc_last_ - target_closed);
    int16_t d_open = (int16_t)abs(enc_last_ - target_open);

    bool beyond_open = !decreasing && (enc_last_ > enc_max_);
    bool beyond_closed = decreasing && (enc_last_ < enc_min_);

    if (d_closed <= 1 && d_closed <= d_open && decreasing)
    {
      if (enc_last_ <= enc_min_)
      {
        enc_min_ = enc_last_;
        save = true;
        ESP_LOGD(TAG, "Stopped: snapped to %d, set door state to Closed", enc_last_);
      }
      encoder_received(GarageDoorCurrentState::CURR_CLOSED);
    }
    else if (d_open <= 1 && d_open < d_closed && !decreasing)
    {
      if (enc_last_ >= enc_max_)
      {
        enc_max_ = enc_last_;
        save = true;
        ESP_LOGD(TAG, "Stopped: snapped to %d, set door state to Open", enc_last_);
      }
      encoder_received(GarageDoorCurrentState::CURR_OPEN);
    }
    else if (beyond_open)
    {
      enc_max_ = enc_last_;
      save = true;
      ESP_LOGD(TAG, "Stopped: boundary extended to %d, set door state to Open", enc_last_);
      encoder_received(GarageDoorCurrentState::CURR_OPEN);
    }
    else if (beyond_closed)
    {
      enc_min_ = enc_last_;
      save = true;
      ESP_LOGD(TAG, "Stopped: boundary extended to %d, set door state to Closed", enc_last_);
      encoder_received(GarageDoorCurrentState::CURR_CLOSED);
    }
    else
    {
      ESP_LOGD(TAG, "Stopped: set door state to Stopped");
      encoder_received(GarageDoorCurrentState::CURR_STOPPED);
    }
  }

  if (save)
  {
    enc_save_cal();
  }

  // Direction-correction retry: now that door has stopped, send the corrected
  // command
  if (enc_dir_corr_pending_)
  {
    enc_dir_corr_pending_ = false;
    int8_t intended = enc_dir_corr_intended_;
    enc_dir_corr_intended_ = 0;
    ESP_LOGI(TAG, "Direction correction retry: sending %s", intended > 0 ? "Open" : "Close");
    if (intended > 0)
      open_door();
    else
      close_door(true); // ignore TTC for direction-correction retry
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
  reversed = userConfig->getEncoderReversed();

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
    int16_t target_closed = reversed ? enc_max_ : enc_min_;
    int16_t target_open = reversed ? enc_min_ : enc_max_;
    int16_t d_closed = (int16_t)abs(enc_last_ - target_closed);
    int16_t d_open = (int16_t)abs(enc_last_ - target_open);
    GarageDoorCurrentState startup_state;
    if (d_closed <= 1 && d_closed <= d_open)
      startup_state = GarageDoorCurrentState::CURR_CLOSED;
    else if (d_open <= 1 && d_open < d_closed)
      startup_state = GarageDoorCurrentState::CURR_OPEN;
    else
      startup_state = GarageDoorCurrentState::CURR_STOPPED;
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
  ESP_LOGI(TAG, "ISR attached: A=GPIO%d B=GPIO%d reversed=%d", DRY_CONTACT_OPEN_PIN, DRY_CONTACT_CLOSE_PIN, reversed);

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
  if (enc_watchdog_armed_ && (_millis() - enc_last_pulse_ms_ >= ENC_STOPPED_MS))
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
  enc_travel_dir_ = 0;
  enc_reverse_count_ = 0;
  enc_intended_dir_ = 0;
  enc_dir_corr_pending_ = false;
  enc_watchdog_armed_ = false;

  EncCalBlob b = {};
  write_door_data(nvram_enc_cal, &b, sizeof(b));
  ESP_LOGI(TAG, "Calibration cleared; will re-learn on next full open/close cycle");
}

void encoder_set_intended_open() { enc_intended_dir_ = 1; }
void encoder_set_intended_close() { enc_intended_dir_ = -1; }

int16_t encoder_last_step() { return enc_last_; }
#endif // RATGDO_ENCODER