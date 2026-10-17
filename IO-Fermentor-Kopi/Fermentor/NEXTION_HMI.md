# Finished single-page Nextion dashboard

The current Rust firmware matches page 0: t0-t10 (IDs 2-12), b0/b1/b2
(IDs 13-15), and h0 (ID 16). Follow the shared
[single-page setup guide](../hmi-display/NEXTION_SINGLE_PAGE.md) for component
mapping, Editor settings and startup code. The automatic-mode behavior below
overrides that guide's older manual direction/slider behavior for Rust only.

UART1 remains ESP TX GPIO15 / RX GPIO16, 9600 baud. DS18B20 stays on GPIO12;
I2C stays SDA8/SCL9. Sensor calibration is unchanged.

src/nextion.rs publishes all eleven text fields and polls sendme every second.
src/nextion_wire.rs decodes native touch, page, numeric and startup frames.
The mixer runs automatically, forward, for two minutes every six hours
(start-to-start), with its first scheduled start 30 seconds after startup.
PWM is hardcoded to the existing example's 64/255, **not measured 3 RPM**.
See [automatic mixer configuration](README.md#automatic-mixer-schedule).

- b0 / ID 13: firmware changes its label to RESUME. It rearms a paused schedule
  with a fresh 30-second delay; repeated presses do not extend a run.
- b1 / ID 14: label becomes LOCKED and touch is disabled. Reverse is ignored.
- b2 / ID 15: STOP remains enabled; it immediately stops and pauses all future
  scheduled runs until RESUME. It is accepted even during handshake recovery.
- h0 / ID 16: touch is disabled and the slider shows the fixed PWM value.
- t9 shows AUTO WAIT / AUTO FWD / AUTO PAUSED; t10 still shows PWM, not RPM.

The React website also shows this firmware state and can STOP or RESUME the
same schedule over USB. No extra Nextion components or events are needed.
Website RESUME cannot bypass the page-0 connection requirement. See
[website control setup](../../Monitor_Framework_ALBON/MIXER_CONTROL.md).

Page 0 must be connected for automatic motion. Losing page replies for about
three seconds, changing pages or restarting an already-connected display
stops and pauses the schedule. Reconnecting does not automatically resume it.
The initial Nextion power-on message before its first handshake does not
cancel the normal 30-second startup schedule. Rebooting the ESP32 restarts
the schedule from scratch, including after an operator STOP.

Enable Send Component ID on button releases and both slider touch events.
No heartbeat timer or hidden text component needs to be added.
The user's Program.s initialization can remain unchanged.

The O2 field t5 shows `WARMUP mm:ss` during its three-minute startup window
only while no valid reading has arrived. Valid O2 values replace that text
immediately, even before the timer ends. Disconnected devices remain OFFLINE;
invalid readings after the window (or after an earlier valid sample) are ERROR.
No extra component, HMI timer or Editor event is required. All other sensor
fields continue showing their own readings/readiness states independently.

The older three-page assets/recovered projects retain historical interfaces.
The finished .HMI project was provided as a screenshot and component list, so
the required Editor checkboxes must still be set in the user's project.
