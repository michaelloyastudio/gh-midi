#!/usr/bin/env python3
"""Generate the GH MIDI app icon.

The background is not a screenshot crop -- it is kBgFrag from
HighwayRenderer.cpp ported to numpy, so the icon is the same stage gradient,
horizon bloom, sweeping beams and embers the app actually draws, evaluated
cleanly at 1024 instead of upscaled from a 2x grab.

"GH" is set in MetalMania, the app's own display face, in its own gold.
"""
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

S      = 1024
GOLD   = (0xf2, 0xd0, 0x2a)
FONT   = 'plugin/assets/MetalMania-Regular.ttf'
UTIME  = 3.4          # beam phase that puts the two beams pleasingly apart

def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)

def background(n):
    # vUV: y=0 at the BOTTOM, as in GL. Flipped to image space at the end.
    ux, uy = np.meshgrid(np.linspace(0, 1, n), np.linspace(0, 1, n))
    top = np.array([0.075, 0.075, 0.145])
    bot = np.array([0.012, 0.012, 0.030])
    c = bot[None, None, :] + (top - bot)[None, None, :] * uy[..., None]

    horizon = 0.62
    d = np.sqrt((ux - 0.5) ** 2 + (uy - horizon) ** 2)
    c += np.array([0.23, 0.16, 0.40])[None, None, :] * ((1.0 - smoothstep(0.0, 0.55, d)) * 0.85)[..., None]

    for b in (0, 1):
        s = b * 2.0 - 1.0
        bx = 0.5 + s * 0.20 + np.sin(UTIME * 0.25 + b * 2.6) * 0.13
        span = uy - horizon
        dd = np.abs(ux - (bx + span * s * 0.22))
        beam = (1.0 - smoothstep(0.0, 0.018 + np.maximum(span, 0) * 0.10, dd)) * 0.05
        c += (np.where(span > 0, beam, 0.0))[..., None] * 0.9

    rng = np.random.default_rng(7)
    for _ in range(14):
        ex, ey = rng.random(), rng.random()
        dd = np.sqrt((ux - ex) ** 2 + (uy - ey) ** 2)
        c += np.array([0.9, 0.55, 0.25])[None, None, :] * ((1.0 - smoothstep(0.0, 0.0045, dd)) * 0.5)[..., None]

    c = np.clip(c, 0, 1)
    return Image.fromarray((c * 255).astype('uint8')).transpose(Image.FLIP_TOP_BOTTOM)

def squircle(size, r):
    m = Image.new('L', (size * 4, size * 4), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, size * 4 - 1, size * 4 - 1], r * 4, fill=255)
    return m.resize((size, size), Image.LANCZOS)

def build(out, art=824, inset=None, glow=True):
    canvas = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    bg = background(art)

    d = ImageDraw.Draw(bg)
    # Fit "GH" to the art box by measuring, not by guessing a point size.
    size = art
    while size > 10:
        f = ImageFont.truetype(FONT, size)
        bb = d.textbbox((0, 0), 'GH', font=f)
        if (bb[2] - bb[0]) <= art * 0.66 and (bb[3] - bb[1]) <= art * 0.46:
            break
        size -= 4
    bb = d.textbbox((0, 0), 'GH', font=f)
    tx = (art - (bb[2] - bb[0])) / 2 - bb[0]
    ty = (art - (bb[3] - bb[1])) / 2 - bb[1]

    if glow:
        g = Image.new('RGBA', bg.size, (0, 0, 0, 0))
        ImageDraw.Draw(g).text((tx, ty), 'GH', font=f, fill=GOLD + (255,))
        bg = Image.alpha_composite(bg.convert('RGBA'),
                                   g.filter(ImageFilter.GaussianBlur(art * 0.045)))
        bg = Image.alpha_composite(bg, g.filter(ImageFilter.GaussianBlur(art * 0.012)))
        d = ImageDraw.Draw(bg)
    d.text((tx, ty), 'GH', font=f, fill=GOLD + (255,))

    bg = bg.convert('RGBA')
    bg.putalpha(squircle(art, int(art * 0.2237)))
    o = (S - art) // 2
    canvas.alpha_composite(bg, (o, o))
    canvas.save(out)
    return out

if __name__ == '__main__':
    print(build('plugin/assets/icon.png'))
