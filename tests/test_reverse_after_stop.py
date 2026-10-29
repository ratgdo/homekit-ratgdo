#!/usr/bin/env python3
"""Compile the production reversal and close-decision blocks with fake I/O."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

SOURCE = Path(os.environ.get("REVERSE_TEST_SOURCE", Path(__file__).resolve().parents[1] / "src/comms.cpp"))


def block(text, start):
    opening = text.index("{", start)
    depth = 0
    for i in range(opening, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    raise AssertionError("unclosed block")


class ReverseAfterStop(unittest.TestCase):
    def test_stopped_state_precedes_one_followup(self):
        text = SOURCE.read_text()
        update = block(text, text.index("void update_door_state("))
        notify = update.index("notify_homekit_current_door_state_change(current_state)")
        first_command = min(update.index("door_command_open()"), update.index("door_command_close()"))
        self.assertLess(notify, first_command, "follow-up must see the new stopped state")
        followup = update.index("if (current_state == GarageDoorCurrentState::CURR_STOPPED &&")
        followup_code = block(update, followup)
        close_start = text.index("void door_command_close()\n{")
        close = block(text, close_start)
        decision_start = close.index("    if (", close.index("#else"))
        decision_end = close.index("    if (doorControlType == DOOR_CONTROL_SEC_PLUS_V2", decision_start)
        decision = close[decision_start:decision_end]
        harness = r"""
#include <cassert>
#include <cstdint>
using _millis_t = uint64_t;
enum GarageDoorCurrentState { CURR_OPEN, CURR_OPENING, CURR_STOPPED };
enum DoorAction { Close, Toggle };
struct Door { bool pinModeObstructionSensor=false; GarageDoorCurrentState current_state=CURR_OPENING; int target_state=0; } garage_door;
struct Config { bool getUseToggle() { return false; } } config, *userConfig=&config;
constexpr int DOOR_CONTROL_DRY_CONTACT=0;
int doorControlType=2;
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define DOOR_STATE(...) "state"
const char *TAG="test";
_millis_t now=1000, stopSentClosePending=0, stopSentOpenPending=0;
_millis_t _millis() { return now; }
int toggles=0, closes=0, opens=0, timers=0;
bool queueAccepts=true;
bool door_command(DoorAction a) { if (!queueAccepts) return false; if (a == Toggle) ++toggles; else ++closes; return true; }
void door_command_open() { ++opens; }
void door_command_close() {
""" + decision + r"""
    ++timers;
}
void follow(GarageDoorCurrentState current_state) {
""" + followup_code + r"""
}
int main() {
    stopSentClosePending=900;
    garage_door.current_state=CURR_STOPPED;
    follow(CURR_STOPPED);
    assert(toggles==1 && closes==0 && timers==1);
    assert(stopSentClosePending==0 && stopSentOpenPending==0);
    follow(CURR_STOPPED); assert(toggles==1);
    garage_door.current_state=CURR_STOPPED; queueAccepts=false;
    door_command_close(); assert(toggles==1 && timers==1);
    queueAccepts=true; garage_door.current_state=CURR_OPENING;
    door_command_close(); assert(toggles==1 && timers==1);
    stopSentClosePending=1; now=2001; garage_door.current_state=CURR_STOPPED;
    follow(CURR_STOPPED); assert(toggles==1 && stopSentClosePending==0);
    now=3000; stopSentOpenPending=2900;
    follow(CURR_STOPPED); assert(opens==1 && stopSentOpenPending==0);
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            cpp = Path(tmp) / "reverse.cpp"
            exe = Path(tmp) / "reverse"
            cpp.write_text(harness)
            subprocess.run(["c++", "-std=c++11", str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
