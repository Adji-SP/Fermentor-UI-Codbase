# Finished single-page Nextion dashboard

The current PlatformIO firmware matches page 0: t0-t10 (IDs 2-12),
b0/b1/b2 (IDs 13-15), and h0 (ID 16). Follow the shared
[single-page setup guide](../hmi-display/NEXTION_SINGLE_PAGE.md) for the complete
mapping, Editor settings, slider range and startup code.

src/hmi_motor.cpp handles field updates, motor controls and slider requests.
src/nextion_protocol.h validates native binary frames and earlier ASCII commands.
UART1 remains ESP TX GPIO15 / RX GPIO16 at 9600 baud.

Enable Send Component ID on button releases and both slider touch events.
The ESP32 polls sendme every second, so no Nextion timer is required.
The motor starts OFF, stops after about three seconds without page replies,
and pauses for 250 ms on reversal. Raising speed after STOP does not restart it.

Projects under restored/ retain their original interfaces. The current root
project uses the finished single page. The screenshot does not provide an
editable .HMI file; set the event checkboxes in the user's Nextion project.
