"""NS HUEHUE: tiles e ícones do app (Xbox/Windows) compostos SÓ com os assets aprovados do
pacote de design (dist/uwp/Assets/ns: graffiti-background, mascot, logo). Nada é desenhado de novo.
Gera os tamanhos que o Xbox usa, em scale-100 e scale-200, sem transparência (o Xbox mostra o
tile em tela cheia)."""
from pathlib import Path
from PIL import Image, ImageEnhance

ROOT = Path(__file__).resolve().parents[2] / "dist/uwp/Assets"
NS = ROOT / "ns"
bg = Image.open(NS / "graffiti-background.jpg").convert("RGB")
mascot = Image.open(NS / "mascot.png").convert("RGBA")
logo = Image.open(NS / "logo.png").convert("RGBA")
logo = logo.crop(logo.getbbox())
BACK = (7, 16, 18)


def cover(img, w, h, focus_x=0.5, focus_y=0.5):
    s = max(w / img.width, h / img.height)
    r = img.resize((round(img.width * s), round(img.height * s)), Image.LANCZOS)
    x = round((r.width - w) * focus_x)
    y = round((r.height - h) * focus_y)
    return r.crop((x, y, x + w, y + h))


def fit(img, w, h):
    s = min(w / img.width, h / img.height)
    return img.resize((max(1, round(img.width * s)), max(1, round(img.height * s))), Image.LANCZOS)


def ground(w, h):
    g = cover(bg, w, h, 0.85, 0.35)
    g = ImageEnhance.Brightness(g).enhance(0.55)
    return g.convert("RGBA")


def square(size):
    im = ground(size, size)
    # The mascot's bust fills the top; the logo sits across the bottom.
    m = mascot.crop((0, 0, mascot.width, int(mascot.height * 0.62)))
    m = fit(m, int(size * 0.98), int(size * 0.78))
    im.alpha_composite(m, ((size - m.width) // 2, int(size * 0.02)))
    shade = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    for y in range(size // 2, size):
        a = int(200 * (y - size // 2) / (size // 2))
        shade.paste((BACK[0], BACK[1], BACK[2], a), (0, y, size, y + 1))
    im.alpha_composite(shade)
    l = fit(logo, int(size * 0.92), int(size * 0.34))
    im.alpha_composite(l, ((size - l.width) // 2, size - l.height - int(size * 0.05)))
    return im.convert("RGB")


def wide(w, h):
    im = ground(w, h)
    m = mascot.crop((0, 0, mascot.width, int(mascot.height * 0.70)))
    m = fit(m, int(w * 0.46), int(h * 1.0))
    im.alpha_composite(m, (w - m.width - int(w * 0.02), h - m.height))
    l = fit(logo, int(w * 0.56), int(h * 0.62))
    im.alpha_composite(l, (int(w * 0.04), (h - l.height) // 2))
    return im.convert("RGB")


def small(size):
    # Tiny sizes: the logo alone on the dark ground stays readable.
    im = Image.new("RGBA", (size, size), BACK + (255,))
    l = fit(logo, int(size * 0.96), int(size * 0.96))
    im.alpha_composite(l, ((size - l.width) // 2, (size - l.height) // 2))
    return im.convert("RGB")


# The package has no resources.pri (no makepri step), so Windows reads these exact paths: one
# file per name, at its 100% size.
square(150).save(ROOT / "Square150x150Logo.png", optimize=True)
square(310).save(ROOT / "Square310x310Logo.png", optimize=True)
square(71).save(ROOT / "Square71x71Logo.png", optimize=True)
wide(310, 150).save(ROOT / "Wide310x150Logo.png", optimize=True)
small(44).save(ROOT / "Square44x44Logo.png", optimize=True)
small(50).save(ROOT / "StoreLogo.png", optimize=True)
print("tiles ok")
