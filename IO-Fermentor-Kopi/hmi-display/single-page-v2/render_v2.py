"""Render the revised single-page Fermentor Kopi Nextion assets (NX3224F024, 320x240).

Every asset is drawn 4x supersampled and downscaled with LANCZOS so edges stay
smooth on the 320x240 panel. Button and slider images are cropped from the same
full-screen render as the background, so their rounded corners blend exactly
with the pixels behind them (Nextion pictures have no alpha channel).

Usage:  py render_v2.py
Output: assets/*.png, preview_320x240.png, preview_1280x960.png, asset_sheet.png
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
ASSETS = HERE / "assets"
FONT_BOLD = "C:/Windows/Fonts/segoeuib.ttf"
SS = 4  # supersampling factor

W, H = 320, 240

# ---------------------------------------------------------------- palette
C = {
    "screen_top": "#0c2439", "screen_bot": "#06141f",
    "head_l": "#12334f", "head_r": "#0a2135",
    "accent": "#56d9d7",
    "title": "#f3f8fc", "subtitle": "#7fa9bf",
    "tile": "#10273a", "tile_edge": "#25475e",
    "well": "#0a1b2a", "well_edge": "#1c3a50",
    "label": "#9fb5c6", "label_dim": "#5f7e93",
    "value": "#f3f8fc",
    "motor_tile": "#0c3141", "motor_edge": "#2b7480",
    "panel": "#0f2537", "panel_edge": "#2a4d63",
    "groove": "#06131d", "groove_edge": "#2c5067", "tick": "#3f6880",
    "temp": "#ff9f5a", "o2": "#56d9d7", "ph": "#8edc6a", "co2": "#b98cff",
    "hum": "#5aa9ff", "press": "#f2c94c",
    "btn_text": "#062030",
    "motor_off": "#8ea3b6", "motor_run": "#4dd6b8",
}

BUTTONS = [  # name, picture ids, x, label, icon, top colour, bottom colour
    ("b0", "fwd", 11, "ON FWD", "fwd", "#5ee3c4", "#2fb393"),
    ("b1", "rev", 113, "ON REV", "rev", "#6eaee6", "#3f7fbf"),
    ("b2", "stop", 215, "OFF / STOP", "stop", "#f27a83", "#cc4752"),
]
BTN_Y, BTN_W, BTN_H = 179, 94, 30

SLIDER = (52, 214, 258, 20)  # x, y, w, h of h0
THUMB_W, THUMB_H = 14, 20

TILE_H = 20
TILES = [  # component, label, accent, x, y, w, note
    ("t0", "DS18B20", "temp", 6, 32, 151, None),
    ("t5", "OXYGEN", "o2", 163, 32, 151, None),
    ("t1", "pH", "ph", 6, 54, 151, None),
    ("t6", "CO2", "co2", 163, 54, 151, None),
    ("t2", "SCD41 T", "temp", 6, 76, 151, None),
    ("t7", "SCD41 RH", "hum", 163, 76, 151, None),
    ("t3", "SHT30 T", "temp", 6, 98, 151, None),
    ("t8", "SHT30 RH", "hum", 163, 98, 151, None),
    ("t4", "PRESSURE", "press", 6, 120, 308, "ADS1115 . A1"),
]
SAMPLE = {"t0": "25.42 C", "t5": "20.90 %", "t1": "6.85", "t6": "612 ppm",
          "t2": "25.31 C", "t7": "60.52 %", "t3": "24.98 C", "t8": "68.12 %",
          "t4": "0.842 bar", "t9": "OFF", "t10": "64/255 (25%)"}
WELL_X, WELL_W = 64, 83  # value well offset inside each tile and its width
MOTOR = {"y": 144, "h": 26, "t9": (60, 147, 108, 20), "t10": (210, 147, 100, 20)}


def rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def rgb565(h):
    r, g, b = rgb(h)
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


class Canvas:
    """Draws in 320x240 design units onto a supersampled image."""

    def __init__(self, w, h, scale=1):
        self.k = SS * scale
        self.scale = scale
        self.img = Image.new("RGB", (w * self.k, h * self.k))
        self.d = ImageDraw.Draw(self.img)

    def s(self, v):
        return round(v * self.k)

    def box(self, x, y, w, h):
        return (self.s(x), self.s(y), self.s(x + w) - 1, self.s(y + h) - 1)

    def vgrad(self, x, y, w, h, top, bot, r=0):
        bx = self.box(x, y, w, h)
        gw, gh = bx[2] - bx[0] + 1, bx[3] - bx[1] + 1
        col = Image.new("RGB", (1, 2))
        col.putpixel((0, 0), rgb(top))
        col.putpixel((0, 1), rgb(bot))
        grad = col.resize((gw, gh), Image.BILINEAR)
        mask = Image.new("L", (gw, gh), 0)
        ImageDraw.Draw(mask).rounded_rectangle((0, 0, gw - 1, gh - 1), self.s(r), fill=255)
        self.img.paste(grad, bx[:2], mask)

    def hgrad(self, x, y, w, h, left, right):
        bx = self.box(x, y, w, h)
        col = Image.new("RGB", (2, 1))
        col.putpixel((0, 0), rgb(left))
        col.putpixel((1, 0), rgb(right))
        self.img.paste(col.resize((bx[2] - bx[0] + 1, bx[3] - bx[1] + 1), Image.BILINEAR), bx[:2])

    def rrect(self, x, y, w, h, r, fill=None, outline=None, width=0.8):
        self.d.rounded_rectangle(self.box(x, y, w, h), self.s(r), fill=fill,
                                 outline=outline, width=max(1, self.s(width)) if outline else 0)

    def rect(self, x, y, w, h, fill):
        self.d.rectangle(self.box(x, y, w, h), fill=fill)

    def line(self, x0, y0, x1, y1, fill, width=1):
        self.d.line((self.s(x0), self.s(y0), self.s(x1), self.s(y1)), fill=fill, width=self.s(width))

    def poly(self, pts, fill):
        self.d.polygon([(self.s(x), self.s(y)) for x, y in pts], fill=fill)

    def text(self, x, y, s, size, fill, anchor="ls", spacing=0.0):
        font = ImageFont.truetype(FONT_BOLD, self.s(size))
        if not spacing:
            self.d.text((self.s(x), self.s(y)), s, font=font, fill=fill, anchor=anchor)
            return
        # manual letter spacing, left anchored only
        cx = self.s(x)
        for ch in s:
            self.d.text((cx, self.s(y)), ch, font=font, fill=fill, anchor="ls")
            cx += font.getlength(ch) + self.s(spacing)

    def done(self, w, h):
        return self.img.resize((w * self.scale, h * self.scale), Image.LANCZOS)


# ---------------------------------------------------------------- drawing
def draw_bean(cv, cx, cy):
    """Small coffee-bean logo: ellipse with a centre seam."""
    layer = Image.new("RGBA", (cv.s(14), cv.s(14)), (0, 0, 0, 0))
    ld = ImageDraw.Draw(layer)
    ld.ellipse((cv.s(2.5), cv.s(0.5), cv.s(11.5), cv.s(13.5)), fill=rgb(C["accent"]) + (255,))
    ld.arc((cv.s(4.5), cv.s(1), cv.s(10.5), cv.s(13)), 100, 260, fill=rgb(C["head_l"]) + (255,), width=cv.s(1.3))
    layer = layer.rotate(-35, resample=Image.BICUBIC)
    cv.img.paste(layer, (cv.s(cx - 7), cv.s(cy - 7)), layer)


def draw_button(cv, x, label, icon, top, bot, pressed):
    y = BTN_Y
    if pressed:
        cv.rrect(x, y, BTN_W, BTN_H, 5, fill=C["groove"])  # dark socket
        cv.vgrad(x + 1, y + 2, BTN_W - 2, BTN_H - 2, bot, bot, r=4)
        cv.rrect(x + 1, y + 2, BTN_W - 2, BTN_H - 2, 4, outline="#0a1a26", width=1)
        dy = 1.5
    else:
        cv.rrect(x, y + 1.5, BTN_W, BTN_H - 1.5, 5, fill="#030b12")  # drop shadow
        cv.vgrad(x, y, BTN_W, BTN_H - 1.5, top, bot, r=5)
        cv.line(x + 5, y + 0.7, x + BTN_W - 5, y + 0.7, "#ffffff", 0.6)  # top highlight
        dy = 0
    cx, cy = x + BTN_W / 2, y + BTN_H / 2 - 0.5 + dy
    font = ImageFont.truetype(FONT_BOLD, cv.s(11))
    tw = font.getlength(label) / cv.k
    gx = cx - (tw + 11) / 2  # icon(8) + gap(3) + text
    ink = C["btn_text"]
    if icon == "fwd":
        cv.poly([(gx, cy - 4), (gx + 7, cy), (gx, cy + 4)], ink)
    elif icon == "rev":
        cv.poly([(gx + 7, cy - 4), (gx, cy), (gx + 7, cy + 4)], ink)
    else:
        cv.rrect(gx, cy - 3.5, 7, 7, 1, fill=ink)
    cv.text(gx + 11, cy, label, 11, ink, anchor="lm")


def draw_slider_track(cv, x, y, w, h):
    cv.rect(x, y, w, h, C["panel"])
    gy = y + h / 2 - 2.5
    cv.rrect(x + 2, gy, w - 4, 5, 2.5, fill=C["groove"], outline=C["groove_edge"], width=0.7)
    for i in range(11):  # tick every 10 %, long ticks at 0/50/100
        tx = x + THUMB_W / 2 + i * (w - THUMB_W) / 10
        long = i in (0, 5, 10)
        cv.line(tx, y + (1 if long else 3), tx, gy - 1.5, C["tick"] if long else C["label_dim"], 0.8)
        cv.line(tx, gy + 6.5, tx, y + h - (1 if long else 3), C["tick"] if long else C["label_dim"], 0.8)


def draw_thumb(cv, x, y):
    cv.rect(x, y, THUMB_W, THUMB_H, C["panel"])
    cv.rrect(x, y + 0.5, THUMB_W, THUMB_H - 0.5, 3.5, fill="#030b12")
    cv.vgrad(x, y, THUMB_W, THUMB_H - 1, "#8af0ea", "#2fb3b0", r=3.5)
    cv.rrect(x, y, THUMB_W, THUMB_H - 1, 3.5, outline="#c9fbf6", width=0.6)
    for gx in (-2.5, 0, 2.5):  # grip lines
        cv.line(x + THUMB_W / 2 + gx, y + 6, x + THUMB_W / 2 + gx, y + THUMB_H - 7, "#0b5a5c", 0.9)


def draw_screen(cv, pressed=False, preview=False):
    cv.vgrad(0, 0, W, H, C["screen_top"], C["screen_bot"])
    # header
    cv.hgrad(0, 0, W, 26, C["head_l"], C["head_r"])
    cv.rect(0, 26, W, 2, C["accent"])
    draw_bean(cv, 14, 13)
    cv.text(25, 18, "FERMENTOR KOPI", 13.5, C["title"], spacing=0.5)
    cv.text(311, 17, "PROCESS CONTROL", 7.5, C["subtitle"], anchor="rs")

    # sensor tiles
    for comp, label, acc, x, y, w, note in TILES:
        cv.rrect(x + 0.5, y + 0.5, w - 1, TILE_H - 1, 3, fill=C["tile"], outline=C["tile_edge"], width=0.8)
        cv.rrect(x + 1, y + 2, 2.5, TILE_H - 4, 1.2, fill=C[acc])
        cv.text(x + 8, y + 13.5, label, 7.5, C["label"], spacing=0.3)
        cv.rrect(x + WELL_X, y + 2, WELL_W, 16, 2, fill=C["well"], outline=C["well_edge"], width=0.6)
        if note:
            cv.text(x + w - 8, y + 13.5, note, 7, C["label_dim"], anchor="rs")
        if preview:
            cv.text(x + WELL_X + WELL_W - 4, y + 10, SAMPLE[comp], 11, C["value"], anchor="rm")

    # motor strip
    my = MOTOR["y"]
    cv.rrect(6.5, my + 0.5, 307, MOTOR["h"] - 1, 3, fill=C["motor_tile"], outline=C["motor_edge"], width=0.8)
    cv.rrect(7, my + 2, 2.5, MOTOR["h"] - 4, 1.2, fill=C["accent"])
    cv.text(14, my + 16.5, "MOTOR", 7.5, C["label"], spacing=0.4)
    cv.text(180, my + 16.5, "PWM", 7.5, C["label"], spacing=0.4)
    cv.line(174, my + 5, 174, my + MOTOR["h"] - 5, "#326072", 1)
    for comp in ("t9", "t10"):
        x, y, w, h = MOTOR[comp]
        cv.rrect(x, y, w, h, 2.5, fill=C["well"], outline=C["well_edge"], width=0.6)
        if preview:
            colour = C["motor_off"] if comp == "t9" else C["value"]
            cv.text(x + 6, y + h / 2, SAMPLE[comp], 10.5, colour, anchor="lm")

    # control panel
    cv.rrect(6.5, 174.5, 307, 63, 4, fill=C["panel"], outline=C["panel_edge"], width=0.8)
    for _, _, x, label, icon, top, bot in BUTTONS:
        draw_button(cv, x, label, icon, top, bot, pressed)
    cv.text(13, 227.5, "SPEED", 7.5, C["label"], spacing=0.4)
    draw_slider_track(cv, *SLIDER)
    if preview:
        sx, sy, sw, _ = SLIDER
        draw_thumb(cv, sx + round(64 / 255 * (sw - THUMB_W)), sy)


def render_screen(scale=1, **kw):
    cv = Canvas(W, H, scale)
    draw_screen(cv, **kw)
    return cv.done(W, H)


def render_thumb():
    cv = Canvas(THUMB_W, THUMB_H)
    draw_thumb(cv, 0, 0)
    return cv.done(THUMB_W, THUMB_H)


def crop(img, x, y, w, h):
    return img.crop((x, y, x + w, y + h))


# ---------------------------------------------------------------- outputs
PICTURES = [  # id, file, size, used by
    (0, "bg_main.png", "320x240", "page0.pic (or p0.pic)"),
    (1, "btn_fwd_up.png", f"{BTN_W}x{BTN_H}", "b0.pic"),
    (2, "btn_fwd_dn.png", f"{BTN_W}x{BTN_H}", "b0.pic2"),
    (3, "btn_rev_up.png", f"{BTN_W}x{BTN_H}", "b1.pic"),
    (4, "btn_rev_dn.png", f"{BTN_W}x{BTN_H}", "b1.pic2"),
    (5, "btn_stop_up.png", f"{BTN_W}x{BTN_H}", "b2.pic"),
    (6, "btn_stop_dn.png", f"{BTN_W}x{BTN_H}", "b2.pic2"),
    (7, "slider_track.png", f"{SLIDER[2]}x{SLIDER[3]}", "h0.pic"),
    (8, "slider_thumb.png", f"{THUMB_W}x{THUMB_H}", "h0.pic2"),
]


def main():
    ASSETS.mkdir(exist_ok=True)
    bg = render_screen()
    down = render_screen(pressed=True)
    bg.save(ASSETS / "bg_main.png")
    for _, key, x, *_ in BUTTONS:
        crop(bg, x, BTN_Y, BTN_W, BTN_H).save(ASSETS / f"btn_{key}_up.png")
        crop(down, x, BTN_Y, BTN_W, BTN_H).save(ASSETS / f"btn_{key}_dn.png")
    crop(bg, *SLIDER).save(ASSETS / "slider_track.png")
    render_thumb().save(ASSETS / "slider_thumb.png")

    render_screen(preview=True).save(HERE / "preview_320x240.png")
    render_screen(scale=4, preview=True).save(HERE / "preview_1280x960.png")

    # asset sheet: every picture at 3x on a neutral board, labelled with its id
    files = [f for _, f, _, _ in PICTURES[1:]]
    tiles = [Image.open(ASSETS / f) for f in files]
    z = 3
    sheet = Image.new("RGB", (W * z + 40 + max(2 * (BTN_W * z + 40), SLIDER[2] * z + 20), H * z + 40), "#1b1f24")
    sheet.paste(bg.resize((W * z, H * z), Image.NEAREST), (20, 20))
    sd = ImageDraw.Draw(sheet)
    font = ImageFont.truetype(FONT_BOLD, 16)
    cx, cy = W * z + 40, 20
    for i, (f, im) in enumerate(zip(files, tiles)):
        col, row = i % 2, i // 2
        px, py = cx + col * (BTN_W * z + 40), cy + row * (BTN_H * z + 34)
        if f.startswith("slider_track"):
            px, py = cx, cy + 4 * (BTN_H * z + 34)
        if f.startswith("slider_thumb"):
            px, py = cx, cy + 4 * (BTN_H * z + 34) + SLIDER[3] * z + 34
        sheet.paste(im.resize((im.width * z, im.height * z), Image.NEAREST), (px, py + 18))
        sd.text((px, py), f"pic {i + 1}  {f}", font=font, fill="#d6dee6")
    sd.text((20, 0), "pic 0  bg_main.png", font=font, fill="#d6dee6")
    sheet.save(HERE / "asset_sheet.png")

    for comp, color in [("value", C["value"]), ("motor_off", C["motor_off"]), ("motor_run", C["motor_run"]),
                        ("label", C["label"]), ("panel", C["panel"]), ("well", C["well"])]:
        print(f"{comp:10s} {color}  565={rgb565(color)}")


if __name__ == "__main__":
    main()
