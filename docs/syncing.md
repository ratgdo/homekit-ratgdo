The sync process
===

Every packet sent to the garage door opener includes a client ID and a rolling code. The client ID
does not (read: is not expected to) change over time and presumably uniquely identifies a device on
the bus. Each subsequent packet's rolling code must be higher than the last one sent, for the given
client ID.

When a newly-installed device on the bus wishes to register itself with the garage door opener, it
"handshakes" by sending two commands. The first packet is sent with its client ID and an arbitrary
rolling code. The second is sent with its client ID and the arbitrary rolling code plus one (other
values may also work, I have not tested it). The packets are not (afaik) important. The original
MQTT firmware sent a bunch of packets of various types. The ESPHome firmware sends two: GetStatus
and Openings, which the HomeKit firmware copies, mostly.

The important thing to know is that the first packet is *ignored*. Future packets get a response.

The "sync" process performed at startup is not actually required for an established client ID, but
if the rolling code is lost or corrupted there is no way to "reset" it. Since the sync itself is
simply "send some packets", we just do it at startup anyway. In this case (of an already-established
client ID), all sync packets get a response.

So why does this firmware send Openings and then GetStatus?

At program start the GarageDoor struct is initialized with zeroes. The zero value, to HomeKit, means
"garage door open", but the specific meaning isn't important, since there's a 50% chance it's wrong
anyway. Note that HomeKit has no value to signify "unknown" or "not yet". This means we need to get
a GetStatus response ASAP.

In the case of introducing a new client ID, the first packet is ignored. So why not send two
GetStatus commands? There is an untested-by-me claim that the garage door will ignore duplicate
packets sent within some indeterminate short period of time. We avoid this problem by sending an
Openings packet as the first. We need status information, so we send a GetOpenings packet next,
which garners a response.

The delay between the packets was basically just pulled out of thin air.
