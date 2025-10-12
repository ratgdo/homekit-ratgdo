> [!IMPORTANT]
> This firmware is for the orginal ESP8266-based RATGDO v2.5-series devices. It does not work with the ESP32-based ratgdo32 or ratgdo32 disco. HomeKit firmware for ratgdo32 & ratgdo32 disco can be found [here](https://github.com/ratgdo/homekit-ratgdo32).

> [!NOTE]
> **Version 2.0.0 is a major upgrade** for ESP8266-based ratgdo boards.  Almost all source files for the ESP8266 and ESP32 versions of ratgdo have been merged which results in significant changes to the underlying code base, features and function for ESP8266 versions.
>
>While source files have been merged there remain significant differences between the two board types, most notably in the library used to communicate with HomeKit which are completely different.
>
>* Before an Over-The-Air (OTA) upgrade it is good to first reboot your current version.

# What is HomeKit-RATGDO?

HomeKit-ratgdo is alternative firmware for the ratgdo-series WiFi control boards that works
over your _local network_ using HomeKit, or over the internet using your Apple HomeKit home hubs, to
control your garage door opener. It requires no supporting infrastructure such as Home Assistant,
Homebridge, MQTT, etc, and connects to your garage door opener with as few as three wires.

This firmware supports Security+, Security+ 2.0 and Dry Contact enabled garage door openers.

## What does this firmware support?

- Opening and closing garage doors independently in the same HomeKit home.
- Light Control and Status
- Obstruction sensor reporting with automatic fallback detection
- Motion sensor reporting, if you have a "smart" wall-mounted control panel or with the obstruction sensor
- Vehicle presence, arrival and departure sensing (ratgdo32-disco board only)
- Parking assist laser (ratgdo32-disco board only)

Check the [GitHub Issues](https://github.com/ratgdo/homekit-ratgdo/issues) for planned features, or to suggest your own.

For full history please see [CHANGELOG.md](https://github.com/ratgdo/homekit-ratgdo/blob/main/CHANGELOG.md)

### Known Issues

- Security+ 1.0 doors with digital wall panel (e.g. LiftMaster 889LM) sometimes do not close after a time-to-close delay. Please watch your door to make sure it closes after TTC delay.
- Security+ 1.0 doors with "0x37" digital wall panel (e.g. LiftMaster 398LM) not working. We detect but do not support them. Recommend replacing with 889LM panel.
- When creating automations in Apple Home the garage door may show only lock/unlock and not open/close as triggers. This is a bug in Apple Home. Workaround is to use the Eve App to create the automation, it will show both options.

## How do I install it?

Connect your ratgdo to a computer with a USB cable and use the [online browser-based flash tool](https://ratgdo.github.io/homekit-ratgdo/flash.html) to install the firmware.

> [!NOTE]
> The browser must be able to access a USB serial device. Some browsers block USB access. The Google Chrome browser is known to work and is recommended.

After flashing the firmware, the browser-based tool will give you the option to provision WiFi network and password, and following that the option to connect to the ratgdo from where you can configure settings.

If you skip this step, then you can set the network and password later using a laptop, phone or tablet.
Search for the WiFi network named _Garage Door ABCDEF_ where the last six characters are a unique identifier. Once connected use a browser to access http://192.168.4.1 and wait for it to display a list of WiFi networks to join.

After selecting the WiFi network the device will reboot and you can connect to it using a browser on your network at the address http://Garage-Door-ABCDEF.
Once the device is running you can change the name and add it to HomeKit by scanning the QR code.

The manual setup code is `2510-2023`.

> [!NOTE]
> If you experience very slow or poor connection accessing the ratgdo device web page then try moving the device further away from the garage door opener. Several users have reported that this can improve reliability of the connection. We do not know why this is the case but may suggest some RF interference between the door opener and the ratgdo device.

## HomeKit support

When you first add the device to HomeKit a number of accessories are added:

- HomeKit _bridge_ to which all other accessories are attached (ratgdo32 board only)
- _garage door_ with door state, obstruction detection and lock. Lock not available with Dry Contact protocol
- _light switch_. Not available with Dry Contact protocol
- _motion_ sensor. Automatically added for doors with wall panels that detect motion. Also can be optionally added and triggered by a button press on the wall panel and/or triggering the obstruction sensor.
- Vehicle arriving _motion_ sensor. Only on ratgdo32-disco boards, triggers motion if it detects arrival of a vehicle.
- Vehicle departing _motion_ sensor. Only on ratgdo32-disco boards, triggers motion if it detects departure of a vehicle.
- Vehicle presence _occupancy_ sensor. Only on ratgdo32-disco boards, set if the distance sensor detects presence of a vehicle.
- Parking assist laser _light switch_. Only on ratgdo32-disco boards.

Vehicle arrival and departing sensors are only triggered if vehicle motion is detected within 5 minutes of door opening or closing. The parking assist laser is activated when vehicle arrival is detected.

See below for instructions on setting the distance threshold for triggering vehicle presence.

For ratgdo32-disco boards, if you select _Identify_ during the add accessory process then the blue LED and Laser will light and the beeper sound for 2 seconds.

## Using ratgdo Webpage

Before pairing to HomeKit / Apple Home you should open up the ratgdo webpage and configure basic settings. Simply enter the local name or IP address of the ratgdo device to access the settings page to set a more appropriate device name and check that your door protocol is correct.

[![webpage](docs/webpage/webpage.png)](#webpage)

The webpage comprises three sections, HomeKit and ratgdo device status, garage door opener status, and an information and diagnostic section.

### HomeKit and ratgdo status

If your ratgdo device is not yet paired to HomeKit then a QR code is displayed for you to scan and add the garage door to HomeKit. If the device is already paired then this is replaced with a statement that you must first un-pair the device if you wish to use it with another HomeKit home. A Reset or Un-pair HomeKit button is provided for this.

> [!NOTE]
> If you will re-pair to the same HomeKit home you must also delete the accessory from HomeKit as well as un-pairing at the ratgdo device.

This section also displays the current firmware version, with a statement on whether an update is available, the uptime of the device in days/hours/minutes/seconds and WiFi connection status.

> [!NOTE]
> If the word _locked_ appears after the WiFi AP identifier then the ratgdo device will _only_ connect to that WiFi Access Point. See warning in the [Set WiFi SSID](#set-wifi-ssid) section below.

> [!NOTE]
> For ratgdo32 boards, a link local IPv6 address (LLA) will always be shown even if your network does not support IPv6. If your network does support IPv6 then Global Unicast Address (GUA) and/or Unique Local Address (ULA) will be displayed as well.

### Garage door opener status

Status of the garage door along with action buttons are shown in this section. The status values are updated in real time whether triggered by one of the action buttons or an external action (motion in the garage, someone using a door remote).

If you have a Security+ 1.0 door then you may see the word _(emulation)_ next to the door protocol. This indicates that you do not have a digital wall panel and the ratgdo is emulating this.

Next to the obstruction status you will see the word _(Pin-based)_ or _(Message)_ that indicates how ratgdo is detecting whether there is an obstruction that may prevent the door from closing. By default, ratgdo will attempt to use the pin-based method and only fall back to using the garage door status messages if no signal is detected on the obstruction sensor wire.  You can force ratgdo to use status messages, see below.

For Security+ 2.0 doors, the number of times the door has been opened and closed is shown as _Cycle Count_ and, if equipped, the status of the emergency backup battery.

_Opening_ and _Closing_ values represent the time it takes for the door to open or close. This is averaged over the last five door operations amd resets when the ratgdo is rebooted.

If the _Obtain time from NTP server_ option is selected then the last date and time of door opening/closing is displayed under the _opening_ and _closing_ durations in the time-zone selected on the settings page. These time stamps persist across device reboot, but may not display immediately after reboot as the current time must first be received from NTP server by the ratgdo.

For ratgdo32-disco boards, vehicle status is shown as _Away_, _Parked_, _Arriving_ or _Departing_. A distance value in centimeters is also shown that represents the distance between the ratgdo board and either the garage floor (if no vehicle present) or the roof/hood of the vehicle. It is normal for this value to fluctuate. See section below on setting vehicle distance threshold.

> [!NOTE]
> It is important that the distance sensor does not point at the glass windshield of your vehicle as this will give unreliable results.

### Information section

The final section provides useful links to documentation and legal/license information. At the very bottom of the page is diagnostic information, see [Troubleshooting](#troubleshooting) section below.

### Authentication

By default authentication is not required for any action on this web page. However it is strongly recommended that you enable the setting to require a password and change the default. If authentication is enabled then all buttons _except_ Reboot are protected with a username and password

#### Default Username/Password: `admin`/`password`

> [!NOTE]
> The device uses _Digest Authentication_ supported in all web browsers, this is not cryptographically secure but is sufficient to protect against unauthorized or inadvertent access. Note that web browsers remember the username and password for a period of time so you will not be prompted to authenticate for every access.

You can change the user name and password by clicking into the settings page:

[![settings](docs/webpage/settings.png)](#settings)

## Settings Webpage

[![password](docs/webpage/password.png)](#password)

The settings page allows you to input a new user name and password. Saving a new password will return you to the main webpage from which point you will have to authenticate with the new password to access the settings page or any of the action buttons (except for reboot).

When you save settings from this page the ratgdo device will either return immediately to the main page or, if required, reboot return to the main page and after a countdown.

### Name

This updates the name reported to HomeKit and for mDNS device discovery. The default name is _Garage Door ABCDEF_ where the last 6 characters are set to the MAC address of the ratgdo device. Changing the name after pairing with HomeKit does not change the name within HomeKit or Apple Home. The maximum length is 31 characters. For network hostname the length is truncated to 23 characters and all non alphanumeric characters are replaced with a hyphen to comply with RFC952

### Require Password

If selected then all the action buttons _except reboot_, and access to the settings page, will require authentication. Default is not required.

### LED activity

If selected _On when idle_, then the blue LED light on the ratgdo device will remain illuminated when the device is idle and flash off when there is activity. If you prefer the LED to remain off while idle then select _Off when idle_ and the LED to flash on with activity. During normal operation the LED will flash once every 5 seconds. You can also _Disable_ the blue LED. Note that the ratgdo32-disco boards also have a red power LED light. This cannot be controlled and is always on when power is connected.

### Syslog

This setting allows you to send ratgdo logs to a syslog server. When selected, enter the IP address and port of your syslog server (default UDP port 514) and select the Syslog facility number (default Level0).

> [!NOTE]
> If your ratgdo is on an IoT VLAN or otherwise isolated VLAN, then you need to make sure it has access to your syslog server. If the syslog server is on a separate VLAN, you need to allow UDP port 514 through the firewall.

### Log Level

You can select the verbosity of log messages from none to verbose. Default is _Info_ level. If you are diagnosing a problem then you should change this to _Debug_ level.

### HomeSpan

On ratgdo32 boards we use an external library, [HomeSpan](https://github.com/HomeSpan/HomeSpan), for all HomeKit operations. When debugging using the serial port it may be helpful to enable HomeSpan's message logging and Command Line Interface (CLI). If you enable this setting then Improv-based WiFi provisioning is disabled unless the ratgdo32 boots into SoftAP mode.

### Door Close Delay

You can select up-to 60 second delay before door starts closing. During the delay period the garage door lights will flash and you may hear the relay clicking. On ratgdo32-disco boards you will also hear audible beeping. Default time-to-close delay is 5 seconds. See WARNING below.

### Flash light during time-to-close

You can disable the garage door opener light from flashing during the delay period. This has the benefit of reducing traffic on the serial communications to the door opener. However note the WARNING below.

> [!WARNING]
> The US Consumer Product Safety Act Regulations, section 1211.14, unattended operation requirements [16 CFR § 1211.14](https://www.law.cornell.edu/cfr/text/16/1211.14), state that there must be a minimum delay of 5 seconds before the door closes.
> The regulation also requires that the light flash with audible beeping. If you select a time-to-close delay of under 5 seconds, or disable light flashing, then a warning is shown in the web settings page and you accept all responsibility and liability for injury or any other loss.

### Motion Triggers

This allows you to select what causes the HomeKit motion sensor accessory to trigger. The default is to use the motion sensor built into the garage door opener, if it exists. This checkbox is not selectable because presence of the motion sensor is detected automatically... based on detecting motion in the garage. If your door opener does not have a motion sensor then the checkbox will show as un-checked.

Motion can also be triggered by the obstruction sensor. This is disabled by default but may be selected on the web page.

### Occupancy Duration _(not supported on ratgdo v2.5 boards)_

For ratgdo32 boards, a HomeKit occupancy sensor may be set active when motion is detected. It will remain active for a user set duration between zero and 120 minutes after the last motion detected. HomeKit accessory is disabled if set to zero. Disabled by default.

### Vehicle Distance _(ratgdo32-disco boards only)_

Ratgdo32-disco boards detect vehicle arrival, departure and presence based on distance measured from the ratgdo board. You should monitor the value
reported in vehicle distance when there is no vehicle present (the approximate distance to the floor) and when the vehicle is parked (the approximate
distance to the vehicle roof or hood). Set the vehicle distance slider to between these these two values. Measured distance can fluctuate so allow
for this when setting the value.

> [!IMPORTANT]
> Take care when installing the ratgdo32-disco board that the sensor is not pointing at glass (e.g. your vehicle windshield). You may need tilt the board
> to point towards the vehicle roof. If you get unreliable results, you should also remove the yellow sticker that may be on the sensor to protect
> it from dust during manufacture and shipping.

### Laser _(ratgdo32-disco boards only)_

For ratgdo32-disco boards, if you have the parking assist laser accessory installed, select this option to enable support and optionally add HomeKit light switch accessory
to allow for manual or HomeKit automation control.

When enabled, you can configure how long the laser remains on during parking assist by selecting a value from zero to 300 seconds (5 minutes). Selecting
zero disables parking assist laser. Parking assist is triggered if an arriving vehicle is detected with 5 minutes of the door opening or closing.

### Door Protocol

Set the protocol for your model of garage door opener. This defaults to Security+ 2.0 and you should only change this if necessary. Note that the changing the door protocol also resets the door opener rolling codes and whether there is a motion sensor (this will be automatically detected after reset).

### Debounce duration

For Dry Contact door protocol, set the sensor debounce duration. When the door opens or closes it can take time for the sensor switch to settle into its correct state. You can set this between 50 and 1000 milliseconds. You can observe the log messages when door reaches closed or open state to get an idea of what the correct setting should be for your door.

### Enable hardwired open/close control

For Security+ 1.0 and Security +2.0 it is possible to repurpose the sensors used for dry-contact door open / close to buttons that trigger a door open / close action. Select this check box to enable this option.

### Use software serial emulation rather than h/w UART _(ratgdo32 boards only)_

For Security+ 1.0 and Security+ 2.0, communications with the garage door uses a serial port. This can be either the ESP32's built-in hardware UART or a software emulation of a UART. Software emulation is required for Security+ 2.0 to allow for automatic sensing and changing of the baud rate... without this, the ratgdo cannot detect button presses at the garage door wall panel controller. For Security+ 1.0, software emulation is optional. If you experience communication errors you can try changing to use hardware UART.

> [!NOTE]
> This selection will only be available if the ratgdo firmware was built with the external GDOLIB library.

### Get obstruction from GDO status messages

By default ratgdo obtains obstruction state by monitoring signals on the sensor wire. The door also reports status in messages sent by the GDO and ratgdo will fall back to use status messages if no signal is detected on the wire. Selecting this option will force the ratgdo to always use status messages. Note that the messages method can be slow to report a change in obstruction state.

### WiFi Version _(not supported on ratgdo32 boards)_

If the device fails to connect reliably and consistently to your WiFi network it may help to lock it to a specific WiFi version. The ratgdo supports 802.11b, 802.11g and 802.11n on the 2.4GHz WiFi band and by default will auto-select. If it helps in your network, select the specific version you wish to use.

### WiFi Tx Power

You can set the WiFi transmit power to between 0 and 20 dBm. It defaults to the maximum (20.5 dBm, displayed as 20 dBm) but you may wish to fine tune this to control how the device connects to available WiFi access points.

> [!NOTE]
> If the ratgdo device fails to connect to a WiFi access point by approximately 40 seconds after a reboot from a setting change, then the device will reset all the WiFi settings and will then attempt to reconnect to the server.

### Enable IPv6 _(ratgdo32 boards only)_

If your network supports IPv6 then selecting this will allow ratgdo to acquire IPv6 addresses and communicate over IPv6.

### Static IPv4

If selected then you can enter a static IP address, network mask, gateway IP and DNS server IP. Note that if the address changes, then after reboot the web page will not automatically reload from the new IP address... you will need to manually connect to the ratgdo device at its new address. **Most users should NOT select static IP** as it is far safer to use DHCP to automatically configure the device.

Ratgdo tests for network connection approximately 30 seconds after a reboot from a WiFi setting change. If this fails then the device will disable static IP and will attempt to reconnect to the network using DHCP.

> [!NOTE]
> You must provide an IP address for DNS (Domain Name Service), this is required to set the clock on ratgdo with Network Time Protocol (NTP). Most household gateway routers act as DNS servers so you can use the same IP address as the gateway.

> [!WARNING]
> If you enter an IP address that is not valid on your network then you will not be able to access the device by web browser or from HomeKit. If the device fails to automatically recover by resetting to DHCP, then you can recover by using the soft AP mode described below in [Set WiFi SSID](#set-wifi-ssid) (press light button 5 times). If this also fails then you will need to flash the ratgdo over the USB port using the web installer AND select the option to erase the device. This will factory reset the device and you will have to remove the garage door accessory from HomeKit and then add it again.

### Enable NTP

The ratgdo device will obtain current time from an NTP (Network Time Protocol) server. This requires internet access and the default setting is disabled.

When enabled, the _lastDoorChange_ date and time (reported at the bottom of the web page) is saved across reboots and the actual time of log data is reported when viewed in the browser system logs page.

### Time Zone

You can select time zone for your location. On fist boot, or after changing the WiFi network SSID, the ratgdo will reset to GMT/UTC and then attempt to obtain your time zone by geo-locating the external IP address of the network that ratgdo is connected to. The discovered time zone will take effect immediately after you view the ratgdo web page.

### Reboot Every

During early development there were several reports that the ratgdo device would reset itself and loose its pairing with HomeKit. To reduce the chance of this occurring a regular (e.g. daily) reboot of the device provided a work-around. The firmware is far more stable now and it is hoped that this is no longer required. This setting may be removed in future versions.

### Reset Door

This button resets the Security+ 2.0 rolling codes and whether your door opener has a motion sensor. This may be necessary if the ratgdo device gets out-of-sync with what the door opener expects. Selecting this button requires the ratgdo to reboot and does not save any new settings.

### Set WiFi SSID

This button enters the WiFi network selection page, similar to booting into Soft AP mode described below, but without the reboot. If you _cancel_ from this page then you return to the main ratgdo page without a reboot or saving any changes. Selecting _submit_ will save changes and reboot the device as described below.

### Boot Soft AP

This button will restart the ratgdo in soft Access Point (AP) mode from where you can set a new WiFi network SSID and password. You can connect to the ratgdo by connecting your laptop or mobile device to the ratgdo's WiFi SSID and pointing your browser to IP address 192.168.4.1. The SSID created is based on the device name, e.g. _Garage-Door-ABCDEF_.

If you are unable to connect to your ratgdo, or the old WiFi network is not available, you can force the device into soft Access Point mode by rapidly pressing the wall panel light button 5 times within 3 second. The ratgdo will respond by flashing the lights for 3 more seconds before rebooting into AP mode. Allow 15 to 20 seconds for the ratgdo to boot.

If you are preparing to move the ratgdo to a new location then, after the device has booted into soft AP mode, you can disconnect it (within 10 minutes). When it first boots in the new location it will start up in soft AP mode. In soft AP mode the ratgdo does not connect to the garage door opener or HomeKit.

On changing the WiFi network SSID you will have to un-pair and re-pair the ratgdo to Apple Home.

> [!NOTE]
> If you do not set a new SSID and password within 10 minutes of booting into soft AP mode then the ratgdo will reboot as normal and connect to the previously set WiFi network SSID.

> [!NOTE]
> On changing the SSID in soft AP mode the ratgdo attempts to connect to the new WiFi network. If that fails, then the ratgdo will reset to the old SSID and password, remove any Access Point lock, and reboot.

> [!WARNING]
> The soft AP page has an _advanced_ mode. If you select this then the full list of discovered networks are shown, including same network SSID's broadcast by multiple access points. Selecting a WiFi network in advanced mode locks the device to a specific WiFi access point by its **unique hardware BSSID**. If that access point goes offline, or you replace it, then the device will **NOT connect** to WiFi. **Use advanced mode with extreme caution**. **Not implemented on ratgdo32 boards.**

### Factory Reset

This button erases all saved settings, including WiFi, HomeKit unique IDs. The device is reset to as-new state and must be reconfigured. HomeKit will no longer recognize the device and consider it all new.

## How do I upgrade?

Over-the-Air (OTA) updates are supported, either directly from GitHub or by selecting a firmware binary file on your computer. Follow the steps below to update:

- Navigate to your ratgdo's ip address where you will see the devices webpage, Click `Firmware Update`

[![ota](docs/ota/ota.png)](#ota)

- Update from Github
  - To check for updates, click `Check for update`.
    Select the _Include pre-releases_ box to include pre- or beta-release versions in the check.
  - If update is available, Click `Update`.
- Update from local file.
  - Download the latest release, by download the `.bin` file from the [latest release](https://github.com/ratgdo/homekit-ratgdo/releases) page.
    [![firmware](docs/ota/firmware.png)](#firmware)
  - Upload the firmware that was downloaded in step 1, by clicking `Choose File` under `Update from local file`.
  - Click `Update` to proceed with upgrading.
  - Once the update is Successful, ratgdo will now Reboot.
  - After a firmware update, you _may_ have to go through the process of re-pairing your device to HomeKit. If your device is showing up as unresponsive in HomeKit, please try un-pairing, reboot, and re-pairing.

Automatic updates are not supported (and probably will never be), so set a reminder to check back again in the future.

## Upgrade failures

If the OTA firmware update fails the following message will be displayed and you are given the option to reboot or cancel. If you reboot, the device will reload the same firmware as previously installed. If you cancel then the device remains open, but the HomeKit service will be shutdown. This may be helpful for debugging, see [Troubleshooting](#troubleshooting) section below.

[![updatefail](docs/ota/updatefail.png)](#updatefail)

### Esptool

[Espressif](https://www.espressif.com) publishes [esptool](https://docs.espressif.com/projects/esptool/en/latest/esp8266/index.html), a command line utility built with python. Esptool requires that you connect a USB serial cable to the ratgdo device. If you are able to install and run this tool then the following commands may be useful...

```
sudo python3 -m esptool -b 115200 -p <serial_device> verify_flash --diff yes 0x0000 <firmware.bin>
```
The above command verifies that the flash memory on the ratgdo has an exact image of the contents of the provided firmware binary file. If a flash CRC error was reported then this command will fail and list out the memory locations that are corrupt. This information will be useful to the developers to assist with debugging. Please copy it into the GitHub issue.

```
sudo python3 -m esptool -b 115200 -p <serial_device> write_flash 0x0000 <firmware.bin>
```
The above command will upload a new firmware binary file to the ratgdo device which will recover from the flash CRC error. This is an alternative to using the [online browser-based flash tool](https://ratgdo.github.io/homekit-ratgdo/flash.html) described above.

Replace _<serial_device>_ with the identifier of the USB serial port. On Apple MacOS this will be something like _/dev/cu.usbserial-10_ and on Linux will be like _/dev/ttyUSB0_. You should not use a baud rate higher than 115200 as that may introduce serial communication errrors.

## Command Line Interface

It is possible to query status, monitor and reboot/reset the ratgdo device from a command line. The following have been tested on Ubuntu Linux and Apple macOS.

### Retrieve ratgdo status

```
curl -s http://<ip-address>/status.json
```

Status is returned as JSON formatted text.

### Set a ratgdo setting value

```
curl -X POST http://<ip-address>/setgdo \
   -H "Content-Type: application/x-www-form-urlencoded" \
   -d "<setting>=<value>&<setting2>=<value2>"
```

Set one of more values for ratgdo settings. Note that some settings require a reboot, the web page settings page identifies which require a reboot. For a full list of settings see the [status.json](https://github.com/ratgdo/homekit-ratgdo/blob/main/src/www/status.json) file. You can also see the settings object in a browser JavaScript Console.

### Reboot ratgdo device

```
curl -s -X POST http://<ip-address>/reboot
```

Allow 30 seconds for the device to reboot before attempting to reconnect.

### Reset ratgdo device

```
curl -s -X POST http://<ip-address>/reset
```

Resets and reboots the device. This will delete HomeKit pairing.

> [!NOTE]
> Will not work if device set to require authentication

### Show last crash log

```
curl -s http://<ip-address>/crashlog
```

Returns details of the last crash including stack trace and the message log leading up to the crash.

### Clear crash log

```
curl -s http://<ip-address>/clearcrashlog
```

Erase contents of the crash log.

### Show message log

```
curl -s http://<ip-address>/showlog
```

Returns recent history of message logs.

### Show last reboot log

```
curl -s http://<ip-address>/showrebootlog
```

Returns log of messages that immediately preceded the last clean reboot (e.g. not a crash or power failure).

> [!NOTE]
> This may be older than the most recent crash log.

### Monitor message log

The following script is available in this repository as `viewlog.sh`

```
UUID=$(uuidgen)
URL=$(curl -s "http://${1}/rest/events/subscribe?id=${UUID}&log")
curl -s "http://${1}/showlog"
curl -s -N "http://${1}${URL}?id=${UUID}" | sed -u -n '/event: logger/{n;p;}' | cut -c 7-
```

Run this script as `<path>/viewlog.sh <ip-address>`

Displays recent history of message log and remains connected to the device. Log messages are displayed as they occur.
Use Ctrl-C keystroke to terminate and return to command line prompt. You will need to download this script file from github.

### Upload new firmware

> [!WARNING]
> This should be used with extreme caution, updating from USB serial or web browser is strongly preferred. Before using this script you should check that embedded commands like `md5` or `md5sum` and `stat` work correctly on your system. Monitoring the message log (see above) is recommended and no other browser should be connected to the ratgdo device.

```
<path>/upload_firmware.sh <ip-address> <firmware_file.bin>
```

Uploads a new firmware binary file to the device and reboots. It can take some time to complete the upload, you can monitor progress using the `viewlog.sh` script in a separate command line window. You will need to download this script file from github.

> [!NOTE]
> Will not work if device set to require authentication

## Help! aka the FAQs

### How can I tell if the ratgdo is paired to HomeKit?

Connect a browser to your device at its IP address or host name (e.g. http://Garage-Door-ABCDEF). If you see a big QR code, the ratgdo is _not_ paired.

### I added my garage door in the Home app but can't find it

This is a common problem. Be sure to check all of the "rooms" in the Home app. If you really can't
find it, you can try un-pairing and re-pairing the device, paying close attention to the room you
select after adding it.

### Unable to Pair

I get a message [Unable to Add Accessory: The setup code is incorrect.](https://github.com/ratgdo/homekit-ratgdo/issues/97)

> [!WARNING]
> We have had a number of users that have encountered this error that was a result of running HomeBridge with the Bonjour-HAP mDNS backend. You can find
> more details in the issue thread, but the short story is to consider changing that backend to Avahi or Ciao.

### How do I re-pair my ratgdo?

From the ratgdo device home page click the _Reset HomeKit_ or _Un-pair HomeKit_ button, and then delete the garage door from within the HomeKit app (or vice versa, order does not matter). Resetting or Un-pairing HomeKit will cause the ratgdo device to reboot. You can then re-pair the device by adding it again as normal.

### Where can I get help?

If your question has not been answered here, you can try the Discord chat.

Click [this link](https://discord.gg/homebridge-432663330281226270) to follow an invite to the
server. Server rules require a 10 minute wait after sign-up.

Now that you've signed up, go here to join the discussion:

[![the Discord logo](docs/discord-logo.png)](https://discord.com/channels/432663330281226270/1184710180563329115).

Please also feel free to open a [GitHub Issue](https://github.com/ratgdo/homekit-ratgdo/issues) if
you don't already see your concern listed. Don't forget to check the [closed
issues](https://github.com/ratgdo/homekit-ratgdo/issues?q=is%3Aissue+is%3Aclosed) to see if someone
has already found a fix.

## Troubleshooting

Great reliability improvements have been made in recent versions of the firmware, but it is possible that things can still go wrong. As noted above you should check that the door protocol is correctly set and if WiFi connection stability is suspected then, on ratgdo v2.5 devices, you select a specific WiFi version.

The footer of the webpage displays useful information that can help project contributors assist with diagnosing a problem. The ratgdo is a low-memory device so monitoring actual memory usage is first place to start. Whenever you connect to the webpage, the firmware reports memory utilization... current available free heap, the lowest value that free heap has reached since last reboot.

In addition the last reboot date and time is reported (calculated by subtracting up-time from current time).

The _lastDoorChange_ will show the date and time that the door was last opened or closed.

### Show system logs

Clicking on the system logs link will open a new browser tab with details of current and saved logs. On this page you can select to view the current system log, the current system status in raw JSON format, the system log immediately before the last user requested reboot or reset, and the system log immediately before the last crash. If you open an issue on GitHub then please copy/paste the full crash log into the issue.

> [!NOTE]
> For ratgdo32 devices, logs from the last reboot and crash are saved in volatile memory and do not survive a power interruption or a firmware flash.
> If you need to retain these logs to report an issue please make a copy before disconnecting the power or updating firmware.

### Serial port log

If you are unable to connect to the ratgdo web page then it is possible to capture logs from the USB serial port.  If you installed homekit-ratgdo using the [online browser-based flash tool](https://ratgdo.github.io/homekit-ratgdo/flash.html), you can use this same tool to display serial logs which you can then copy and use in a bug report.

### Serial port command line interface

There is a basic Command Line Interface (CLI) available through the serial port where basic commands like reboot, show logs and factory reset are available. For a full list of commands enter ? (question mark) followed by the enter/return key. If you are running on ratgdo32 then a more extensive CLI is available from the HomeSpan library... you can switch between the ratgdo CLI and the HomeSpan CLI as documented in the command list.

## How can I contribute?

HomeKit-RATGDO uses [PlatformIO](https://platformio.org/platformio-ide) for builds. You'll want to install PlatformIO first.

Check out this repo:

```
git clone --recurse-submodules  https://github.com/ratgdo/homekit-ratgdo.git
```

The [`x.sh`](https://github.com/ratgdo/homekit-ratgdo/blob/main/x.sh) script is my lazy way of not
having to remember PlatformIO-specific `pio` commands. The important ones are `run`, `upload`, and
`monitor`.

## Acknowledgements & Credits.

The original ESP8266 HomeKit firmware was written by [Brandon Matthews](https://github.com/thenewwazoo). The ESP32 firmware was written by, and ongoing maintenance for both versions is provided by, [David Kerr](https://github.com/dkerr64). We are thankful for lots of help from contributors:

- [Donavan Becker](https://github.com/donavanbecker)
- [Adam Franke](https://github.com/frankea)
- [Mike La Spina](https://github.com/descipher)
- [Mitchell Solomon](https://github.com/mitchjs)
- [Jonathan Stroud](https://github.com/jgstroud/)

We also acknowledge inspiration from [esphome-ratgdo](https://github.com/ratgdo/esphome-ratgdo) written by [Paul Wieland](https://github.com/PaulWieland) and dependence on the [secplus decoder library](https://github.com/argilo/secplus).

Special credit goes to the Chamberlain Group, without whose decision to [close their API to third parties](https://chamberlaingroup.com/press/a-message-about-our-decision-to-prevent-unauthorized-usage-of-myq), this firmware would never have been necessary.

[Garage icons](https://www.flaticon.com/free-icons/garage) created by Creative Squad - Flaticon

Copyright (c) 2023-25 HomeKit-ratgdo [contributors](https://github.com/ratgdo/homekit-ratgdo/graphs/contributors).
