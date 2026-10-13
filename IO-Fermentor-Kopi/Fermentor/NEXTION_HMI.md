# Finished single-page Nextion dashboard

The current Rust firmware matches page 0: t0-t10 (IDs 2-12), b0/b1/b2
(IDs 13-15), and h0 (ID 16). Follow the shared
[single-page setup guide](../hmi-display/NEXTION_SINGLE_PAGE.md) for the complete
mapping, Editor settings and startup code.

UART1 remains ESP TX GPIO15 / RX GPIO16, 9600 baud. DS18B20 stays on GPIO12;
I2C stays SDA8/SCL9. Sensor calibration is unchanged.

src/nextion.rs publishes all eleven text fields and polls sendme every second.
src/nextion_wire.rs decodes native touch, page, numeric and startup frames.
The slider is read on release. The motor retains its 0-255 speed range,
OFF-at-boot state, 250 ms reversal pause and three-second connection timeout.

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
