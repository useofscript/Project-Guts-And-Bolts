#!/usr/bin/env python3
"""Draws the Guts&Bolts clothing templates (shirt and pants).

The pictures are 585 x 559, the classic layout: the torso's six sides at the
top, and one arm (shirts) or leg (pants) in each bottom corner. Paint your
clothing over the grey boxes; anything you leave see-through shows the
avatar's body colour. src/scene/PlayerModel.cpp maps the character onto the
same boxes, so keep the two in step.

    python3 tools/make_clothing_template.py
writes assets/templates/ and website/app/templates/.
"""
import os
from PIL import Image, ImageDraw, ImageFont

W, H = 585, 559
TORSO = {"FRONT": (231, 74, 128, 128), "BACK": (427, 74, 128, 128), "R": (165, 74, 64, 128),
         "L": (361, 74, 64, 128), "UP": (231, 8, 128, 64), "DOWN": (231, 204, 128, 64)}
RIGHT = {"FRONT": (217, 355, 64, 128), "BACK": (85, 355, 64, 128), "R": (151, 355, 64, 128),
         "L": (19, 355, 64, 128), "UP": (217, 289, 64, 64), "DOWN": (217, 485, 64, 64)}
LEFT = {"FRONT": (308, 355, 64, 128), "BACK": (440, 355, 64, 128), "R": (506, 355, 64, 128),
        "L": (374, 355, 64, 128), "UP": (308, 289, 64, 64), "DOWN": (308, 485, 64, 64)}


def font(size):
    for path in ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                 "/Library/Fonts/Arial Bold.ttf", "C:/Windows/Fonts/arialbd.ttf"):
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def draw(kind):
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    small, big = font(11), font(15)
    limb = "ARM" if kind == "shirt" else "LEG"
    groups = [("TORSO", TORSO, (200, 205, 214, 255)),
              ("RIGHT " + limb, RIGHT, (214, 206, 196, 255)),
              ("LEFT " + limb, LEFT, (196, 210, 214, 255))]
    for title, boxes, fill in groups:
        for side, (x, y, w, h) in boxes.items():
            d.rectangle([x, y, x + w - 1, y + h - 1], fill=fill, outline=(90, 95, 110, 255))
            tw = d.textlength(side, font=small)
            d.text((x + (w - tw) / 2, y + h / 2 - 6), side, font=small, fill=(60, 64, 76, 255))
        # The group's name over its FRONT box.
        x, y, w, h = boxes["FRONT"]
        tw = d.textlength(title, font=small)
        d.text((x + (w - tw) / 2, y + 6), title, font=small, fill=(40, 44, 56, 255))
    label = "Guts&Bolts " + ("Shirt" if kind == "shirt" else "Pants") + " Template"
    d.text((12, 12), label, font=big, fill=(30, 30, 40, 255))
    d.text((12, 32), "585 x 559. Paint over the boxes.", font=small, fill=(70, 70, 80, 255))
    d.text((12, 46), "See-through = body colour.", font=small, fill=(70, 70, 80, 255))
    d.text((12, 60), "R / L = the character's", font=small, fill=(70, 70, 80, 255))
    d.text((12, 74), "right / left side.", font=small, fill=(70, 70, 80, 255))
    return img


if __name__ == "__main__":
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    for out in ("assets/templates", "website/app/templates"):
        os.makedirs(os.path.join(root, out), exist_ok=True)
        for kind in ("shirt", "pants"):
            draw(kind).save(os.path.join(root, out, kind + "_template.png"))
    print("Wrote the shirt and pants templates.")
