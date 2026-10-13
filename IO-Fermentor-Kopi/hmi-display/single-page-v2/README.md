# Fermentor Kopi — single-page HMI, revision 2

Target: **Nextion NX3224F024**, 320 × 240 landscape. This revision keeps the
component names from the original single-page layout (`p0`, `t0`–`t10`,
`b0`–`b2`, `h0`), so existing event scripts carry over.

![preview](preview_1280x960.png)

What changed from revision 1:

- Each sensor tile has a color-coded accent: orange = temperature, teal = O2,
  green = pH, violet = CO2, blue = humidity, yellow = pressure.
- Every value sits in a dark, right-aligned "well". The well is baked into the
  background, so the Text component only crops it.
- The buttons are images with an icon, a 3D raised state and a pressed state.
- The slider is image-based: a grooved track with 10 % ticks and a teal thumb
  with grip lines.
- The header has a coffee-bean logo. The pressure tile carries an `ADS1115 . A1`
  source tag.

Regenerate every PNG with `py render_v2.py` (needs Pillow and Segoe UI Bold).
[asset_sheet.png](asset_sheet.png) shows all pictures with their IDs.

## Picture resources

Import the pictures in this order so the IDs match the tables below.

| ID | File | Size | Used by |
| --- | --- | --- | --- |
| 0 | [`bg_main.png`](assets/bg_main.png) | 320×240 | `page0.pic`. Every Crop Image component also uses it as `picc` |
| 1 | [`btn_fwd_up.png`](assets/btn_fwd_up.png) | 94×30 | `b0.pic` |
| 2 | [`btn_fwd_dn.png`](assets/btn_fwd_dn.png) | 94×30 | `b0.pic2` |
| 3 | [`btn_rev_up.png`](assets/btn_rev_up.png) | 94×30 | `b1.pic` |
| 4 | [`btn_rev_dn.png`](assets/btn_rev_dn.png) | 94×30 | `b1.pic2` |
| 5 | [`btn_stop_up.png`](assets/btn_stop_up.png) | 94×30 | `b2.pic` |
| 6 | [`btn_stop_dn.png`](assets/btn_stop_dn.png) | 94×30 | `b2.pic2` |
| 7 | [`slider_track.png`](assets/slider_track.png) | 258×20 | `h0.pic` (slider background) |
| 8 | [`slider_thumb.png`](assets/slider_thumb.png) | 14×20 | `h0.pic2` (slider cursor) |

Nextion pictures have no transparency. The button and track images were
cropped from the same render as `bg_main.png`, so their rounded corners already
contain the correct background pixels. **Place them at the exact x,y below.**
If they move, the corners stop matching.

## Fonts

| ID | Font | Height | Used by |
| --- | --- | --- | --- |
| 0 | Arial Bold (or Segoe UI Bold), anti-aliased | 16 | `t0`–`t8` values |
| 1 | Same face | 16 | `t9`, `t10` |

Button labels are baked into the images. No button font is needed.

## Page

| Item | Setting |
| --- | --- |
| `page0` | `sta=image`, `pic=0` |
| `p0` | Optional. It is not needed when the page uses `pic=0`. If kept: Picture, `0,0,320,240`, `pic=0` |

Use the page background rather than a full-screen `p0` when possible. A
picture component on top can briefly cover other components during redraws.

## Style list per component

All Text components use `sta=crop image` and `picc=0`, so they show the well
from the background. Set `txt_maxl=16` on these components.

### Sensor values (`t0`–`t8`)

Shared style: `font=0`, `pco=63455` (#F3F8FC), `xcen=2` (right),
`ycen=1` (center), `isbr=0`, `spax=0`.

| Name | Tile | x,y,w,h | Tile accent |
| --- | --- | --- | --- |
| `t0` | DS18B20 | `70,34,83,16` | temperature `#FF9F5A` |
| `t1` | pH | `70,56,83,16` | pH `#8EDC6A` |
| `t2` | SCD41 T | `70,78,83,16` | temperature `#FF9F5A` |
| `t3` | SHT30 T | `70,100,83,16` | temperature `#FF9F5A` |
| `t4` | PRESSURE | `70,122,83,16` | pressure `#F2C94C` |
| `t5` | OXYGEN | `227,34,83,16` | O2 `#56D9D7` |
| `t6` | CO2 | `227,56,83,16` | CO2 `#B98CFF` |
| `t7` | SCD41 RH | `227,78,83,16` | humidity `#5AA9FF` |
| `t8` | SHT30 RH | `227,100,83,16` | humidity `#5AA9FF` |

Status colors that firmware can write to `tX.pco`: normal `63455`, WAIT
`62922`, OFFLINE `64715`, fault `60238`.

### Motor strip

| Name | x,y,w,h | Font | Style |
| --- | --- | --- | --- |
| `t9` (motor state) | `60,147,108,20` | 1 | `xcen=0` (left), `ycen=1`, `pco=36118` when OFF, `pco=20151` when running, `txt="OFF"` |
| `t10` (PWM) | `210,147,100,20` | 1 | `xcen=0`, `ycen=1`, `pco=63455`, `txt="64/255 (25%)"` |

### Buttons (`b0`–`b2`)

Shared style: `sta=image`, `txt=""` (the label is in the image), and
"Send Component ID" turned off. Use the same Touch Release scripts as before.

| Name | x,y,w,h | `pic` (up) | `pic2` (pressed) | Release event |
| --- | --- | --- | --- | --- |
| `b0` ON FWD | `11,179,94,30` | 1 | 2 | `prints "MOTOR:FWD",0` / `printh 0A` |
| `b1` ON REV | `113,179,94,30` | 3 | 4 | `prints "MOTOR:REV",0` / `printh 0A` |
| `b2` OFF / STOP | `215,179,94,30` | 5 | 6 | `prints "MOTOR:STOP",0` / `printh 0A` |

### Slider (`h0`)

| Attribute | Value |
| --- | --- |
| x,y,w,h | `52,214,258,20` |
| `mode` | horizontal |
| `sta` | image, `pic=7` (track) |
| `psta` | image, `pic2=8` (thumb). The cursor size is taken from the image (14×20) |
| `minval` / `maxval` / `val` | `0` / `255` / `64` |

Slider events are unchanged:

```text
cov h0.val,tSpeed.txt,0
prints "MOTOR:SPEED:",0
prints tSpeed.txt,0
printh 0A
```

In Nextion Editor, the cursor image field appears only after `psta=image` is
selected. If your Editor version labels it differently (`pic1`), select image 8
in whichever cursor-picture field it shows.

The track ticks sit at `x = 7 + n × 24.4` for n = 0 to 10. These are the
thumb-center positions for 0 %, 10 %, … 100 %. Because the track is a static
image, the region left of the thumb is not filled in. This avoids layering a
Progress Bar under the slider, which flickers on Nextion.

## Palette (hex → RGB565)

| Role | Hex | RGB565 |
| --- | --- | --- |
| Value text | `#F3F8FC` | 63455 |
| Label text (baked) | `#9FB5C6` | 40376 |
| Motor OFF | `#8EA3B6` | 36118 |
| Motor running | `#4DD6B8` | 20151 |
| Well fill (baked) | `#0A1B2A` | 2245 |
| Control panel (baked) | `#0F2537` | 2342 |
| Accent | `#56D9D7` | 22234 |

Nothing here has been compiled into a `.HMI` or `.tft`, and it has not been
tested on hardware.
