#!/usr/bin/env python3
"""Build the deterministic 32-row post-solar mining atlas from generated boards."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw


TILE = 64
FRAMES = 19


def quantize(image: Image.Image, colors: int = 64) -> Image.Image:
    return image.convert("RGB").quantize(
        colors=colors, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE
    ).convert("RGBA")


def crop_board(path: Path) -> list[Image.Image]:
    board = Image.open(path).convert("RGB")
    xs = [round(index * board.width / 3) for index in range(4)]
    ys = [round(index * board.height / 3) for index in range(4)]
    return [
        board.crop((xs[column], ys[row], xs[column + 1], ys[row + 1])).resize(
            (TILE, TILE), Image.Resampling.BOX
        ).convert("RGBA")
        for row in range(3)
        for column in range(3)
    ]


def mix(left: tuple[int, ...], right: tuple[int, ...], amount: float) -> tuple[int, ...]:
    return tuple(round(a * (1.0 - amount) + b * amount) for a, b in zip(left, right))


def make_group_seamless(images: list[Image.Image], margin: int = 6) -> list[Image.Image]:
    images = [image.copy().convert("RGBA") for image in images]
    maps = [image.load() for image in images]
    vertical = [
        tuple(round(sum(pixels[x, y][channel] for pixels in maps for x in (0, TILE - 1)) / (2 * len(maps))) for channel in range(4))
        for y in range(TILE)
    ]
    horizontal = [
        tuple(round(sum(pixels[x, y][channel] for pixels in maps for y in (0, TILE - 1)) / (2 * len(maps))) for channel in range(4))
        for x in range(TILE)
    ]
    for pixels in maps:
        for distance in range(margin):
            amount = 1.0 - distance / margin
            opposite = TILE - 1 - distance
            for y in range(TILE):
                pixels[distance, y] = mix(pixels[distance, y], vertical[y], amount)
                pixels[opposite, y] = mix(pixels[opposite, y], vertical[y], amount)
            for x in range(TILE):
                pixels[x, distance] = mix(pixels[x, distance], horizontal[x], amount)
                pixels[x, opposite] = mix(pixels[x, opposite], horizontal[x], amount)
    strip = Image.new("RGBA", (TILE * len(images), TILE))
    for index, image in enumerate(images):
        strip.alpha_composite(image, (index * TILE, 0))
    strip = quantize(strip)
    images = [strip.crop((i * TILE, 0, (i + 1) * TILE, TILE)) for i in range(len(images))]
    maps = [image.load() for image in images]
    for y in range(TILE):
        value = maps[0][0, y]
        for pixels in maps:
            pixels[0, y] = value
            pixels[TILE - 1, y] = value
    for x in range(TILE):
        value = maps[0][x, 0]
        for pixels in maps:
            pixels[x, 0] = value
            pixels[x, TILE - 1] = value
    return images


def overlay(kind: str) -> Image.Image:
    image = Image.new("RGBA", (TILE, TILE))
    draw = ImageDraw.Draw(image)
    cx, cy = 32, 31
    if kind == "common":
        draw.polygon([(cx, 14), (45, 22), (45, 39), (cx, 48), (19, 39), (19, 22)], fill=(184, 200, 214, 255), outline=(239, 247, 255, 255), width=3)
        draw.line([(24, 24), (39, 39)], fill=(113, 137, 158, 255), width=3)
    elif kind == "rare":
        draw.polygon([(32, 11), (38, 25), (52, 31), (38, 37), (32, 52), (26, 37), (12, 31), (26, 25)], fill=(239, 179, 42, 255), outline=(255, 222, 94, 255), width=3)
    elif kind == "exotic":
        draw.polygon([(32, 9), (45, 24), (39, 49), (25, 52), (18, 26)], fill=(139, 68, 218, 255), outline=(223, 151, 255, 255), width=3)
        draw.line([(32, 13), (30, 46)], fill=(245, 210, 255, 255), width=2)
    elif kind == "artifact":
        draw.rounded_rectangle((14, 17, 50, 47), radius=7, fill=(24, 109, 132, 255), outline=(116, 237, 255, 255), width=3)
        draw.ellipse((25, 23, 39, 37), outline=(202, 255, 255, 255), width=3)
        draw.line([(20, 41), (44, 41)], fill=(67, 190, 211, 255), width=2)
    elif kind == "fuel":
        draw.polygon([(32, 10), (45, 31), (41, 45), (32, 51), (23, 45), (19, 31)], fill=(217, 91, 22, 255), outline=(255, 172, 52, 255), width=3)
        draw.rectangle((28, 27, 36, 42), fill=(255, 204, 73, 255))
    elif kind == "oxygen":
        draw.ellipse((13, 13, 51, 51), outline=(58, 218, 245, 255), width=6)
        draw.ellipse((23, 23, 41, 41), outline=(193, 251, 255, 255), width=3)
    elif kind == "thermal":
        for points in [[(13, 47), (24, 34), (20, 26), (31, 14)], [(31, 50), (37, 38), (34, 29), (48, 16)], [(40, 51), (46, 43), (43, 35), (53, 29)]]:
            draw.line(points, fill=(255, 80, 20, 255), width=5)
            draw.line(points, fill=(255, 190, 43, 255), width=2)
    elif kind == "cryo":
        for angle in [(32, 8, 32, 56), (8, 32, 56, 32), (14, 14, 50, 50), (50, 14, 14, 50)]:
            draw.line(angle, fill=(89, 220, 255, 255), width=3)
        draw.ellipse((25, 25, 39, 39), fill=(217, 255, 255, 255))
    elif kind == "radiation":
        draw.ellipse((25, 25, 39, 39), fill=(224, 255, 57, 255))
        draw.arc((13, 9, 51, 47), 205, 335, fill=(174, 238, 34, 255), width=7)
        draw.arc((5, 22, 43, 60), 325, 95, fill=(174, 238, 34, 255), width=7)
        draw.arc((21, 22, 59, 60), 85, 215, fill=(174, 238, 34, 255), width=7)
    elif kind == "toxic":
        for box in [(12, 28, 28, 44), (29, 15, 48, 34), (37, 36, 55, 54)]:
            draw.ellipse(box, fill=(177, 53, 219, 230), outline=(245, 129, 255, 255), width=3)
        draw.ellipse((18, 18, 27, 27), fill=(221, 100, 255, 220))
    return image


def verify_group(frames: list[Image.Image], indices: tuple[int, int, int], label: str) -> None:
    for left_index in indices:
        for right_index in indices:
            left, right = frames[left_index].load(), frames[right_index].load()
            for coordinate in range(TILE):
                if left[TILE - 1, coordinate] != right[0, coordinate]:
                    raise ValueError(f"{label}: horizontal seam {left_index}->{right_index}")
                if left[coordinate, TILE - 1] != right[coordinate, 0]:
                    raise ValueError(f"{label}: vertical seam {left_index}->{right_index}")


def build_sheet(source: Path, profile_id: str) -> Image.Image:
    geology = crop_board(source)
    frames = (
        make_group_seamless(geology[0:3])
        + make_group_seamless(geology[3:6])
        + make_group_seamless(geology[6:9])
    )
    for index, kind in enumerate(("common", "rare", "exotic", "artifact", "fuel", "oxygen", "thermal", "cryo", "radiation", "toxic")):
        base = frames[3 + index % 3].copy()
        base.alpha_composite(overlay(kind))
        composite = quantize(base, 64)
        composite_pixels = composite.load()
        base_pixels = frames[3].load()
        # Palette reduction can shift an untouched border by a single channel.
        # Restore the shared Hard Rock profile exactly so semantic frames tile
        # against every sibling variant, not merely against themselves.
        for coordinate in range(TILE):
            composite_pixels[0, coordinate] = base_pixels[0, coordinate]
            composite_pixels[TILE - 1, coordinate] = base_pixels[TILE - 1, coordinate]
            composite_pixels[coordinate, 0] = base_pixels[coordinate, 0]
            composite_pixels[coordinate, TILE - 1] = base_pixels[coordinate, TILE - 1]
        frames.append(composite)
    verify_group(frames, (0, 1, 2), profile_id + " regolith")
    verify_group(frames, (3, 4, 5), profile_id + " hard-rock")
    verify_group(frames, (6, 7, 8), profile_id + " bedrock")
    sheet = Image.new("RGB", (TILE * FRAMES, TILE))
    for index, frame in enumerate(frames):
        sheet.paste(frame.convert("RGB"), (index * TILE, 0))
    return sheet


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--contact-sheet", type=Path)
    args = parser.parse_args()
    data = json.loads(args.manifest.read_text(encoding="utf-8"))
    profiles = sorted(data["profiles"], key=lambda profile: profile["row"])
    if [profile["row"] for profile in profiles] != list(range(32)):
        raise ValueError("post-solar geology rows must be exactly 0..31")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    library = Image.new("RGB", (TILE * FRAMES, TILE * len(profiles)))
    for profile in profiles:
        source = args.manifest.parent / profile["source"]
        sheet = build_sheet(source, profile["id"])
        sheet_path = args.output_dir / f"mining-tiles-{profile['id'].replace('_', '-')}.png"
        sheet.save(sheet_path, optimize=True)
        library.paste(sheet, (0, profile["row"] * TILE))
        print(f"wrote {sheet_path} ({sheet.width}x{sheet.height})")
    library_path = args.output_dir / "mining-tiles-post-solar.png"
    library.save(library_path, optimize=True)
    print(f"wrote {library_path} ({library.width}x{library.height})")
    if args.contact_sheet:
        args.contact_sheet.parent.mkdir(parents=True, exist_ok=True)
        contact = library.resize((28 * FRAMES, 28 * len(profiles)), Image.Resampling.NEAREST)
        contact.save(args.contact_sheet, optimize=True)
        print(f"wrote {args.contact_sheet} ({contact.width}x{contact.height})")


if __name__ == "__main__":
    main()
