# Change Log

All notable changes to `homekit-ratgdo` will be documented in this file. This project tries to adhere to [Semantic Versioning](http://semver.org/).

## v2.1.4 (2026-02-??)

### What's Changed

* Bugfix: Inform browser whenever IP address is set or changed
* Bugfix: Fix setting keyname mismatch between browser and server (homekitLight / lightHomeKit)
* Feature: Allow user to set NTP server URL

### Known Issues

* Sec+ 1.0 doors with digital wall panel (e.g. 889LM) sometimes do not close after a time-to-close delay. Please watch your door to make sure it closes after TTC delay.
* Sec+ 1.0 doors with "0x37" digital wall panel (e.g. 398LM) not working. We now detect but will not support them.  Recommend replacing with 889LM panel.
* When creating automations in Apple Home the garage door may show only lock/unlock and not open/close as triggers. This is a bug in Apple Home. Workaround is to use the Eve App to create the automation, it will show both options.
* ESP8266 (original ratgdo) only... possible crash when a storm of HomeKit messages arrives... which may be triggered on a upgrade of Apple iOS/tvOS/etc. versions. System recovers.

## v2.1.3 (2026-01-11)

### What's Changed

* Bugfix: (Sec+2.0 only) door not closing if ratgdo thinks it is still opening (rightly or wrongly). https://github.com/ratgdo/homekit-ratgdo32/issues/131
* Bugfix: Escape backslash and double quotes inside JSON strings.  https://github.com/ratgdo/homekit-ratgdo32/issues/134
* Bugfix/feature: (Sec+2.0 only) allow user to select sending TOGGLE command instead of CLOSE. https://github.com/ratgdo/homekit-ratgdo32/issues/131
* Feature: Hardwired Sec+ GPIO Controls Mirror Wall Panel, Optional TTC Bypass. https://github.com/ratgdo/homekit-ratgdo32/pull/136
* Feature: Publish ratgdo and door status over mDNS
* Other: Update settings page visuals to disable/enable options rather than hide/show.

### Known Issues

* Sec+ 1.0 doors with digital wall panel (e.g. 889LM) sometimes do not close after a time-to-close delay. Please watch your door to make sure it closes after TTC delay.
* Sec+ 1.0 doors with "0x37" digital wall panel (e.g. 398LM) not working. We now detect but will not support them.  Recommend replacing with 889LM panel.
* When creating automations in Apple Home the garage door may show only lock/unlock and not open/close as triggers. This is a bug in Apple Home. Workaround is to use the Eve App to create the automation, it will show both options.
* ESP8266 (original ratgdo) only... possible crash when a storm of HomeKit messages arrives... which may be triggered on a upgrade of Apple iOS/tvOS/etc. versions. System recovers.

## v2.1.2 (2025-12-xx)

### What's Changed

* Feature: Query the state of emergency back up battery on boot and every 55 minutes (Sec+2.0 only).
* Bugfix: If firmware upload error detected before update begins, do not require a reboot.
* Other: Average vehicle distance over larger sample size (now 50) to smooth out spurious readings (ratgdo32-disco only).

## v2.1.1 (2025-11-22)

### What's Changed

* Bugfix: re-Announce ratgdo mDNS every two minutes, so that we remain visible on network (default TTL is 2 minutes)
* Bugfix: The home icon at top/right of the system logs page was not always returning to ratgdo main page https://github.com/ratgdo/homekit-ratgdo/issues/318
* Feature: Add a [webmanifest](https://developer.mozilla.org/en-US/docs/Web/Progressive_web_apps/Manifest) file and update all browser favorite icons for better visuals
* Feature: Add support for Captive Network Assistant (CNA) so that Apple and Android devices will automatically load WiFi provisioning page when connecting to ratgdo Soft Access Point (Soft AP mode)
* Feature: Add a warning and countdown timer to web page when Sec+2.0 doors have automatic door close (TTC) active
* Other: Display "Off" instead of "0" when settings sliders are set to zero seconds/minutes
* Other: Improved web page design for iPhone and iPad devices
* Other: Attempt to recover from out-of-sync Sec+2.0 rolling code https://github.com/ratgdo/homekit-ratgdo/issues/315
* Other: Various log message cleanup to make debugging easier and reduce log clutter at default Info level

## v2.1.0 (2025-11-01)

### What's Changed

* Bugfix: User selected syslog facility not restored on startup. https://github.com/ratgdo/homekit-ratgdo32/issues/116
* Bugfix: Crash when HomeKit tries to open or close a dry contact door. https://github.com/ratgdo/homekit-ratgdo32/issues/117
* Bugfix: Sec+2.0 only, not handling packet transmit errors during initialization
* Feature: Sec+2.0 only, support garage door automatic close after selected delay, [SEE README](https://github.com/ratgdo/homekit-ratgdo/blob/main/README.md#automatic-close)
* Feature: Add time-to-close countdown timer to web page
* Other: Allow user to disable triggering motion from Sec+2.0 wall panel motion sensors
* Other: Adjust some Info-level log messages to Debug- or Error-level... reduces log clutter at default Info level

## v2.0.9 (2025-10-24)

### What's Changed

* Bugfix: Update function that calculates median door open/close duration. https://github.com/ratgdo/homekit-ratgdo/issues/309
* Bugfix: Do not cancel time-to-close if second door close request received. https://github.com/ratgdo/homekit-ratgdo32/issues/112
* Other: Additional Serial CLI commands for development and debugging to e.g, provision WiFi SSID and password.

## v2.0.8 (2025-10-19)

### What's Changed

* Bugfix: dry contact doors not reporting status correctly on web page. https://github.com/ratgdo/homekit-ratgdo32/issues/109
* Bugfix: Sec+1.0 add timeout when waiting for GDO reply to poll commands https://github.com/ratgdo/homekit-ratgdo32/issues/111
* Other: Save door open/close durations so not reset to unknown on a reboot
* Other: Add timers to check that door starts to open/close and reaches fully opened/closed state in expected time
* Other: Sec+2.0 use MotorOn packet to error correct if we miss notification packet of door opening or closing
* Other: Add serial CLI commands to scan WiFi networks and reset door ID & rolling codes
* Other: Remove known issues list from prior versions in CHANGELOG.md... because they are now repeating

## v2.0.7 (2025-10-12)

### What's Changed

* Bugfix: Sec+2.0 doors not opening or closing. Issue https://github.com/ratgdo/homekit-ratgdo/issues/305

## v2.0.6 (2025-10-12)

### What's Changed

* Bugfix: HomeKit pairing failed with out-of-compliance error. https://github.com/ratgdo/homekit-ratgdo/issues/300
* Bugfix: Web wage status occasionally getting out-of-sync with actual light/lock/door state.
* Bugfix: Door open/close duration calculation not handling cases where door reverses before reaching open/close state.
* Bugfix: Setting syslog port not taking effect until after reboot. https://github.com/ratgdo/homekit-ratgdo/issues/304
* Bugfix: do not attempt to act on Sec+2.0 packet that failed to decode. Issue https://github.com/ratgdo/homekit-ratgdo32/issues/106
* Feature: Change WiFi and MDNS hostname when user changes GDO name. https://github.com/ratgdo/homekit-ratgdo32/issues/93
* Feature: Add clipboard copy icon to IP address and mDNS name.
* Other: Detect (but do not support) 0x37 wall panels like LiftMaster 398LM. https://github.com/ratgdo/homekit-ratgdo32/issues/95
* Other: ESP8266 (original ratgdo) only... suspend GDO communications during HomeKit pairing process.
* Other: ESP8266 (original ratgdo) only... move more constants into PROGMEM and optimize use of system stack.

## v2.0.5 (2025-09-28)

### What's Changed

* Bugfix: Add error handling for a blank SSID... force boot into Soft AP mode, https://github.com/ratgdo/homekit-ratgdo/issues/295
* Bugfix: Buffer overrun that caused Improv setup to fail, https://github.com/ratgdo/homekit-ratgdo/issues/298
* Bugfix: Log messages that are truncated for exceeding buffer size not null terminated
* Other: Add simple serial console CLI to allow setting debug level, displaying saved logs and request reboot.

## v2.0.4 (2025-09-27)

### What's Changed

* Bugfix: Activity LED blink was not obeying user preferences (e.g. always off). https://github.com/ratgdo/homekit-ratgdo/issues/292
* Bugfix: Ignore implausibly long door opening / closing times. https://github.com/ratgdo/homekit-ratgdo32/issues/98
* Feature: Allow user selection of Syslog facility number (Local0 .. Local7). https://github.com/ratgdo/homekit-ratgdo32/issues/94
* Feature: Add MDNS service for _http._tcp to allow local network name discovery. https://github.com/ratgdo/homekit-ratgdo32/issues/93
* Other: Disable HomeKit and garage door communications during OTA firmware update.
* Other: Miscellaneous stability improvements.

## v2.0.3 (2025-09-21)

### What's Changed

* Bugfix: Issue #291 Watchdog timeout trying to retrieve timezone info

## v2.0.2 (2025-09-20)

### What's Changed

* Bugfix: Date and time on web page now displayed in the time zone of the server (NTP server feature must be enabled).
* Bugfix: Crash log display was not showing stack dump, now fixed.
* New Feature: last door open and close date and time is displayed under opening/closing status (NTP server feature must be enabled).
* Other: Add a "home" button to system logs page because iOS and iPad OS 26 have removed the "done" button.

## v2.0.1 (2025-09-14)

### What's Changed

* Bugfix... Updating the ratgdo32 version from GitHub was not finding the firmware download file.
* Other... Add release notes to firmware update dialog.
* Other... Reduce unnecessary network traffic during firmware update

## v2.0.0 (2025-09-13)

**Version 2.0.0 is a major upgrade** for ESP8266-based ratgdo boards.  Almost all source files for the ESP8266 and ESP32 versions of ratgdo have been merged which results in significant changes to the underlying code base, features and function for ESP8266 versions.

While source files have been merged there remain significant differences between the two board types, most notably in the library used to communicate with HomeKit which are completely different.

* Before an Over-The-Air (OTA) upgrade it is good to first reboot your current version.

### What's Changed

* New feature... Door open & close duration reported on ratgdo web page
* New feature... Door cycle count reported on ratgdo web page (Sec+2.0 doors only)
* Bugfix... Expected impovement in overall performance, stability and reliability
* Other... Significant source code changes to support move towards single code base for ESP8266 and ESP32
* Other... Updates to Sec+ 1.0 door control to improve reliability.  With thanks to @mitchjs

### Known Issues

* If you upgrade to v2.0.0 and then later downgrade back to v1.9.1 or earlier, then Static IP address settings will be lost.

## v1.9.1 (2025-08-21)

### What's Changed

* Bugfix... Handle invalid packets on Sec+1.0 doors that was incorrectly causing door state change to be reported. With thanks to @mitchjs
* Bugfix... Make sure that a previous WiFi packet has finished sending before attempting to send another, fixes occasional crashes we were seeing.
* Other... Strategically add esp_yield() around HomeKit crypto functions to protect against WatchDog timeouts.

### Known Issues

* None

## v1.9.0 (2025-07-18)

### Major Release - Comprehensive Stability and Performance Overhaul

This major release represents a comprehensive rewrite of critical stability components with extensive testing infrastructure. All known crash conditions have been eliminated and performance has been dramatically improved.

### Critical Stability Fixes

* **Fixed 6 Critical Race Conditions** - Eliminated all identified critical failure modes that could cause system crashes, permanent hangs, or data corruption
* **Millis() Rollover Safety** - Fixed timing bugs that caused permanent system hangs every ~49.7 days of uptime
* **ESP8266 Alignment Crashes** - Fixed Exception 9 crashes by ensuring 4-byte alignment for multi-byte data structures
* **Buffer Overflow Prevention** - Fixed Exception 0 crashes from stack memory corruption with comprehensive bounds checking
* **Interrupt Safety** - Fixed race conditions in obstruction sensor ISR that caused false readings
* **Stack Overflow Protection** - Prevented crashes in dense WiFi environments through safe array sizing
* **Configuration Corruption** - Fixed race conditions in config writes that could corrupt settings during WiFi events
* **Rolling Code Protection** - Eliminated race conditions that could desynchronize door opener communication

### New Features

* **Comprehensive Testing Framework** - Added Unity-based test suite with 11 test categories covering all critical functionality
* **Smart Obstruction Detection** - Automatic fallback from pin-based to Pair3Resp packet-based detection when hardware sensor unavailable
* **Performance Monitoring** - Real-time web performance metrics exposed via JSON API (requests, cache hits, dropped connections, max response time)
* **Enhanced Security+ 1.0 Support** - Improved door state validation and reduced "Door State: unknown" occurrences
* **Memory Usage Tracking** - Comprehensive monitoring of both regular and IRAM heap usage
* **Static Analysis Integration** - cppcheck integration for continuous code quality monitoring
* **GitHub Actions CI/CD** - Added .github/workflows/test.yml and codeql.yml for automated testing and security scanning
* **Test Infrastructure** - Added run_tests.sh script and comprehensive test/README.md documentation
* **Performance Profiling** - Added test_performance.py for measuring real-world response times

### Performance Improvements

* **68% Faster Web Interface** - JSON caching reduces response times from 459ms to 146ms
* **277% More Free IRAM** - Optimized memory usage from 1.9KB to 7.3KB free
* **Connection Management** - Throttling, timeout protection, and resource leak prevention
* **WiFi Stability** - Stack overflow prevention in dense network environments (20 network limit)
* **Long-term Reliability** - Rollover-safe timing for continuous operation beyond 49 days

### Testing Infrastructure

* **Unity Test Framework** - 11 comprehensive test suites with 100% pass rate
* **Core Functionality Tests** - Rollover safety, validation logic, and ESP8266-specific behavior
* **Integration Tests** - HomeKit functionality and protocol communication
* **Performance Tests** - Memory usage, timing analysis, and resource monitoring
* **Hardware Simulation** - Door operation testing with mocked hardware interfaces
* **Web Interface Tests** - REST API endpoints and response validation with Python unittest
* **Static Analysis** - Automated code quality checks with cppcheck
* **Build Monitoring** - Size tracking and memory usage validation
* **CI/CD Integration** - Automated testing on all commits and pull requests
* **CodeQL Analysis** - Security and vulnerability scanning for C++, Python, and JavaScript
* **Test Documentation** - Comprehensive test/README.md with coverage goals and debugging guides
* **Mock Hardware Layer** - Simulates ESP8266 memory functions and Arduino framework for native testing

### Memory and Performance Optimizations

* **RAM Conservation** - Removed duplicate JSON buffer saving 1.3KB RAM for memory-constrained ESP8266
* **IRAM Optimization** - Strategic buffer placement providing 5.4KB additional memory headroom
* **Connection Throttling** - Max 4 concurrent connections with 5-second timeout protection
* **WiFi Stack Safety** - Limited network scanning to prevent overflow in dense environments
* **Safe String Operations** - Bounds-checked string concatenation preventing buffer overflows
* **Request Caching** - JSON response caching dramatically improves repeat request performance
* **Memory Monitoring** - Real-time tracking of heap fragmentation and usage patterns

### Technical Implementation Details

* **Struct Alignment** - Added `__attribute__((aligned(4)))` to PacketAction and ForceRecover structs preventing Exception 9 crashes
* **Rollover-Safe Arithmetic** - Replaced all direct millis() comparisons with rollover-safe subtraction patterns ((int32_t)(millis() - last_time) > timeout)
* **Interrupt Safety** - Protected pulse counter access with proper synchronization between ISR and main loop using noInterrupts()/interrupts()
* **Buffer Management** - Replaced Variable Length Arrays with fixed-size arrays for stack safety
* **String Safety** - Replaced unsafe `strcat` with bounds-checked `safe_strcat` wrapper functions
* **Type Safety** - Added proper format specifiers (ADD_LONG, ADD_TIME macros) eliminating compiler warnings
* **Memory Layout** - Strategic buffer allocation between IRAM and regular heap for optimal performance (LOG_BUFFER_SIZE in IRAM heap)
* **Connection Management** - Web server rate limiting, timeout handling, and resource leak prevention
* **Protocol Enhancement** - Pair3Resp packet parity detection (3=clear, 4=obstructed) for obstruction fallback
* **State Validation** - Enhanced Security+ 1.0 logic accepting valid states immediately while confirming suspicious values
* **Config Protection** - Atomic file writes with mutex protection preventing corruption during concurrent access
* **Config Write Optimization** - Added configChanged flag to prevent unnecessary flash writes on transient operations
* **Network Safety** - WiFi scanning limits (MAX_NETWORKS=20) and stack overflow protection in dense environments
* **Request Throttling** - ActiveRequest tracking with MAX_CONCURRENT_REQUESTS=4 and REQUEST_TIMEOUT_MS=5000
* **Diagnostic Logging** - Added comprehensive RINFO/RERROR logging for debugging race conditions and system state
* **Memory Allocation Safety** - Added malloc failure protection with automatic ESP.restart() in log.cpp and utilities.cpp
* **Atomic Configuration Writes** - Implemented temp file + rename pattern to prevent config corruption during power loss
* **Config File Validation** - Added malformed line detection and graceful handling in config parser
* **WiFi Connection Optimization** - Reduced connection delay (500ms→100ms) and added 10-second timeout protection
* **WiFi Stack Protection** - Dynamic memory allocation for network lists and MAX_NETWORKS=20 limit preventing overflow
* **Code Quality** - Removed trailing commas in HomeKit characteristic declarations for compiler compliance
* **LOG_BUFFER_SIZE Optimization** - Reduced from 8192 to 2048 bytes saving 6KB IRAM for critical services

### Issues Resolved

* **<https://github.com/ratgdo/homekit-ratgdo/issues/124>** - Obstruction sensor unreliable/always shows obstructed (fixed by automatic fallback to Pair3Resp packet-based detection)
* **<https://github.com/ratgdo/homekit-ratgdo/issues/132>** - Security+ 1.0 door state synchronization issues and frequent "Door State: unknown" (fixed by improved state validation logic)
* **<https://github.com/ratgdo/homekit-ratgdo/issues/218>/<https://github.com/ratgdo/homekit-ratgdo/issues/215>** - Memory-related crashes and HomeKit malloc failures (improved by IRAM optimization and connection management)
* **<https://github.com/ratgdo/homekit-ratgdo/issues/252>** - SEC+1.0 bootloop crashes due to IRAM heap exhaustion during HomeKit MDNS initialization (fixed by LOG_BUFFER_SIZE optimization)
* **<https://github.com/ratgdo/homekit-ratgdo/issues/261>** - Timing issues and bugs after millis() rollover (49+ day uptime)
* **<https://github.com/ratgdo/homekit-ratgdo/issues/266>** - Slow web interface performance and timeouts
* **<https://github.com/ratgdo/homekit-ratgdo/issues/267>** - Connectivity crashes, web interface timeouts, WiFi instability, Exception (0) crashes with ASCII in addresses
* **<https://github.com/ratgdo/homekit-ratgdo/issues/271>** - ESP8266 alignment crashes (Exception 9/LoadStoreAlignmentCause) due to unaligned struct access

### Known Issues

* None

## v1.8.3 (2024-11-23)

### What's Changed

* Bugfix... Unrecoverable crash/reboot loop for Sec+ reported in <https://github.com/ratgdo/homekit-ratgdo/issues/252>.

### Known Issues

* None

## v1.8.2 (2024-11-22)

### What's Changed

* New feature... Allow disabling of LED activity.
* New feature... Allow setting syslog server port.
* Bugfix... Passwords do not match message had been removed from html by mistake.

### Known Issues

* None

## v1.8.1 (2024-10-29)

### What's Changed

* New feature... Allow selection of time zone when NTP server enabled.
* Change... We use built in Arduino core NTP client for time instead of separate module
* Change... Replace DNS lookup with a ping to gateway to test for network connectivity
* Bugfix... Don't display that an update is available if running newer pre-release
* Bugfix... When changing SSID in soft access point mode, make sure the WiFi settings are reset to DHCP
* Bugfix... wifiSettingsChanged setting had been removed by mistake, impacting recovery from failure to connect.

### Known Issues

* Same as v1.8.0...
* Occasional failure to connect to WiFi. Tracked in <https://github.com/ratgdo/homekit-ratgdo/issues/217>

## v1.8.0 (2024-10-26)

This release has significant updates.  Please review the [README](https://github.com/ratgdo/homekit-ratgdo/blob/main/README.md) for full documentation of the new features.

### What's Changed

* New feature... add soft AP mode to allow setting WiFi SSID (<https://github.com/ratgdo/homekit-ratgdo/issues/224>).
* New feature... enable IRAM heap to increase available memory, expected to improve reliability.
* New feature... move all user config settings into single file, improves boot time by ~13 seconds (<https://github.com/ratgdo/homekit-ratgdo/issues/241>).
* New feature... add support for logging to a syslog server.
* Change... Reboot countdown timer changed from 30 seconds to 15 seconds
* Change... Remove option to receive server logs to JavaScript console, replaced by syslog and system logs page.
* Bugfix... ensure that network hostname is RFC952 compliant (e.g. no spaces).
* Bugfix... Possible fix to <https://github.com/ratgdo/homekit-ratgdo/issues/215> by modifying HomeKit server malloc() from local to global.
* Bugfix... Possible fix to <https://github.com/ratgdo/homekit-ratgdo/issues/211> and <https://github.com/ratgdo/homekit-ratgdo/issues/223> as we enable IRAM heap.
* Bugfix... Occasional HomeKit notification that garage door is unlocked. Tracked in <https://github.com/ratgdo/homekit-ratgdo/issues/233>

### Known Issues

* Occasional failure to connect to WiFi. Tracked in <https://github.com/ratgdo/homekit-ratgdo/issues/217>

## v1.7.1 (2024-09-23)

### What's Changed

* Bugfix... alignment of minHeap and minStack was wrong on chrome browsers.
* Bugfix... set WiFi hostname based on user provided device name.
* Bugfix... also set the browser page title to the user provided device name.
* New feature... allow user to set custom username for the webpage.
* New feature... allow user to set static IP address.
* New feature... add new system logs page (opens in new browser tab).
* New feature... add spinning page-loading icon to provide feedback on slow network links
* New feature... add option to obtain real time from NTP sever
* Cleanup... consolidated all code for retrieving user configured settings into one file (utilities.cpp).

### Known Issues

* Same as v1.7.0

## v1.7.0 (2024-08-10)

### What's Changed

* Removed the heap fragmentation tracking introduced in v1.6.1... it was occasionally crashing inside Arduino library.
* Bugfix... device reboot countdown timer from 30 seconds to zero was not always displaying.
* New feature... add _reset door_ button. This resets the Sec+ 2.0 rolling code and whether the door has a motion sensor or not.
* New feature... add option to trigger motion sensor on user pressing wall panel buttons (door open/close, light and/or lock).
* New feature... add option to trigger motion sensor on door obstruction.
* New feature... add option for user to select whether the LED light remains on while device is idle or turns off.  Any activity causes the LED to flash.

### Known Issues

* Random crashes inside the MDNSresponder code which usually occur shortly after booting or when something significant changes on the network that causes a storm of mDNS messages. Ratgdo always recovers. Tracked in <https://github.com/ratgdo/homekit-ratgdo/issues/211>, <https://github.com/ratgdo/homekit-ratgdo/issues/223>
* Random crashes inside the WiFi stack possibly associated with failing to connect to WiFi access point.  Tracked in <https://github.com/ratgdo/homekit-ratgdo/issues/215>, <https://github.com/ratgdo/homekit-ratgdo/issues/218>, <https://github.com/ratgdo/homekit-ratgdo/issues/223>
* Occasional failure to connect to WiFi. Tracked in <https://github.com/ratgdo/homekit-ratgdo/issues/217>

## v1.6.1 (2024-07-28)

### What's Changed

* Fixes to SEC1.0 when no DIGITAL wall panel connected by @mitchjs in <https://github.com/ratgdo/homekit-ratgdo/pull/208>
* Improve memory heap usage tracking and logging to assist with future debugging
* Cleaned up numerous compiler warnings

The only functional change in this release is to better support Sec+ 1.0 garage door openers when there is no digital wall panel.  Upgrading is therefore optional if your GDO is Sec+ 2.0.

## v1.6.0 (2024-06-17)

### What's Changed

* Documentation updates and test HomeKit server running by @dkerr64 in <https://github.com/ratgdo/homekit-ratgdo/pull/197>
* Fix a bug in HomeKit library was not not always correctly updating characteristics by @jgstroud in <https://github.com/ratgdo/homekit-ratgdo/pull/204> Thanks @hjdhjd for pointing me in the right direction
* Get status on boot by @jgstroud in <https://github.com/ratgdo/homekit-ratgdo/pull/205>
* Make obstruction detection ignore spurrious detections by @jgstroud in <https://github.com/ratgdo/homekit-ratgdo/pull/206>

## v1.5.0 (2024-06-03)

### What's Changed

* Firmware verification working branch by @dkerr64 in <https://github.com/ratgdo/homekit-ratgdo/pull/193>

Fixes several critical bugs.  Recommended for all users to upgrade.  @dak64 identified some storage issues in the homekit library and corrected them.  This should address #194 #189 #184

**NOTE: You will need to re-pair with HomeKit after installing this update.**
The most reliable way to do this is 1) erase from Apple Home, 2) reset/re-pair button on ratgdo, 3) kill Apple Home and restart it and 4) scan the QR code.

## v1.4.0 (2024-05-30)

### What's Changed

* Fixes / features for next release. by @dkerr64 and @jgstroud in <https://github.com/ratgdo/homekit-ratgdo/pull/191>
* Remove old redundant code
* Save logs on clean shutdown
* Check flash CRC on upload complete
* Add a full flash verify on upload to make sure contents are written properly to flash
* Change wifi persist to false.  was writing wifi settings to flash multiple times on each boot.  known to cause issues.  #192

Lots of changes mainly focuses on trying to prevent flash corruptions.

## v1.4.0 (2024-05-21)

### What's Changed

* Fixes and features for next release by @dkerr64 in <https://github.com/ratgdo/homekit-ratgdo/pull/175>
  * Back out usage of secondary IRAM heap. This was causing some users to not be able to access the webUI.  Fixes #173
  * Change webUI structure for memory optimization
  * Add script to remotely monitor logs
  * Document CLI control in the README
* New prerelease check by @jgstroud in <https://github.com/ratgdo/homekit-ratgdo/pull/170>
* v1.3.5 in changeling by @donavanbecker in <https://github.com/ratgdo/homekit-ratgdo/pull/169>
* Wait before publishing release to discord by @donavanbecker in <https://github.com/ratgdo/homekit-ratgdo/pull/168>

## v1.3.5 (2024-05-01)

### What's Changed

* Create CHANGELOG.md by @donavanbecker in #165
* Add check for pre-releases in firmware update dialog by @dkerr64 in #166
* Remove the visibility check as it was causing issues. by @jgstroud in #167

## Hotfix

* New flash wear protection was causing problems with rolling code getting out of sync on some GDOs. Fix implemented
* OTA Flash CRC check was causing false failures and blocking OTA upgrade. Removed this check. MD5 check still in place.

Full Changelog: [v1.3.2...v1.3.5](https://github.com/ratgdo/homekit-ratgdo/compare/v1.3.2...v1.3.5)

## v1.3.2 (2024-04-30)

### What's Changed

* Suspend certain activity when update underway, including comms and ho… by [@dkerr64](https://github.com/dkerr64) in [#153](https://github.com/ratgdo/homekit-ratgdo/pull/153)
* Time-to-close and further memory improvements by [@dkerr64](https://github.com/dkerr64) in [#145](https://github.com/ratgdo/homekit-ratgdo/pull/145)
* Include md5 in upload by [@donavanbecker](https://github.com/donavanbecker) in [#163](https://github.com/ratgdo/homekit-ratgdo/pull/163)
Fixes #60 Warning is now configurable in the setting page. User can set the duration of the delay before close. Lights will flash, but no beep. Controlling the beep is not yet possible.

Fixes [#150](https://github.com/ratgdo/homekit-ratgdo/issues/150) Allow user to set the WiFi transmit power. This combined with the ability to force 802.11g should fix [#77](https://github.com/ratgdo/homekit-ratgdo/issues/177)

Added lots of additional error checking and safeguards to address [#151](https://github.com/ratgdo/homekit-ratgdo/issues/151) Note, you won't see the benefits of these changes until the next update.

* More stability enhancements. Memory improvements to free up additional heap.
* Store serial log to flash on crash
* Allow logging over the network to the javascript console. No longer need a USB cable to capture the logs
Many thanks to [@dkerr64](https://github.com/dkerr64) for all the work on this release.

Full Changelog: [v1.2.1...v1.3.2](https://github.com/ratgdo/homekit-ratgdo/compare/v1.2.1...v1.3.2)

## v1.2.1 (2024-04-04)

### What's Changed

Hk update and ota fix by [@jgstroud](https://github.com/jgstroud) in #152

### Hotfix release

* Fixed incorrect reporting of garage door state
* A number of users have reported failed OTA updates requiring a USB reflash in [#151](https://github.com/ratgdo/homekit-ratgdo/pull/151)
  I believe this was introduced by: [ee38a90](https://github.com/ratgdo/homekit-ratgdo/commit/ee38a9048e339e88cef1df21138e34102e10bdae)
  
  Reverted those changes
  
  NOTE: since the OTA update failure is a result of the running code and not the incoming code, you may have to flash this release with a USB cable as well, but hopefully this will fix any future OTAs.

Full Changelog: [v1.2.0...v1.2.1](https://github.com/ratgdo/homekit-ratgdo/compare/v1.2.0...v1.2.1)

## v1.2.0 (2024-04-03)

### What's Changed

* Fix Github typo by @SShah7433 in [#137](https://github.com/ratgdo/homekit-ratgdo/pull/137)
* Add wifi RSSI to web page by [@jgstroud](https://github.com/jgstroud) in [#136](https://github.com/ratgdo/homekit-ratgdo/pull/136)
* Include Elf file by [@donavanbecker](https://github.com/donavanbecker) in [#142](https://github.com/ratgdo/homekit-ratgdo/pull/142)
* Improve web page stability by [@dkerr64](https://github.com/dkerr64) in [#139](https://github.com/ratgdo/homekit-ratgdo/pull/139)
* Store crash dumps to flash by [@dkerr64](https://github.com/dkerr64) and [@jgstroud](https://github.com/jgstroud)
* Changes to LwIP configuration to help prevent running out of memory when TCP connections die and when there are corrupt mDNS packets on the network by [@jgstroud](https://github.com/jgstroud) [#147](https://github.com/ratgdo/homekit-ratgdo/pull/147)
* Update to HomeKit library to reduce memory footprint by [@jgstroud](https://github.com/jgstroud) [#148](https://github.com/ratgdo/homekit-ratgdo/pull/148)

### New Contributors

* [@SShah7433](https://github.com/SShah7433) made their first contribution in [#137](https://github.com/ratgdo/homekit-ratgdo/pull/137)

Full Changelog: [v1.1.0...v1.2.0](https://github.com/ratgdo/homekit-ratgdo/compare/v1.1.0...v1.2.0)

## v1.1.0 (2024-03-25)

### What's Changed

* Improved pairing reliability by [@jgstroud](https://github.com/jgstroud) in [#135](https://github.com/ratgdo/homekit-ratgdo/pull/135)
* Allow setting WiFi phy mode to 802.11B/G/N or auto by [@dkerr64](https://github.com/dkerr64) in [#133](https://github.com/ratgdo/homekit-ratgdo/pull/133)

Some users with eero networks having connectivity issues have reported improved reliability by setting PHY mode to 802.11G

Full Changelog: [v1.0.0...v1.1.0](https://github.com/ratgdo/homekit-ratgdo/compare/v1.0.0...v1.1.0)

## v1.1.0 (2024-03-19)

### Release 1.0

We've focuses this release on stability and believe we are good to finally make an official 1.0 release.

Many thanks to @thenewwazoo for starting this project and to [@dkerr64](https://github.com/dkerr64) for his work on helping get this release out.

### What's Changed

Loads of stability improvements.

* Use our own HomeKit server. by [@dkerr64](https://github.com/dkerr64) in [#127](https://github.com/ratgdo/homekit-ratgdo/pull/127)
* Stability Improvements by [@jgstroud](https://github.com/jgstroud) in [#129](https://github.com/ratgdo/homekit-ratgdo/pull/129)

  Fixes [#15](https://github.com/ratgdo/homekit-ratgdo/issues/15), Fixes [#36](https://github.com/ratgdo/homekit-ratgdo/issues/36), Fixes [#94](https://github.com/ratgdo/homekit-ratgdo/issues/94), Fixes [#103](https://github.com/ratgdo/homekit-ratgdo/issues/103), Fixes [#126](https://github.com/ratgdo/homekit-ratgdo/issues/126), Fixes [#130](https://github.com/ratgdo/homekit-ratgdo/issues/130)
  
Full Changelog: [v0.12.0...v1.0.0](https://github.com/ratgdo/homekit-ratgdo/compare/v0.12.0...v1.0.0)

## v0.12.0 (2024-03-08)

### What's Changed

* Update Readme by [@donavanbecker](https://github.com/donavanbecker) in [#115](https://github.com/ratgdo/homekit-ratgdo/pull/115)
* Update README by @thenewwazoo in [#123](https://github.com/ratgdo/homekit-ratgdo/pull/123)
* Security 1.0 support, and web page updates by [@dkerr64](https://github.com/dkerr64) by @mitchjs in [#117](https://github.com/ratgdo/homekit-ratgdo/pull/117)
* Add an option to auto reboot every X number of hours

### New Contributors

* [@mitchjs](https://github.com/mitchjs) made their first contribution in [#117](https://github.com/ratgdo/homekit-ratgdo/pull/117)
Full Changelog: [v0.11.0...v0.12.0](https://github.com/ratgdo/homekit-ratgdo/compare/v0.11.0...v0.12.0)

## v0.11.0 (2024-02-06)

### What's Changed

* Add web authentication
* Automatically detect new releases on github
* Add door controls

Default login admin/password

* Discord Webhook after Release by [@donavanbecker](https://github.com/donavanbecker) in [#101](https://github.com/ratgdo/homekit-ratgdo/pull/101)
* Web page updates by [@dkerr64](https://github.com/dkerr64) in [#107](https://github.com/ratgdo/homekit-ratgdo/pull/107)
Full Changelog: [v0.10.0...v0.11.0](https://github.com/ratgdo/homekit-ratgdo/compare/v0.10.0...v0.11.0)

## v0.10.0 (2024-01-27)

### What's Changed

The main reason for this release is to make the motion sensor service visible to only those that have one. After installing this release, you may have to re-pair your device to HK. If you have a motion detector, it won't show up initially, but should show up shortly after triggering it for the first time. From this point on, it will always be visible even after upgrades.

* Update Release Process by [@donavanbecker](https://github.com/donavanbecker) in [#86](https://github.com/ratgdo/homekit-ratgdo/pull/86)
* make the motion sensor dynamic. by [@jgstroud](https://github.com/jgstroud) in [#85](https://github.com/ratgdo/homekit-ratgdo/pull/85)
* remove tag pattern check by [@donavanbecker](https://github.com/donavanbecker) in [396](https://github.com/ratgdo/homekit-ratgdo/pull/96)
Full Changelog: [v0.9.0...v0.10.0](https://github.com/ratgdo/homekit-ratgdo/compare/v0.9.0...v0.10.0)

## v0.9.0 (2024-01-07)

### What's Changed

* Add discord release notification by [@donavanbecker](https://github.com/donavanbecker) in [#83](https://github.com/ratgdo/homekit-ratgdo/pull/83)
* Add OTA to Readme by [@donavanbecker](https://github.com/donavanbecker) in [#84](https://github.com/ratgdo/homekit-ratgdo/pull/84)
* New webpage by [@dkerr64](https://github.com/dkerr64) in [#63](https://github.com/ratgdo/homekit-ratgdo/pull/63)
Full Changelog: [v0.8.0...v0.9.0](https://github.com/ratgdo/homekit-ratgdo/compare/v0.8.0...v0.9.0)

## v0.8.0 (2024-01-04)

### What's Changed

* Fix the millis timer veriable data types by [@jgstroud](https://github.com/jgstroud) in [#75](https://github.com/ratgdo/homekit-ratgdo/pull/75)
* Fix a few duplicate log messages by [@tabacco](https://github.com/tabacco) in [#73](https://github.com/ratgdo/homekit-ratgdo/pull/73)
* Pull in main branch of mrthiti's Homekit library to get mdns fix by [@jgstroud](https://github.com/jgstroud) in [#76](https://github.com/ratgdo/homekit-ratgdo/pull/76)
* Add HTTPUpdateServer OTA support by [@sstoiana](https://github.com/sstoiana) and [@donavanbecker](https://github.com/donavanbecker) in [#72](https://github.com/ratgdo/homekit-ratgdo/pull/72)
* Update Releasing Notes, & Enforce Version Pattern by [@donavanbecker](https://github.com/donavanbecker) in [#82](https://github.com/ratgdo/homekit-ratgdo/pull/82)
Full Changelog: [v0.7.0...v0.8.0](https://github.com/ratgdo/homekit-ratgdo/compare/v0.7.0...v0.8.0)

## v0.7.0 (2024-01-01)

### What's Changed

* Removes duplicate SSIDs from the wifi networks list, and shows only the one with the highest signal strength
* Slightly improves responsiveness at first startup and when controlling lights
* other minor improvements
Thank you to all the contributors and testers!

(no, you're not feeling deja-vu, this is a re-release in order to fix a new release process)
Full Changelog: [v0.6.0...v0.7.0](https://github.com/ratgdo/homekit-ratgdo/compare/v0.6.0...v0.7.0)

## v0.6.0 (2023-12-14)

### What's Changed

This release fixes garage door lock behavior and a crash on early setup, as well as adds support for directly sensing obstructions using the wired connection (versus relying on the garage door to report).

Once again, many thanks to @jgstroud for doing all the work in this release, and to the many users testing and reporting issues.
Full Changelog: [v0.5.0...v0.6.0](https://github.com/ratgdo/homekit-ratgdo/compare/v0.5.0...v0.6.0)

## v0.5.0 (2023-12-13)

### What's Changed

Thanks to @jgstroud for adding locks and light support to this release!
Full Changelog: [v0.4.0...v0.5.0](https://github.com/ratgdo/homekit-ratgdo/compare/v0.4.0...v0.5.0)

## v0.4.0 (2023-12-13)

### What's Changed

This release enables multiple HomeKit-native RATGDOs to co-exist.

NOTE: THIS IS A BREAKING RELEASE. You will need to re-pair your device after flashing.
Full Changelog: [v0.3.1...v0.4.0](https://github.com/ratgdo/homekit-ratgdo/compare/v0.3.1...v0.4.0)

## v0.3.1 (2023-12-12)

### What's Changed

This tweaks the motion sensor service notifications to save some redundant updates
Full Changelog: [v0.3.0...v0.3.1](https://github.com/ratgdo/homekit-ratgdo/compare/v0.3.0...v0.3.1)

## v0.3.0 (2023-12-12)

### What's Changed

New in this release is support for motion sensors.

Full Changelog: [v0.2.3...v0.3.0](https://github.com/ratgdo/homekit-ratgdo/compare/v0.2.3...v0.3.0)

## v0.2.3 (2023-12-8)

### What's Changed

* browser-based reboot button for the ratgdo device
* tweaks to wifi setup to improve reliability
* fixes to logic to make the Home UI more consistent

Full Changelog: [v0.2.0...v0.2.3](https://github.com/ratgdo/homekit-ratgdo/compare/v0.2.0...v0.2.3)

## v0.2.0 (2023-12-06)

### What's Changed

* Changes the pairing code to 2510-2023
* Adds a scannable QR code to ease setup
* Adds a web server that permits un-pairing with HomeKit and shows the QR code for pairing when not paired

Full Changelog: [v0.1.0...v0.2.0](https://github.com/ratgdo/homekit-ratgdo/compare/v0.1.0...v0.2.0)

## v0.1.0 (2023-12-03)

### What's Changed

* Improv support is still dicey (it will save the credentials but the device reboots and the page doesn't re-connect), but it will open and close a garage door pretty okay.

Full Changelog: [v0.1.0]([https://github.com/ratgdo/homekit-ratgdo/compare/v0.1.0...v0.2.0](https://github.com/ratgdo/homekit-ratgdo/commits/v0.1.0))
