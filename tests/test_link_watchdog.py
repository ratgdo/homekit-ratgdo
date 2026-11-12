"""Native check of the link watchdog decision logic in src/ratgdo.cpp."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

// Feed one full window of sessions with the given replies per session.
static LinkResult window(LinkWatch &w, unsigned replies) {
    LinkResult r{};
    for (int i = 0; i < LINK_WINDOW_CHECKS; ++i) {
        r = link_watch_step(w, LINK_PINGS_PER_CHECK, replies);
        if (i < LINK_WINDOW_CHECKS - 1) assert(!r.evaluated && r.action == LINK_NONE);
    }
    return r;
}

// Feed `n` windows with the given replies per session; return windows (0-based) that reconnected.
static std::vector<int> run(LinkWatch &w, int n, unsigned replies) {
    std::vector<int> at;
    for (int i = 0; i < n; ++i) {
        LinkResult r = window(w, replies);
        assert(r.action != LINK_RECOVERED);
        if (r.action == LINK_RECONNECT) at.push_back(i);
    }
    return at;
}

int main() {
    const unsigned all = LINK_PINGS_PER_CHECK;

    { // Gateway that never answers ping: never act.
        LinkWatch w;
        assert(run(w, 10, 0).empty());
    }
    { // Healthy link, and light loss below threshold: no action.
        LinkWatch w;
        assert(window(w, all).action == LINK_NONE);
        LinkResult r = link_watch_step(w, all, all - 1); // 1 lost of 20 = 5%
        for (int i = 1; i < LINK_WINDOW_CHECKS; ++i) r = link_watch_step(w, all, all);
        assert(r.evaluated && r.lossPct == 5 && r.action == LINK_NONE);
    }
    { // Field pattern A: device-side stall that one reconnect clears.
        LinkWatch w;
        window(w, all);
        LinkResult r = window(w, all / 2); // 60% loss
        assert(r.action == LINK_RECONNECT && r.lossPct == 60 && r.holdWindows == LINK_BACKOFF_FIRST_WINDOWS);
        r = window(w, 0); // window spanning our own reconnect is discarded
        assert(r.action == LINK_NONE && !r.evaluated);
        for (int i = 1; i < LINK_RECOVERED_WINDOWS; ++i) assert(window(w, all).action == LINK_NONE);
        assert(window(w, all).action == LINK_RECOVERED);
        // Recovery resets backoff: the next episode reconnects at once, then waits the first hold again.
        assert(window(w, 0).action == LINK_RECONNECT);
        std::vector<int> at = run(w, 3, 0);
        assert(at.size() == 1 && at[0] == LINK_BACKOFF_FIRST_WINDOWS - 1);
    }
    { // Field pattern B: congested channel for ~6.5h that no local action fixes.
      // Rebooting looped every ~6-15 min; now reconnects back off 4, 8, 16, 32, 32... min and never reboot.
        LinkWatch w;
        window(w, all);
        const int windows = 195; // 6.5h of 2-min windows
        std::vector<int> at = run(w, windows, all / 5); // 80% loss
        assert(at.front() == 0);
        int expect = LINK_BACKOFF_FIRST_WINDOWS;
        for (size_t i = 1; i < at.size(); ++i) {
            assert(at[i] - at[i - 1] == expect);
            if (expect < LINK_BACKOFF_MAX_WINDOWS) expect *= 2;
        }
        assert(at.size() >= 10 && at.size() <= 16);
        // Channel clears: recovered after the healthy period, backoff reset.
        LinkResult r{};
        for (int i = 0; i < LINK_RECOVERED_WINDOWS + 1 && r.action != LINK_RECOVERED; ++i) r = window(w, all);
        assert(r.action == LINK_RECOVERED);
    }
    { // A relapse before recovery is proven keeps the backoff growing (no reset on a short good spell).
        LinkWatch w;
        window(w, all);
        assert(window(w, 0).action == LINK_RECONNECT);      // hold 2, next backoff 4
        window(w, 0);                                       // skipped
        assert(window(w, all).action == LINK_NONE);         // healthy, hold expires
        LinkResult r = window(w, 0);
        assert(r.action == LINK_RECONNECT && r.holdWindows == 2 * LINK_BACKOFF_FIRST_WINDOWS);
    }
    { // Sessions that never ran (sent == 0) and bogus counts are ignored safely.
        LinkWatch w;
        assert(link_watch_step(w, 0, 0).action == LINK_NONE);
        LinkResult r{};
        for (int i = 0; i < LINK_WINDOW_CHECKS; ++i) r = link_watch_step(w, all, all + 3);
        assert(r.evaluated && r.lossPct == 0);
    }
    std::puts("ok");
}
'''


class LinkWatchdogTest(unittest.TestCase):
    def test_decisions(self):
        with tempfile.TemporaryDirectory(prefix="ratgdo-link-") as d:
            cpp, binary = Path(d) / "t.cpp", Path(d) / "t"
            source = (ROOT / "src/ratgdo.cpp").read_text()
            logic = source[source.index("// Calibration knobs"):
                           source.index("static esp_ping_handle_t link_ping")]
            cpp.write_text(HARNESS.replace("#include <vector>\n", "#include <vector>\n" + logic, 1))
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined",
                            str(cpp), "-o", str(binary)], check=True)
            self.assertEqual(subprocess.check_output([str(binary)], text=True).strip(), "ok")


if __name__ == "__main__":
    unittest.main()
