# Fermentor Kopi Nextion dashboard

The current NX3224F024 interface is a three-page gauge dashboard with a shared
bottom navbar and a dedicated motor-control page.

It displays the five installed sensors (PT100, O2, pH, SCD41 and SHT30).
The SHT30 chamber readings appear on the environment page.

Use the complete build sheet and generated assets in
[`../hmi-display/README.md`](../hmi-display/README.md).

The firmware implements matching `DASH:PAGE:0`, `DASH:PAGE:1`, and
`DASH:PAGE:2` synchronization commands, native gauge updates, motor feedback,
slider synchronization, and the three-second keepalive safety timeout.
