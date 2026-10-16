"""Gera os icones e a splash do HueHue Switch (PNG) a partir de formas vetoriais simples."""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = Path(sys.argv[1])
FONT = "/usr/share/fonts/truetype/google-fonts/Poppins-Bold.ttf"
BG = (11, 11, 13, 255)
GREEN, BLUE, YELLOW, LIME = (34, 197, 94), (59, 130, 246), (250, 204, 21), (200, 243, 29)
WHITE = (255, 255, 255)


FACE = Image.open(sys.argv[2]).convert("RGBA") if len(sys.argv) > 2 else None


def sticker(face: Image.Image, border: int) -> Image.Image:
    """Cara com contorno branco (estilo adesivo) para destacar no fundo escuro."""
    pad = border * 2
    out = Image.new("RGBA", (face.width + pad * 2, face.height + pad * 2), (0, 0, 0, 0))
    a = Image.new("L", out.size, 0)
    a.paste(face.getchannel("A"), (pad, pad))
    ring = a.filter(ImageFilter.MaxFilter(border * 2 + 1))
    out.paste(Image.new("RGBA", out.size, WHITE + (255,)), (0, 0), ring)
    out.alpha_composite(face, (pad, pad))
    return out


def mark(size: int) -> Image.Image:
    """Bandeira estilizada (losango amarelo + circulo azul) com a cara do HueHue por cima."""
    s = 1024
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    sq = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    ImageDraw.Draw(sq).rounded_rectangle((180, 180, 844, 844), radius=120, fill=YELLOW)
    im.alpha_composite(sq.rotate(45, resample=Image.BICUBIC))
    d.ellipse((262, 262, 762, 762), fill=BLUE)
    if FACE is None:
        w = 34
        d.line([(370, 470), (425, 410), (480, 470)], fill=WHITE, width=w, joint="curve")
        d.line([(544, 470), (599, 410), (654, 470)], fill=WHITE, width=w, joint="curve")
        d.chord((360, 420, 664, 690), start=0, end=180, fill=WHITE)
    else:
        target_h = 700
        f = FACE.resize((int(FACE.width * target_h / FACE.height), target_h), Image.LANCZOS)
        st = sticker(f, 14)
        im.alpha_composite(st, ((s - st.width) // 2 + 10, (s - st.height) // 2 + 20))
    return im.resize((size, size), Image.LANCZOS)


def tile(size: int, pad: float) -> Image.Image:
    im = Image.new("RGBA", (size, size), BG)
    m = mark(int(size * (1 - 2 * pad)))
    im.alpha_composite(m, (int(size * pad), int(size * pad)))
    return im


def splash() -> Image.Image:
    W, H, k = 620, 300, 3  # desenha em 3x e reduz
    im = Image.new("RGBA", (W * k, H * k), BG)
    pat = Image.new("RGBA", (W * k * 2, H * k * 2), (0, 0, 0, 0))
    pd = ImageDraw.Draw(pat)
    pf = ImageFont.truetype(FONT, 30 * k)
    row = "HUE RA HUE RA HUEHUE " * 6
    for i in range(14):
        pd.text(((-160 if i % 2 else -40) * k, i * 44 * k), row, font=pf, fill=(255, 255, 255, 13))
    pat = pat.rotate(9, resample=Image.BICUBIC)
    im.alpha_composite(pat, (-W * k // 2, -H * k // 2))
    d = ImageDraw.Draw(im)
    m = mark(110 * k)
    im.alpha_composite(m, (70 * k, 95 * k))
    f = ImageFont.truetype(FONT, 66 * k)
    x, y = 196 * k, 98 * k
    for ch, col in zip("HUEHUE", [GREEN, BLUE, YELLOW, GREEN, BLUE, YELLOW]):
        d.text((x, y), ch, font=f, fill=col)
        x += d.textlength(ch, font=f)
    tf = ImageFont.truetype(FONT, 17 * k)
    tw = d.textlength("SWITCH", font=tf) + 22 * k
    d.rounded_rectangle((196 * k, 182 * k, 196 * k + tw, 210 * k), radius=6 * k, fill=LIME)
    d.text((196 * k + 11 * k, 183 * k), "SWITCH", font=tf, fill=BG)
    return im.resize((W, H), Image.LANCZOS)


OUT.mkdir(parents=True, exist_ok=True)
mark(256).save(OUT / "HueHueLogo.png")
tile(150, 0.14).save(OUT / "Square150x150Logo.png")
tile(44, 0.06).save(OUT / "Square44x44Logo.png")
tile(50, 0.06).save(OUT / "StoreLogo.png")
splash().save(OUT / "SplashScreen.png")
print("ok", sorted(p.name for p in OUT.glob("*.png")))
