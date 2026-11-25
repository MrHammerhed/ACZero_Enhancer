"""
Generate crisp 4x-supersampled HD gamepad (PlayStation, Xbox), keyboard, and ACZ UI icons
into mods/enhancer/ui/assets/icons/{ps,xbox,kb,ui}/*.png.

All icons are 100% original procedural vector renders (CC0 / public domain) and can be
freely replaced by modders with any custom PNG pack (e.g. Kenney / Xelu CC0 prompts).
"""
import math
import os
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUT  = ROOT / "mods" / "enhancer" / "ui" / "assets" / "icons"
FONT_TITLE = ROOT / "mods" / "enhancer" / "fonts" / "EuroStyle Normal.ttf"
FONT_BODY  = ROOT / "mods" / "enhancer" / "fonts" / "ACZ_HUD_Font.ttf"

SS = 4  # 4x supersampling for razor-sharp anti-aliasing at 1080p & 4K


def load_font(size_px: int, bold_title: bool = True) -> ImageFont.FreeTypeFont:
    for p in ([FONT_TITLE, FONT_BODY] if bold_title else [FONT_BODY, FONT_TITLE]):
        if p.exists():
            try:
                return ImageFont.truetype(str(p), size_px)
            except Exception:
                pass
    return ImageFont.load_default()


def draw_centered_text(draw: ImageDraw.ImageDraw, cx: float, cy: float, text: str, font, fill):
    bbox = draw.textbbox((0, 0), text, font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    draw.text((cx - tw / 2 - bbox[0], cy - th / 2 - bbox[1]), text, font=font, fill=fill)


def save_ss(img: Image.Image, path: Path, final_w: int, final_h: int):
    path.parent.mkdir(parents=True, exist_ok=True)
    out = img.resize((final_w, final_h), Image.Resampling.LANCZOS)
    out.save(path, "PNG")


def make_round_button(size=128, rim=(190, 198, 210, 255), bg=(22, 24, 30, 242)):
    S = size * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    pad = int(6 * SS)
    # Subtle outer shadow
    d.ellipse([pad - 2*SS, pad, S - pad + 2*SS, S - pad + 4*SS], fill=(0, 0, 0, 120))
    # Main disc
    d.ellipse([pad, pad, S - pad, S - pad], fill=bg, outline=rim, width=int(5 * SS))
    # Inner bevel highlight
    p2 = int(13 * SS)
    d.arc([p2, p2, S - p2, S - p2], start=200, end=340, fill=(255, 255, 255, 55), width=int(3 * SS))
    return img, d, S


def make_pill_button(w=160, h=128, rim=(210, 215, 225, 255), bg=(24, 26, 34, 242), radius=26):
    W, H = w * SS, h * SS
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    px, py = int(8 * SS), int(18 * SS)
    d.rounded_rectangle([px, py + 3*SS, W - px, H - py + 3*SS], radius=radius * SS, fill=(0, 0, 0, 115))
    d.rounded_rectangle([px, py, W - px, H - py], radius=radius * SS, fill=bg, outline=rim, width=int(5 * SS))
    return img, d, W, H


def make_keycap(text: str, w=160, h=128, font_size=44):
    W, H = w * SS, h * SS
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    px, py = int(8 * SS), int(14 * SS)
    r = int(16 * SS)
    # Keycap base (3D bottom edge)
    d.rounded_rectangle([px, py + 6*SS, W - px, H - py + 4*SS], radius=r, fill=(14, 15, 20, 240), outline=(120, 100, 50, 200), width=int(3 * SS))
    # Keycap top face
    d.rounded_rectangle([px, py, W - px, H - py - 4*SS], radius=r, fill=(32, 30, 26, 245), outline=(235, 195, 70, 255), width=int(4 * SS))
    font = load_font(font_size * SS, bold_title=True)
    draw_centered_text(d, W / 2, (H - 4 * SS) / 2, text, font, (255, 244, 205, 255))
    return img


def make_dpad(mode="all", size=128):
    # mode: "all", "ud", "lr"
    S = size * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = S / 2
    arm_w = 18 * SS
    arm_l = 48 * SS
    bg = (28, 30, 38, 245)
    rim = (195, 200, 210, 240)
    hi  = (255, 215, 65, 255)

    # Cross body
    d.rounded_rectangle([c - arm_w, c - arm_l, c + arm_w, c + arm_l], radius=8*SS, fill=bg, outline=rim, width=int(4*SS))
    d.rounded_rectangle([c - arm_l, c - arm_w, c + arm_l, c + arm_w], radius=8*SS, fill=bg, outline=rim, width=int(4*SS))
    # Clean center fill to erase internal border overlap
    d.rectangle([c - arm_w + 3*SS, c - arm_w + 3*SS, c + arm_w - 3*SS, c + arm_w - 3*SS], fill=bg)

    # Highlighted direction triangles
    def tri(pts, active):
        col = hi if active else (130, 138, 150, 190)
        d.polygon(pts, fill=col)

    o = 26 * SS
    t = 12 * SS
    w = 9 * SS
    tri([(c, c - o - t), (c - w, c - o), (c + w, c - o)], mode in ("all", "ud"))  # Up
    tri([(c, c + o + t), (c - w, c + o), (c + w, c + o)], mode in ("all", "ud"))  # Down
    tri([(c - o - t, c), (c - o, c - w), (c - o, c + w)], mode in ("all", "lr"))  # Left
    tri([(c + o + t, c), (c + o, c - w), (c + o, c + w)], mode in ("all", "lr"))  # Right
    return img


def generate_ps():
    pdir = OUT / "ps"
    # Cross (blue)
    img, d, S = make_round_button(rim=(115, 165, 255, 255))
    m = 36 * SS
    d.line([(m, m), (S - m, S - m)], fill=(115, 175, 255, 255), width=int(10 * SS))
    d.line([(S - m, m), (m, S - m)], fill=(115, 175, 255, 255), width=int(10 * SS))
    save_ss(img, pdir / "cross.png", 128, 128)

    # Circle (red-coral)
    img, d, S = make_round_button(rim=(255, 115, 115, 255))
    m = 32 * SS
    d.ellipse([m, m, S - m, S - m], outline=(255, 110, 110, 255), width=int(10 * SS))
    save_ss(img, pdir / "circle.png", 128, 128)

    # Square (pink-magenta)
    img, d, S = make_round_button(rim=(245, 140, 215, 255))
    m = 34 * SS
    d.rectangle([m, m, S - m, S - m], outline=(245, 135, 215, 255), width=int(10 * SS))
    save_ss(img, pdir / "square.png", 128, 128)

    # Triangle (emerald-teal)
    img, d, S = make_round_button(rim=(95, 225, 175, 255))
    pts = [(S * 0.50, S * 0.24), (S * 0.76, S * 0.71), (S * 0.24, S * 0.71)]
    d.polygon(pts, outline=(95, 225, 175, 255))
    for i in range(3):
        d.line([pts[i], pts[(i + 1) % 3]], fill=(95, 225, 175, 255), width=int(10 * SS), joint="curve")
    save_ss(img, pdir / "triangle.png", 128, 128)

    # Shoulders & triggers
    for name, label in [("l1", "L1"), ("r1", "R1"), ("l2", "L2"), ("r2", "R2"), ("l3", "L3"), ("r3", "R3")]:
        img, d, W, H = make_pill_button(152, 128)
        draw_centered_text(d, W / 2, H / 2, label, load_font(52 * SS), (242, 246, 255, 255))
        save_ss(img, pdir / f"{name}.png", 152, 128)

    # Start / Select
    for name, label in [("start", "START"), ("select", "SELECT")]:
        img, d, W, H = make_pill_button(196, 128, radius=32)
        draw_centered_text(d, W / 2, H / 2, label, load_font(36 * SS), (242, 246, 255, 255))
        save_ss(img, pdir / f"{name}.png", 196, 128)

    # D-Pad variants
    for suffix, mode in [("dpad", "all"), ("dpad_ud", "ud"), ("dpad_lr", "lr")]:
        save_ss(make_dpad(mode), pdir / f"{suffix}.png", 128, 128)


def generate_xbox():
    xdir = OUT / "xbox"
    face = [
        ("a", "A", (95, 225, 110, 255)),
        ("b", "B", (255, 95, 95, 255)),
        ("x", "X", (95, 175, 255, 255)),
        ("y", "Y", (255, 215, 70, 255)),
    ]
    for name, letter, col in face:
        img, d, S = make_round_button(rim=col)
        draw_centered_text(d, S / 2, S / 2, letter, load_font(64 * SS), col)
        save_ss(img, xdir / f"{name}.png", 128, 128)

    for name, label in [("lb", "LB"), ("rb", "RB"), ("lt", "LT"), ("rt", "RT"), ("ls", "LS"), ("rs", "RS")]:
        img, d, W, H = make_pill_button(152, 128)
        draw_centered_text(d, W / 2, H / 2, label, load_font(50 * SS), (242, 246, 255, 255))
        save_ss(img, xdir / f"{name}.png", 152, 128)

    for name, label in [("start", "MENU"), ("back", "VIEW")]:
        img, d, W, H = make_pill_button(184, 128, radius=32)
        draw_centered_text(d, W / 2, H / 2, label, load_font(38 * SS), (242, 246, 255, 255))
        save_ss(img, xdir / f"{name}.png", 184, 128)

    for suffix, mode in [("dpad", "all"), ("dpad_ud", "ud"), ("dpad_lr", "lr")]:
        save_ss(make_dpad(mode), xdir / f"{suffix}.png", 128, 128)


def generate_kb():
    kdir = OUT / "kb"
    keys = [
        ("enter",     "ENTER", 196, 40),
        ("esc",       "ESC",   152, 44),
        ("space",     "SPACE", 210, 40),
        ("tab",       "TAB",   152, 44),
        ("shift",     "SHIFT", 196, 40),
        ("ctrl",      "CTRL",  176, 42),
        ("arrows",    "ARROWS",216, 36),
        ("arrows_ud", "UP/DN", 196, 38),
        ("arrows_lr", "LT/RT", 196, 38),
        ("wasd",      "WASD",  184, 40),
        ("q",         "Q",     128, 54),
        ("e",         "E",     128, 54),
        ("f1",        "F1",    136, 48),
        ("f5",        "F5",    136, 48),
    ]
    for name, label, w, fsz in keys:
        save_ss(make_keycap(label, w=w, h=128, font_size=fsz), kdir / f"{name}.png", w, 128)


def generate_ui():
    udir = OUT / "ui"
    # Authentic ACZ golden tactical folder icon
    W, H = 128 * SS, 104 * SS
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    gold = (255, 214, 58, 255)
    dark = (95, 62, 8, 255)
    # Folder back + tab
    d.polygon([
        (10*SS, 16*SS), (50*SS, 16*SS), (62*SS, 30*SS),
        (118*SS, 30*SS), (118*SS, 92*SS), (10*SS, 92*SS)
    ], fill=gold, outline=dark, width=int(4*SS))
    # Subtle inner crease line
    d.line([(16*SS, 40*SS), (112*SS, 40*SS)], fill=(170, 120, 16, 200), width=int(3*SS))
    save_ss(img, udir / "folder.png", 128, 104)

    # Crisp submenu right arrow
    S = 96 * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.polygon([(24*SS, 16*SS), (78*SS, 48*SS), (24*SS, 80*SS)], fill=gold)
    save_ss(img, udir / "arrow_right.png", 96, 96)


def main():
    generate_ps()
    generate_xbox()
    generate_kb()
    generate_ui()
    count = sum(1 for _ in OUT.rglob("*.png"))
    print(f"Generated {count} HD UI/button icons in {OUT}")


if __name__ == "__main__":
    main()
