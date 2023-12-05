# What is HomeKit-RATGDO?

HomeKit-RATGDO is an alternative firmware for the RATGDO v2.5 and v2.5i WiFi control boards that
works over your _local network_ using HomeKit, or over the internet using iCloud, to control your
garage door opener. It requires no supporting infrastructure such as Home Assistant, Homebridge,
MQTT, etc, and connects to your garage door opener with three wires.

This firmware supports only Security+ 2.0-enabled garage door openers.

> [!IMPORTANT]
> This is a work-in-progress implementation that is ready for early-alpha testing only. I am pretty
> sure it won't leave your garage open to thieves and light your cat on fire, but it might.

## Current task list

- [x] stub support for garage door HAP service
- [x] wire up comms and homekit
- [x] enable Improv and esp web tools-based configuration
- [x] working security+ 2.0 communications
- [ ] add web configuration endpoint, incl homekit reset and homekit QR code display
- [ ] add support for light HAP service
- [ ] add support for motion detection HAP service
- [ ] extend/generalize in order to support ESP32-based devices

## Hardware Support

Support for the following devices is implemented, or underway:

- [x] v2.5 RATGDO with D1 Mini Lite (default ratgdo configuration)

Support for the following devices is planned:

- [ ] v2.5 RATGDO with ESP32 D1 Mini
- [ ] v2.0 RATGDO with D1 Mini Lite (legacy configuration)
- [ ] v2.0 RATGDO with ESP32 D1 Mini

## Features

The following features are implemented, or underway:

- [x] Garage door support (open/close/stopped)

The following features are planned:

- [ ] Light support
- [ ] Motion detection support
- [ ] Obstruction support
