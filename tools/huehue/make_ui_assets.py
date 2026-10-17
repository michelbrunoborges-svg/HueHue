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


def background(width=1920, height=1080) -> Image.Image:
    rng = random.Random(2026)
    im = Image.new("RGBA", (width, height), rgba(WALL))
    # Parede de tijolos bem apagada, com ruído, para o spray ter onde "pegar".
    bricks = Image.new("L", (width, height), 0)
    bd = ImageDraw.Draw(bricks)
    bw, bh = 132, 54
    for row in range(height // bh + 2):
        off = (bw // 2) if row % 2 else 0
        for col in range(-1, width // bw + 2):
            x, y = col * bw + off, row * bh
            shade = rng.randint(14, 30)
            bd.rectangle((x + 3, y + 3, x + bw - 3, y + bh - 3), fill=shade)
    noise = Image.effect_noise((width, height), 28).point(lambda v: max(0, v - 110) // 6)
    texture = ImageChops.add(bricks, noise).filter(ImageFilter.GaussianBlur(0.8))
    tex_layer = Image.new("RGBA", (width, height), (40, 40, 52, 255))
    tex_layer.putalpha(texture)
    im.alpha_composite(tex_layer)
    # Vinheta: o centro mais aceso, as bordas no preto.
    vignette = Image.new("L", (width, height), 0)
    ImageDraw.Draw(vignette).ellipse((-width * 0.25, -height * 0.35, width * 1.25, height * 1.35),
                                     fill=255)
    vignette = vignette.filter(ImageFilter.GaussianBlur(220))
    dark = Image.new("RGBA", (width, height), (0, 0, 0, 255))
    dark.putalpha(vignette.point(lambda v: int((255 - v) * 0.85)))
    im.alpha_composite(dark)
    # Spray: verde atrás do letreiro, magenta atrás do mascote, respingos pelos cantos.
    spray(im, rng, 430, 200, 420, NEON, 62, dots=200, drips=3)
    spray(im, rng, 1480, 560, 470, MAGENTA, 74, dots=240, drips=4)
    spray(im, rng, 1820, 110, 180, NEON, 46, dots=70, drips=2)
    spray(im, rng, 140, 980, 220, MAGENTA, 40, dots=80, drips=0)
    spray(im, rng, 980, 1020, 160, NEON, 30, dots=40, drips=0)
    # Fita nas cores do Brasil no canto de baixo à direita.
    tape = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    td = ImageDraw.Draw(tape)
    for i, color in enumerate((BR_GREEN, BR_YELLOW, BR_BLUE)):
        y = 1010 + i * 16
        td.polygon([(1460, y + 60), (1920, y - 120), (1920, y - 106), (1460, y + 74)],
                   fill=rgba(color, 150))
    im.alpha_composite(tape.filter(ImageFilter.GaussianBlur(0.6)))
    return im.convert("RGB")


def logo() -> Image.Image:
    """NX ZERO em tinta escorrendo, contorno verde-neon, e BR HUEHUE EDITION em marcador magenta."""
    k = 2
    big = ImageFont.truetype(WET, 230 * k)
    sub = ImageFont.truetype(MARKER, 86 * k)
    main = text_mask("NX ZERO", big, 40 * k)
    tag = text_mask("BR HUEHUE EDITION", sub, 30 * k).rotate(-3, resample=Image.BICUBIC,
                                                               expand=True)
    w = max(main.width, tag.width + 60 * k) + 40 * k
    h = main.height + tag.height - 70 * k
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    mx, my = 20 * k, 0
    full = Image.new("L", (w, h), 0)
    full.paste(main, (mx, my))
    outline = dilate(full, 7 * k)
    im.alpha_composite(glow(outline, NEON, 28 * k, 1.3))
    im.alpha_composite(glow(dilate(full, 3 * k), (0, 0, 0), 2 * k, 2.0))
    stroke = Image.new("RGBA", (w, h), rgba(NEON))
    stroke.putalpha(outline)
    im.alpha_composite(stroke)
    inner = Image.new("RGBA", (w, h), rgba(WHITE))
    inner.putalpha(full)
    im.alpha_composite(inner)
    # A etiqueta em magenta, deslocada para a direita, com uma pincelada verde por baixo.
    tx, ty = w - tag.width - 10 * k, main.height - 95 * k
    swash = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    sd = ImageDraw.Draw(swash)
    sd.rounded_rectangle((tx + 20 * k, ty + tag.height * 0.62, tx + tag.width - 10 * k,
                          ty + tag.height * 0.62 + 22 * k), radius=11 * k, fill=rgba(NEON, 200))
    im.alpha_composite(swash.rotate(-3, resample=Image.BICUBIC, center=(tx, ty)))
    tag_full = Image.new("L", (w, h), 0)
    tag_full.paste(tag, (tx, ty))
    im.alpha_composite(glow(dilate(tag_full, 5 * k), (0, 0, 0), 4 * k, 2.2))
    im.alpha_composite(glow(tag_full, MAGENTA, 18 * k, 1.1))
    tl = Image.new("RGBA", (w, h), rgba(MAGENTA))
    tl.putalpha(tag_full)
    im.alpha_composite(tl)
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
    d.ellipse((0, 0, s - 1, s - 1), fill=(0, 0, 0, 120))
    c = s / 2
    W = rgba(WHITE)
    lw = int(s * 0.065)
    if name == "play":
        d.polygon([(c - s * 0.13, c - s * 0.2), (c - s * 0.13, c + s * 0.2), (c + s * 0.22, c)],
                  fill=W)
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
    background().save(OUT / "Background.jpg", quality=88, optimize=True)
    logo().save(OUT / "Logo.png", optimize=True)
    mascot().save(OUT / "Mascot.png", optimize=True)
    controller().save(OUT / "Controller.png", optimize=True)
    for key, (pt, en) in MENU.items():
        label(pt, 64, WHITE).save(OUT / f"menu_{key}_pt.png", optimize=True)
        label(en, 64, WHITE).save(OUT / f"menu_{key}_en.png", optimize=True)
    for key, (pt, en) in TITLES.items():
        label(pt, 80, NEON, glow_color=NEON_DEEP).save(OUT / f"title_{key}_pt.png", optimize=True)
        label(en, 80, NEON, glow_color=NEON_DEEP).save(OUT / f"title_{key}_en.png", optimize=True)
    for name in ("play", "games", "settings", "controls", "about", "system", "graphics",
                 "account", "online", "sources", "updates", "search"):
        icon(name).save(OUT / f"icon_{name}.png", optimize=True)
    # A splash do Xbox (1240x600, fundo #0A0A0F do manifesto): o letreiro sobre a parede.
    wall = background().resize((1240, 698), Image.LANCZOS).crop((0, 49, 1240, 649)).convert("RGBA")
    mark = logo()
    mark = mark.resize((int(mark.width * 400 / mark.height), 400), Image.LANCZOS)
    wall.alpha_composite(mark, ((1240 - mark.width) // 2, 100))
    wall.convert("RGB").resize((620, 300), Image.LANCZOS).save(OUT.parent / "SplashScreen.png", optimize=True)
    print("ok:", len(list(OUT.iterdir())), "arquivos em", OUT)


if __name__ == "__main__":
    main()
