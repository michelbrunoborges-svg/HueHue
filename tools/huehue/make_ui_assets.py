"""Gera as imagens da interface do NX Zero – BR HueHue Edition em dist/nxbox/Assets/ui.

    python3 tools/huehue/make_ui_assets.py [pasta-de-saida]

Estilo grafite: parede escura, spray verde-neon e magenta, letreiro com tinta escorrendo. Os
textos grandes (letreiro, itens do menu inicial, títulos das telas) viram imagens porque o Xbox só
tem Segoe UI; o resto da interface continua em texto normal (traduzível). Cada rótulo sai em
português (_pt) e inglês (_en).

Precisa de Pillow. Fontes em tools/huehue/fonts (licenças lá). A cara do HueHue vem de
tools/huehue/huehue-face.png. As posições dos botões do controle (CONTROLLER_SPOTS) são as mesmas
que src/eden_uwp/ui/library_screen.cpp usa no teste de controles: mude as duas juntas.
"""
import math
import random
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "dist/uwp/Assets/ui"
FONTS = HERE / "fonts"
WET = str(FONTS / "RubikWetPaint-Regular.ttf")
MARKER = str(FONTS / "PermanentMarker-Regular.ttf")
BANGERS = str(FONTS / "Bangers-Regular.ttf")
FACE = Image.open(HERE / "huehue-face.png").convert("RGBA")

NEON = (57, 255, 20)
NEON_DEEP = (22, 163, 74)
MAGENTA = (255, 43, 214)
WHITE = (255, 255, 255)
WALL = (10, 10, 15)
BR_GREEN, BR_YELLOW, BR_BLUE = (0, 156, 59), (255, 223, 0), (0, 39, 118)


def rgba(color, alpha=255):
    return tuple(color[:3]) + (alpha,)


def glow(mask: Image.Image, color, radius: int, strength: float = 1.0) -> Image.Image:
    """Uma camada colorida que é a máscara borrada (o brilho de neon em volta de algo)."""
    blurred = mask.filter(ImageFilter.GaussianBlur(radius))
    if strength != 1.0:
        blurred = blurred.point(lambda v: min(255, int(v * strength)))
    layer = Image.new("RGBA", mask.size, rgba(color))
    layer.putalpha(blurred)
    return layer


def dilate(mask: Image.Image, px: int) -> Image.Image:
    return mask.filter(ImageFilter.MaxFilter(px * 2 + 1)) if px > 0 else mask


def text_mask(text: str, font: ImageFont.FreeTypeFont, pad: int) -> Image.Image:
    probe = ImageDraw.Draw(Image.new("L", (1, 1)))
    left, top, right, bottom = probe.textbbox((0, 0), text, font=font)
    mask = Image.new("L", (right - left + pad * 2, bottom - top + pad * 2), 0)
    ImageDraw.Draw(mask).text((pad - left, pad - top), text, font=font, fill=255)
    return mask


def crop_alpha(im: Image.Image, margin: int = 4) -> Image.Image:
    box = im.getchannel("A").getbbox()
    if not box:
        return im
    l, t, r, b = box
    return im.crop((max(0, l - margin), max(0, t - margin), min(im.width, r + margin),
                    min(im.height, b + margin)))


def spray(im: Image.Image, rng: random.Random, cx, cy, radius, color, alpha, dots=260, drips=5):
    """Uma mancha de spray: névoa borrada, respingos em volta e algumas gotas escorrendo."""
    w, h = im.size
    mist = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mist).ellipse((cx - radius, cy - radius * 0.7, cx + radius, cy + radius * 0.7),
                                 fill=alpha)
    im.alpha_composite(glow(mist, color, int(radius * 0.45)))
    d = ImageDraw.Draw(im)
    for _ in range(dots):
        a = rng.uniform(0, math.tau)
        dist = radius * (0.35 + rng.random() ** 0.6 * 0.95)
        x, y = cx + math.cos(a) * dist, cy + math.sin(a) * dist * 0.75
        r = rng.choice([1, 1, 1.5, 2, 2, 3, 4]) * (1.6 if rng.random() < 0.08 else 1)
        d.ellipse((x - r, y - r, x + r, y + r), fill=rgba(color, rng.randint(70, 200)))
    for _ in range(drips):
        x = cx + rng.uniform(-radius * 0.6, radius * 0.6)
        y0 = cy + rng.uniform(-radius * 0.1, radius * 0.3)
        length = rng.uniform(radius * 0.12, radius * 0.45)
        width = rng.uniform(2.5, 6)
        for i in range(int(length)):
            t = i / length
            ww = width * (1 - t * 0.55)
            d.ellipse((x - ww / 2, y0 + i - ww / 2, x + ww / 2, y0 + i + ww / 2),
                      fill=rgba(color, int(160 * (1 - t * 0.3))))
        d.ellipse((x - width * 0.8, y0 + length - width * 0.8, x + width * 0.8,
                   y0 + length + width * 0.8), fill=rgba(color, 170))


def rough_mask(mask: Image.Image, rng: random.Random, amount=0.8) -> Image.Image:
    """Borda de spray: a máscara borrada e cortada por ruído, sem linha reta."""
    noise = Image.effect_noise(mask.size, 90).filter(ImageFilter.GaussianBlur(1.2))
    soft = mask.filter(ImageFilter.GaussianBlur(3))
    return ImageChops.multiply(soft, noise.point(lambda v: 255 if v > 255 * (1 - amount) else v))


def brazil_flag(im: Image.Image, rng: random.Random, box, alpha):
    """A bandeira em spray, torta e falhada, como pintada no muro."""
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    layer = Image.new("RGBA", im.size, (0, 0, 0, 0))
    for shape, color in (("rect", BR_GREEN), ("diamond", BR_YELLOW), ("circle", (20, 70, 200))):
        m = Image.new("L", im.size, 0)
        d = ImageDraw.Draw(m)
        if shape == "rect":
            d.rectangle(box, fill=255)
        elif shape == "diamond":
            d.polygon([(x0 + w * 0.08, y0 + h / 2), (x0 + w / 2, y0 + h * 0.1), (x1 - w * 0.08, y0 + h / 2),
                       (x0 + w / 2, y1 - h * 0.1)], fill=255)
        else:
            r = h * 0.25
            d.ellipse((x0 + w / 2 - r, y0 + h / 2 - r, x0 + w / 2 + r, y0 + h / 2 + r), fill=255)
        part = Image.new("RGBA", im.size, rgba(color))
        part.putalpha(rough_mask(m, rng).point(lambda v: v * alpha // 255))
        layer.alpha_composite(part)
    im.alpha_composite(layer.rotate(-8, resample=Image.BICUBIC, center=((x0 + x1) / 2, (y0 + y1) / 2)))


def skyline(im: Image.Image, rng: random.Random, base_y, color=(6, 6, 12), windows=(57, 255, 20)):
    """Prédios recortados contra o muro, com janelas acesas aqui e ali."""
    d = ImageDraw.Draw(im)
    x = -20
    while x < im.width:
        w = rng.randint(50, 140)
        h = rng.randint(50, 230)
        d.rectangle((x, base_y - h, x + w, im.height), fill=rgba(color))
        if rng.random() < 0.35:  # antena
            d.rectangle((x + w // 2 - 2, base_y - h - rng.randint(20, 60), x + w // 2 + 2, base_y - h),
                        fill=rgba(color))
        for wy in range(base_y - h + 14, im.height - 10, 22):
            for wx in range(x + 10, x + w - 10, 18):
                if rng.random() < 0.045:
                    c = windows if rng.random() < 0.6 else MAGENTA
                    d.rectangle((wx, wy, wx + 5, wy + 7), fill=rgba(c, rng.randint(70, 160)))
        x += w + rng.randint(-10, 8)


def crown(size: int, color=(255, 214, 10), width=None) -> Image.Image:
    """Coroa de traço, rabiscada como pichação."""
    k = 4
    s = size * k
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    lw = width or max(3, s // 18)
    pts = [(s * 0.08, s * 0.78), (s * 0.12, s * 0.30), (s * 0.34, s * 0.55), (s * 0.5, s * 0.18),
           (s * 0.66, s * 0.55), (s * 0.88, s * 0.30), (s * 0.92, s * 0.78)]
    d.line(pts + [pts[0]], fill=rgba(color), width=lw, joint="curve")
    d.line([(s * 0.1, s * 0.9), (s * 0.9, s * 0.9)], fill=rgba(color), width=lw)
    for px, py in (pts[1], pts[3], pts[5]):
        r = lw * 0.9
        d.ellipse((px - r, py - r - lw, px + r, py + r - lw), fill=rgba(color))
    return im.resize((size, size), Image.LANCZOS)


def sparkle(size: int, color=NEON) -> Image.Image:
    im = Image.new("RGBA", (size * 4, size * 4), (0, 0, 0, 0))
    c = size * 2
    ImageDraw.Draw(im).polygon([(c, 0), (c + size * 0.35, c - size * 0.35), (size * 4, c),
                                (c + size * 0.35, c + size * 0.35), (c, size * 4),
                                (c - size * 0.35, c + size * 0.35), (0, c), (c - size * 0.35, c - size * 0.35)],
                               fill=rgba(color))
    return im.resize((size, size), Image.LANCZOS)


def tag(text: str, size: int, color, angle: float, font=MARKER) -> Image.Image:
    """Uma frase pichada (marcador), com contorno escuro e brilho."""
    k = 2
    f = ImageFont.truetype(font, size * k)
    lines = text.split("\n")
    masks = [text_mask(line, f, 10 * k) for line in lines]
    w = max(m.width for m in masks)
    h = sum(m.height for m in masks) - (len(masks) - 1) * 18 * k
    mask = Image.new("L", (w + 40 * k, h + 40 * k), 0)
    y = 20 * k
    for m in masks:
        mask.paste(m, (20 * k + (w - m.width) // 2, y))
        y += m.height - 18 * k
    mask = mask.rotate(angle, resample=Image.BICUBIC, expand=True)
    im = Image.new("RGBA", mask.size, (0, 0, 0, 0))
    im.alpha_composite(glow(dilate(mask, 5 * k), (0, 0, 0), 3 * k, 2.0))
    im.alpha_composite(glow(mask, color, 14 * k, 0.8))
    body = Image.new("RGBA", mask.size, rgba(color))
    body.putalpha(mask)
    im.alpha_composite(body)
    im = crop_alpha(im)
    return im.resize((im.width // k, im.height // k), Image.LANCZOS)


def wall(width, height, rng, tint=None):
    im = Image.new("RGBA", (width, height), rgba(WALL))
    bricks = Image.new("L", (width, height), 0)
    bd = ImageDraw.Draw(bricks)
    bw, bh = 132, 54
    for row in range(height // bh + 2):
        off = (bw // 2) if row % 2 else 0
        for col in range(-1, width // bw + 2):
            x, y = col * bw + off, row * bh
            bd.rectangle((x + 3, y + 3, x + bw - 3, y + bh - 3), fill=rng.randint(14, 30))
    noise = Image.effect_noise((width, height), 28).point(lambda v: max(0, v - 110) // 6)
    texture = ImageChops.add(bricks, noise).filter(ImageFilter.GaussianBlur(0.8))
    tex_layer = Image.new("RGBA", (width, height), (40, 40, 52, 255))
    tex_layer.putalpha(texture)
    im.alpha_composite(tex_layer)
    if tint:
        wash = Image.new("L", (width, height), 0)
        ImageDraw.Draw(wash).ellipse((-width * 0.2, -height * 0.3, width * 1.2, height * 1.1), fill=tint[1])
        im.alpha_composite(glow(wash, tint[0], 160))
    return im


def vignette(im: Image.Image, strength=0.85):
    w, h = im.size
    v = Image.new("L", (w, h), 0)
    ImageDraw.Draw(v).ellipse((-w * 0.25, -h * 0.35, w * 1.25, h * 1.35), fill=255)
    v = v.filter(ImageFilter.GaussianBlur(220))
    dark = Image.new("RGBA", (w, h), (0, 0, 0, 255))
    dark.putalpha(v.point(lambda x: int((255 - x) * strength)))
    im.alpha_composite(dark)


def background(width=1920, height=1080) -> Image.Image:
    """O muro das telas: bandeira em spray, prédios, coroas, estrelas e respingos."""
    rng = random.Random(2026)
    im = wall(width, height, rng)
    brazil_flag(im, rng, (1120, 30, 1880, 520), 165)
    spray(im, rng, 420, 210, 430, NEON, 58, dots=160, drips=3)
    spray(im, rng, 1500, 620, 480, MAGENTA, 70, dots=200, drips=4)
    spray(im, rng, 160, 1000, 220, MAGENTA, 40, dots=60, drips=0)
    skyline(im, rng, 1010)
    vignette(im)
    for (x, y, sz, ang) in ((1640, 40, 110, 12), (1770, 860, 80, -6)):
        c = crown(sz).rotate(ang, resample=Image.BICUBIC, expand=True)
        im.alpha_composite(c, (x, y))
    for (x, y, sz) in ((40, 120, 46), (1020, 80, 30), (870, 470, 36), (1850, 380, 40), (640, 980, 28)):
        im.alpha_composite(sparkle(sz), (x, y))
    hh = tag("HUEHUE", 120, NEON, 78, font=WET)
    im.alpha_composite(hh, (width - hh.width - 8, 140))
    return im.convert("RGB")


def loading_background(width=1920, height=1080) -> Image.Image:
    """O muro da tela de carregamento: mais magenta, prédios embaixo e a frase HUEHUE SEMPRE!."""
    rng = random.Random(77)
    im = wall(width, height, rng, tint=(MAGENTA, 70))
    brazil_flag(im, rng, (40, 40, 700, 460), 90)
    spray(im, rng, 960, 520, 560, MAGENTA, 90, dots=200, drips=3)
    spray(im, rng, 300, 900, 240, NEON, 50, dots=70, drips=1)
    skyline(im, rng, 1030, windows=NEON)
    vignette(im, 0.7)
    for (x, y, sz, ang) in ((1520, 760, 110, -12), (150, 520, 80, 10)):
        im.alpha_composite(crown(sz).rotate(ang, resample=Image.BICUBIC, expand=True), (x, y))
    t = tag("HUEHUE\nSEMPRE!", 92, (255, 214, 10), 14)
    im.alpha_composite(t, (width - t.width - 60, height - t.height - 50))
    return im.convert("RGB")


def paint(mask: Image.Image, color, outline=(0, 0, 0), glow_color=None, glow_px=24, edge=6):
    """Pinta uma máscara: brilho, contorno escuro e a cor por cima."""
    im = Image.new("RGBA", mask.size, (0, 0, 0, 0))
    if glow_color:
        im.alpha_composite(glow(dilate(mask, edge), glow_color, glow_px, 1.2))
    e = Image.new("RGBA", mask.size, rgba(outline))
    e.putalpha(dilate(mask, edge))
    im.alpha_composite(e)
    body = Image.new("RGBA", mask.size, rgba(color))
    body.putalpha(mask)
    im.alpha_composite(body)
    return im


def logo() -> Image.Image:
    """NX branco de pincel + ZERO em spray magenta, coroa em cima, BR HUEHUE EDITION embaixo."""
    k = 2
    nx_font = ImageFont.truetype(MARKER, 250 * k)
    zero_font = ImageFont.truetype(WET, 230 * k)
    sub_font = ImageFont.truetype(MARKER, 92 * k)
    nx = text_mask("NX", nx_font, 30 * k)
    zero = text_mask("ZERO", zero_font, 30 * k).rotate(4, resample=Image.BICUBIC, expand=True)
    parts = [("BR ", WHITE), ("HUEHUE ", NEON), ("EDITION", WHITE)]
    probe = ImageDraw.Draw(Image.new("L", (1, 1)))
    sub_w = int(sum(probe.textlength(t, font=sub_font) for t, _ in parts)) + 60 * k
    sub_h = int(sub_font.size * 1.5)
    W = nx.width + zero.width - 40 * k
    H = max(nx.height, zero.height) + sub_h
    top = 110 * k  # room for the crown over the O
    im = Image.new("RGBA", (max(W, sub_w + 120 * k), H + 40 * k + top), (0, 0, 0, 0))
    full_nx = Image.new("L", im.size, 0)
    full_nx.paste(nx, (0, 20 * k + top))
    full_zero = Image.new("L", im.size, 0)
    full_zero.paste(zero, (nx.width - 50 * k, 10 * k + top))
    im.alpha_composite(paint(full_zero, MAGENTA, glow_color=MAGENTA, glow_px=26 * k, edge=6 * k))
    im.alpha_composite(paint(full_nx, WHITE, glow_color=(255, 255, 255), glow_px=10 * k, edge=6 * k))
    cr = crown(150 * k).rotate(-14, resample=Image.BICUBIC, expand=True)
    im.alpha_composite(cr, (min(im.width - cr.width, nx.width + zero.width - 250 * k), 0))
    sub = Image.new("L", (sub_w, sub_h), 0)
    color_layer = Image.new("RGBA", (sub_w, sub_h), (0, 0, 0, 0))
    x = 30 * k
    for text, color in parts:
        m = Image.new("L", (sub_w, sub_h), 0)
        ImageDraw.Draw(m).text((x, 0), text, font=sub_font, fill=255)
        sub = ImageChops.lighter(sub, m)
        c = Image.new("RGBA", (sub_w, sub_h), rgba(color))
        c.putalpha(m)
        color_layer.alpha_composite(c)
        x += probe.textlength(text, font=sub_font)
    sub = sub.rotate(-3, resample=Image.BICUBIC, expand=True)
    color_layer = color_layer.rotate(-3, resample=Image.BICUBIC, expand=True)
    sx, sy = 70 * k, max(nx.height, zero.height) - 40 * k + top
    full_sub = Image.new("L", im.size, 0)
    full_sub.paste(sub, (sx, sy))
    im.alpha_composite(glow(dilate(full_sub, 6 * k), (0, 0, 0), 3 * k, 2.4))
    im.alpha_composite(glow(full_sub, NEON, 12 * k, 0.6))
    im.alpha_composite(color_layer, (sx, sy))
    for (px, py, sz) in ((10, 150, 40), (nx.width // k - 40, 130, 28)):
        im.alpha_composite(sparkle(sz * k), (px * k, py * k))
    im = crop_alpha(im, 8 * k)
    return im.resize((im.width // k, im.height // k), Image.LANCZOS)


def sticker(face: Image.Image, border: int) -> Image.Image:
    pad = border * 2
    out = Image.new("RGBA", (face.width + pad * 2, face.height + pad * 2), (0, 0, 0, 0))
    alpha = Image.new("L", out.size, 0)
    alpha.paste(face.getchannel("A"), (pad, pad))
    ring = dilate(alpha, border)
    out.paste(Image.new("RGBA", out.size, rgba(WHITE)), (0, 0), ring)
    out.alpha_composite(face, (pad, pad))
    return out


def mascot(height=900) -> Image.Image:
    face = FACE.resize((int(FACE.width * (height * 0.8) / FACE.height), int(height * 0.8)),
                       Image.LANCZOS)
    st = sticker(face, 16).rotate(6, resample=Image.BICUBIC, expand=True)
    pad = 90
    im = Image.new("RGBA", (st.width + pad * 2, st.height + pad * 2), (0, 0, 0, 0))
    mask = Image.new("L", im.size, 0)
    mask.paste(st.getchannel("A"), (pad, pad))
    im.alpha_composite(glow(dilate(mask, 10), MAGENTA, 46, 1.2))
    im.alpha_composite(glow(dilate(mask, 4), NEON, 16, 1.0))
    im.alpha_composite(st, (pad, pad))
    cr = crown(int(st.width * 0.42)).rotate(-18, resample=Image.BICUBIC, expand=True)
    im.alpha_composite(cr, (pad + int(st.width * 0.5), 0))
    return crop_alpha(im)


def label(text: str, size: int, fill, outline=(0, 0, 0), glow_color=None) -> Image.Image:
    """Um rótulo em Bangers, levemente inclinado, com contorno escuro (e brilho opcional)."""
    k = 2
    font = ImageFont.truetype(BANGERS, size * k)
    # Altura fixa (pelas métricas da fonte, não pela caixa do texto): acento ou não, todo rótulo
    # tem a mesma altura e a mesma linha de base, e a tela só escala pela altura.
    pad = 24 * k
    ascent, descent = font.getmetrics()
    probe = ImageDraw.Draw(Image.new("L", (1, 1)))
    left, _, right, _ = probe.textbbox((0, 0), text, font=font)
    mask = Image.new("L", (right - left + pad * 2, ascent + descent + pad * 2), 0)
    ImageDraw.Draw(mask).text((pad - left, pad), text, font=font, fill=255)
    # Itálico: cisalhamento horizontal.
    shear = 0.12
    w = mask.width + int(mask.height * shear)
    mask = mask.transform((w, mask.height), Image.AFFINE,
                          (1, shear, -mask.height * shear, 0, 1, 0), resample=Image.BICUBIC)
    im = Image.new("RGBA", mask.size, (0, 0, 0, 0))
    if glow_color:
        im.alpha_composite(glow(dilate(mask, 4 * k), glow_color, 14 * k, 1.0))
    edge = Image.new("RGBA", mask.size, rgba(outline, 235))
    edge.putalpha(dilate(mask, 4 * k))
    shadow = Image.new("RGBA", mask.size, (0, 0, 0, 0))
    shadow.paste(edge, (3 * k, 4 * k), edge)
    im.alpha_composite(shadow)
    im.alpha_composite(edge)
    body = Image.new("RGBA", mask.size, rgba(fill))
    body.putalpha(mask)
    im.alpha_composite(body)
    box = im.getchannel("A").getbbox()
    if box:  # corta só dos lados, para manter a altura igual em todos
        im = im.crop((max(0, box[0] - 2 * k), 0, min(im.width, box[2] + 2 * k), im.height))
    return im.resize((im.width // k, im.height // k), Image.LANCZOS)


MENU = {
    "start": ("INICIAR", "START"),
    "games": ("JOGOS", "GAMES"),
    "settings": ("CONFIGURAÇÕES", "SETTINGS"),
    "controls": ("CONTROLES", "CONTROLS"),
    "about": ("SOBRE", "ABOUT"),
}
TITLES = {
    "games": ("MEUS JOGOS", "MY GAMES"),
    "settings": ("CONFIGURAÇÕES", "SETTINGS"),
    "controls": ("CONTROLES", "CONTROLS"),
    "about": ("SOBRE O NX ZERO", "ABOUT NX ZERO"),
    "loading": ("CARREGANDO...", "LOADING..."),
}


def icon(name: str, size=96) -> Image.Image:
    """Ícone branco num disco escuro translúcido (lê bem tanto no fundo quanto no item verde)."""
    k = 4
    s = size * k
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    c = s / 2
    W = rgba(WHITE)
    lw = int(s * 0.065)
    if name == "play":
        d.polygon([(c - s * 0.16, c - s * 0.26), (c - s * 0.16, c + s * 0.26), (c + s * 0.27, c)],
                  outline=W, width=lw)
    elif name == "games":
        q = s * 0.12
        for dx in (-1, 1):
            for dy in (-1, 1):
                x, y = c + dx * (q + s * 0.025) , c + dy * (q + s * 0.025)
                d.rounded_rectangle((x - q, y - q, x + q, y + q), radius=s * 0.03, fill=W)
    elif name in ("settings", "system"):
        r_out, r_in = s * 0.26, s * 0.19
        pts = []
        teeth = 8
        for i in range(teeth * 4):
            a = i / (teeth * 4) * math.tau
            r = r_out if (i % 4) in (0, 1) else r_in
            pts.append((c + math.cos(a) * r, c + math.sin(a) * r))
        d.polygon(pts, fill=W)
        d.ellipse((c - s * 0.09, c - s * 0.09, c + s * 0.09, c + s * 0.09), fill=(0, 0, 0, 0))
    elif name == "controls":
        d.rounded_rectangle((c - s * 0.29, c - s * 0.13, c + s * 0.29, c + s * 0.13),
                            radius=s * 0.13, fill=W)
        d.ellipse((c - s * 0.31, c - s * 0.05, c - s * 0.11, c + s * 0.21), fill=W)
        d.ellipse((c + s * 0.11, c - s * 0.05, c + s * 0.31, c + s * 0.21), fill=W)
        K = (0, 0, 0, 0)
        d.rectangle((c - s * 0.2, c - s * 0.018, c - s * 0.1, c + s * 0.018), fill=K)
        d.rectangle((c - s * 0.168, c - s * 0.05, c - s * 0.132, c + s * 0.05), fill=K)
        for dx, dy in ((0.15, -0.04), (0.2, 0.0), (0.15, 0.04), (0.1, 0.0)):
            x, y = c + dx * s, c + dy * s
            d.ellipse((x - s * 0.018, y - s * 0.018, x + s * 0.018, y + s * 0.018), fill=K)
    elif name == "about":
        d.ellipse((c - s * 0.045, c - s * 0.21, c + s * 0.045, c - s * 0.12), fill=W)
        d.rounded_rectangle((c - s * 0.04, c - s * 0.06, c + s * 0.04, c + s * 0.22),
                            radius=s * 0.03, fill=W)
    elif name == "graphics":
        d.rounded_rectangle((c - s * 0.27, c - s * 0.2, c + s * 0.27, c + s * 0.12),
                            radius=s * 0.03, outline=W, width=lw)
        d.rectangle((c - s * 0.03, c + s * 0.12, c + s * 0.03, c + s * 0.2), fill=W)
        d.rounded_rectangle((c - s * 0.14, c + s * 0.19, c + s * 0.14, c + s * 0.24),
                            radius=s * 0.02, fill=W)
    elif name == "account":
        d.ellipse((c - s * 0.1, c - s * 0.24, c + s * 0.1, c - s * 0.04), fill=W)
        d.pieslice((c - s * 0.22, c + s * 0.0, c + s * 0.22, c + s * 0.44), 180, 360, fill=W)
    elif name == "online":
        r = s * 0.25
        d.ellipse((c - r, c - r, c + r, c + r), outline=W, width=lw)
        d.ellipse((c - r * 0.45, c - r, c + r * 0.45, c + r), outline=W, width=lw)
        d.line((c - r, c, c + r, c), fill=W, width=lw)
    elif name == "sources":
        d.rectangle((c - s * 0.035, c - s * 0.25, c + s * 0.035, c + s * 0.04), fill=W)
        d.polygon([(c - s * 0.14, c - s * 0.03), (c + s * 0.14, c - s * 0.03), (c, c + s * 0.13)],
                  fill=W)
        d.rounded_rectangle((c - s * 0.24, c + s * 0.16, c + s * 0.24, c + s * 0.23),
                            radius=s * 0.03, fill=W)
    elif name == "updates":
        r = s * 0.22
        d.arc((c - r, c - r, c + r, c + r), 30, 300, fill=W, width=lw)
        a = math.radians(300)
        x, y = c + math.cos(a) * r, c + math.sin(a) * r
        d.polygon([(x - s * 0.1, y - s * 0.05), (x + s * 0.06, y - s * 0.12),
                   (x + s * 0.04, y + s * 0.08)], fill=W)
    elif name == "star":
        pts = []
        for i in range(10):
            a = math.radians(-90 + 36 * i)
            r = s * 0.34 if i % 2 == 0 else s * 0.15
            pts.append((c + math.cos(a) * r, c + math.sin(a) * r))
        d.polygon(pts, outline=W, width=lw)
    elif name == "star_on":
        pts = []
        for i in range(10):
            a = math.radians(-90 + 36 * i)
            r = s * 0.34 if i % 2 == 0 else s * 0.15
            pts.append((c + math.cos(a) * r, c + math.sin(a) * r))
        d.polygon(pts, fill=(255, 214, 10, 255))
    elif name == "search":
        r = s * 0.16
        d.ellipse((c - r - s * 0.05, c - r - s * 0.05, c + r - s * 0.05, c + r - s * 0.05),
                  outline=W, width=lw)
        d.line((c + s * 0.06, c + s * 0.06, c + s * 0.22, c + s * 0.22), fill=W, width=lw + k)
    return im.resize((size, size), Image.LANCZOS)


# Onde ficam os botões no Controller.png (1000x640), para o teste de controles acender cada um.
# (x, y, raio). library_screen.cpp tem a mesma tabela (kControllerSpots).
CONTROLLER_SPOTS = {
    "Left": (352, 398, 26), "Right": (448, 398, 26), "Up": (400, 350, 26), "Down": (400, 446, 26),
    "A": (700, 318, 36), "B": (770, 250, 36), "X": (630, 250, 36), "Y": (700, 182, 36),
    "LeftShoulder": (285, 98, 44), "RightShoulder": (715, 98, 44),
    "View": (440, 236, 22), "Menu": (560, 236, 22),
}


def controller() -> Image.Image:
    """Um controle genérico (sem marca), contorno verde-neon, com a carinha no botão do meio."""
    k = 2
    W, H = 1000 * k, 640 * k
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    body = Image.new("L", (W, H), 0)
    bd = ImageDraw.Draw(body)
    bd.rounded_rectangle((200 * k, 130 * k, 800 * k, 430 * k), radius=130 * k, fill=255)
    bd.ellipse((130 * k, 210 * k, 420 * k, 590 * k), fill=255)
    bd.ellipse((580 * k, 210 * k, 870 * k, 590 * k), fill=255)
    bumpers = Image.new("L", (W, H), 0)
    ImageDraw.Draw(bumpers).rounded_rectangle((170 * k, 66 * k, 400 * k, 140 * k),
                                              radius=34 * k, fill=255)
    ImageDraw.Draw(bumpers).rounded_rectangle((600 * k, 66 * k, 830 * k, 140 * k),
                                              radius=34 * k, fill=255)
    for mask, fill in ((bumpers, (44, 44, 54)), (body, (26, 26, 33))):
        im.alpha_composite(glow(dilate(mask, 5 * k), NEON, 18 * k, 0.9))
        edge = Image.new("RGBA", (W, H), rgba(NEON))
        edge.putalpha(dilate(mask, 4 * k))
        im.alpha_composite(edge)
        layer = Image.new("RGBA", (W, H), rgba(fill))
        layer.putalpha(mask)
        im.alpha_composite(layer)
    d = ImageDraw.Draw(im)
    font = ImageFont.truetype(BANGERS, 40 * k)

    def disc(x, y, r, color, outline=None):
        d.ellipse(((x - r) * k, (y - r) * k, (x + r) * k, (y + r) * k), fill=color,
                  outline=outline, width=3 * k if outline else 0)

    for x, y in ((300, 250), (600, 400)):  # analógicos
        disc(x, y, 64, (14, 14, 18), (70, 70, 84))
        disc(x, y, 44, (52, 52, 64))
        disc(x, y, 30, (40, 40, 50))
    # D-pad
    d.rounded_rectangle((376 * k, 330 * k, 424 * k, 466 * k), radius=8 * k, fill=(52, 52, 64))
    d.rounded_rectangle((332 * k, 374 * k, 468 * k, 422 * k), radius=8 * k, fill=(52, 52, 64))
    for name, color in (("A", (16, 160, 16)), ("B", (220, 30, 46)), ("X", (0, 110, 215)),
                        ("Y", (240, 170, 0))):
        x, y, r = CONTROLLER_SPOTS[name]
        disc(x, y, r, color)
        d.text((x * k, (y + 2) * k), name, font=font, fill=WHITE, anchor="mm")
    for name in ("View", "Menu"):
        x, y, _ = CONTROLLER_SPOTS[name]
        d.rounded_rectangle(((x - 20) * k, (y - 12) * k, (x + 20) * k, (y + 12) * k),
                            radius=12 * k, fill=(60, 60, 74))
    lb = ImageFont.truetype(BANGERS, 34 * k)
    for name, text in (("LeftShoulder", "LB"), ("RightShoulder", "RB")):
        x, y, _ = CONTROLLER_SPOTS[name]
        d.text((x * k, (y + 2) * k), text, font=lb, fill=(200, 200, 210), anchor="mm")
    # Botão do meio com a carinha do HueHue.
    disc(500, 150, 40, (14, 14, 18), NEON)
    face = FACE.resize((int(FACE.width * 62 * k / FACE.height), 62 * k), Image.LANCZOS)
    im.alpha_composite(face, (500 * k - face.width // 2, 150 * k - face.height // 2))
    return im.resize((1000, 640), Image.LANCZOS)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for old in OUT.glob("*"):
        old.unlink()
    background().save(OUT / "Background.jpg", quality=88, optimize=True)
    loading_background().save(OUT / "LoadingBackground.jpg", quality=88, optimize=True)
    logo().save(OUT / "Logo.png", optimize=True)
    mascot().save(OUT / "Mascot.png", optimize=True)
    controller().save(OUT / "Controller.png", optimize=True)
    tag("JOGAR EM\nQUALQUER\nLUGAR", 64, (190, 255, 60), 12).save(OUT / "tag_jogar.png", optimize=True)
    tag("GAMES\nSEM LIMITES\nHUEHUE!", 70, NEON, 10).save(OUT / "tag_semlimites.png", optimize=True)
    for name in ("play", "games", "settings", "controls", "about", "system", "graphics",
                 "account", "online", "sources", "updates", "search", "star", "star_on"):
        icon(name).save(OUT / f"icon_{name}.png", optimize=True)
    # A splash do Xbox (1240x600, fundo escuro do manifesto): o letreiro sobre o muro.
    wall_im = background().resize((1240, 698), Image.LANCZOS).crop((0, 49, 1240, 649)).convert("RGBA")
    mark = logo()
    mark = mark.resize((int(mark.width * 440 / mark.height), 440), Image.LANCZOS)
    wall_im.alpha_composite(mark, ((1240 - mark.width) // 2, 80))
    wall_im.convert("RGB").resize((620, 300), Image.LANCZOS).save(OUT.parent / "SplashScreen.png", optimize=True)
    print("ok:", len(list(OUT.iterdir())), "arquivos em", OUT)


if __name__ == "__main__":
    main()
