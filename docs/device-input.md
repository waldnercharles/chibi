# Device input

The interactive player samples viewport size, touch capability, foreground
state and optional phone tilt into the same input timeline as controls and the
pointer ray. Authored code reads `get_device_state()`:

| Output | Meaning |
| --- | --- |
| `Width`, `Height` | Canvas dimensions in CSS pixels on the web, logical window pixels on desktop. These exclude browser safe-area padding and are independent of render pixel density. |
| `Touch` | The browser reports touch points or a coarse primary pointer. This does not imply motion access. |
| `Active` | The player is in the foreground. |
| `MotionStatus` | `0` disabled, `1` waiting, `2` ready, `3` unavailable, `4` denied. |
| `Tilt` | Calibrated gravity tilt in degrees, with positive X toward screen right and positive Y toward screen down. Zero while inactive or without ready motion. |
| `Haptics` | The host exposes a vibration API. It cannot confirm that physical vibration hardware is present or enabled. |

`request_device_motion()` asks the host to enable sensors. Where the browser
requires permission, the player displays an **Enable tilt** button. Its trusted
click calls `DeviceOrientationEvent.requestPermission()` directly. The authored
simulation tick does not stand in for that click. **Not now** dismisses
the request and reports denied. Browsers without that permission method begin
listening when requested. A finite orientation sample establishes readiness;
the presence of the browser API alone does not.

`recenter_device_motion()` makes the next valid reading neutral.
`disable_device_motion()` removes the listener, dismisses a pending permission
button and ignores any outstanding permission result. Hidden pages and lost
focus discard their sensor sample and stop listening. Foreground return,
screen rotation and a sensor stream stalled for more than 500 milliseconds
require a fresh neutral reading. A pending permission request is canceled when
the document is hidden. A listening device with no reading after 2.5 seconds
reports unavailable and can become ready if readings subsequently arrive.

Tilt uses the gravity direction derived from the orientation angles and rotates
that direction into the current screen axes before subtracting the neutral
reading. This avoids steering reversals caused by portrait/landscape changes
and the Euler-angle discontinuity near upright landscape. It measures lean,
not compass heading or yaw. Game rules own filtering, dead zones, repeat timing
and the actions assigned to lean.

`vibrate_device(milliseconds)` queues one pulse. Positive requests keep the
longest pulse until the host drains the queue. Values above 1000 are capped;
zero clears a queued pulse, so a later request starts a new pulse. Hidden or
inactive players do not submit pulses,
and the host cancels ongoing vibration on loss of focus. Unsupported hosts do
nothing. Native desktop currently reports unavailable motion and no haptics.
Browser vibration requires prior user interaction and remains subject to the
device's settings. There is no web amplitude control in this contract.

Recorded input and injected scenarios preserve the sampled device values.
Playback does not issue external motion or vibration requests. Device requests
never replace injected readings, so C/pico_unit scenarios can exercise authored
rules without a phone. Real permission prompts, physical sensor accuracy and
actuator feel still require a device check.

## Browser contracts

The [W3C orientation specification](https://www.w3.org/TR/orientation-event/)
defines the secure context, permission requirements and the device coordinate
frame, which does not rotate with the screen. The player requests relative
orientation and does not request magnetometer access.

The [W3C vibration specification](https://www.w3.org/TR/vibration/) gates pulses
on document visibility and user activation. Safari's
[open vibration implementation request](https://bugs.webkit.org/show_bug.cgi?id=288846)
means authored controls must remain usable without vibration. Runtime feature
detection handles browser differences; no user-agent list selects controls.
