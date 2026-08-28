#!/usr/bin/env python3
"""Assemble generated mining-tile sources into edge-compatible runtime sheets."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


TILE_SIZE = 64
FRAME_COUNT = 19
DESTINATIONS = (
    "moon",
    "mars",
    "io",
    "saturn",
    "uranus",
    "neptune",
    "khepri-prime",
    "rift-belt",
)
OVERLAYS = (
    ("common", 40),
    ("rare", 40),
    ("exotic", 42),
    ("artifact", 44),
    ("fuel", 42),
    ("oxygen", 42),
    ("thermal", 48),
    ("cryo", 48),
    ("radiation", 48),
    ("toxic", 48),
)


def quantize(image: Image.Image, colors: int = 64) -> Image.Image:
    return image.quantize(
        colors=colors,
        method=Image.Quantize.FASTOCTREE,
        dither=Image.Dither.NONE,
    ).convert("RGBA")


def crop_board(board_path: Path) -> list[Image.Image]:
    board = Image.open(board_path).convert("RGBA")
    if board.width % 3 != 0 or board.height % 3 != 0:
        raise ValueError(f"{board_path} must divide evenly into a 3x3 grid")
    cell_width = board.width // 3
    cell_height = board.height // 3
    return [
        board.crop((column * cell_width, row * cell_height,
                    (column + 1) * cell_width, (row + 1) * cell_height)).resize(
            (TILE_SIZE, TILE_SIZE), Image.Resampling.LANCZOS
        )
        for row in range(3)
        for column in range(3)
    ]


def blend_pixel(a: tuple[int, ...], b: tuple[int, ...], weight: float) -> tuple[int, ...]:
    return tuple(round(left * (1.0 - weight) + right * weight) for left, right in zip(a, b))


def make_group_edge_compatible(images: list[Image.Image], margin: int = 7) -> list[Image.Image]:
    """Give every variant the same opposing borders with a short inward blend."""
    images = [image.convert("RGBA") for image in images]
    pixels = [image.load() for image in images]
    vertical_profile = []
    horizontal_profile = []
    for y in range(TILE_SIZE):
        samples = [pixels[index][x, y] for index in range(len(images)) for x in (0, TILE_SIZE - 1)]
        vertical_profile.append(tuple(round(sum(sample[channel] for sample in samples) / len(samples)) for channel in range(4)))
    for x in range(TILE_SIZE):
        samples = [pixels[index][x, y] for index in range(len(images)) for y in (0, TILE_SIZE - 1)]
        horizontal_profile.append(tuple(round(sum(sample[channel] for sample in samples) / len(samples)) for channel in range(4)))

    for pixel_map in pixels:
        for distance in range(margin):
            weight = 1.0 - distance / margin
            opposite = TILE_SIZE - 1 - distance
            for y in range(TILE_SIZE):
                pixel_map[distance, y] = blend_pixel(pixel_map[distance, y], vertical_profile[y], weight)
                pixel_map[opposite, y] = blend_pixel(pixel_map[opposite, y], vertical_profile[y], weight)
            for x in range(TILE_SIZE):
                pixel_map[x, distance] = blend_pixel(pixel_map[x, distance], horizontal_profile[x], weight)
                pixel_map[x, opposite] = blend_pixel(pixel_map[x, opposite], horizontal_profile[x], weight)

    # Quantize the three variants against one shared palette, then re-lock their
    # outermost pixels so any variant can meet any sibling without a color seam.
    strip = Image.new("RGBA", (TILE_SIZE * len(images), TILE_SIZE))
    for index, image in enumerate(images):
        strip.alpha_composite(image, (index * TILE_SIZE, 0))
    strip = quantize(strip)
    images = [strip.crop((index * TILE_SIZE, 0, (index + 1) * TILE_SIZE, TILE_SIZE)) for index in range(len(images))]
    pixels = [image.load() for image in images]
    for y in range(TILE_SIZE):
        shared = pixels[0][0, y]
        for pixel_map in pixels:
            pixel_map[0, y] = shared
            pixel_map[TILE_SIZE - 1, y] = shared
    for x in range(TILE_SIZE):
        shared = pixels[0][x, 0]
        for pixel_map in pixels:
            pixel_map[x, 0] = shared
            pixel_map[x, TILE_SIZE - 1] = shared
    return images


def prepare_overlay(path: Path, maximum_size: int) -> Image.Image:
    image = Image.open(path).convert("RGBA")
    alpha = image.getchannel("A")
    bounds = alpha.getbbox()
    if bounds is None:
        raise ValueError(f"{path} contains no visible pixels")
    image = image.crop(bounds)
    scale = min(maximum_size / image.width, maximum_size / image.height)
    size = (max(1, round(image.width * scale)), max(1, round(image.height * scale)))
    image = image.resize(size, Image.Resampling.LANCZOS)
    image = quantize(image, 48)
    # Generated alpha fringes look muddy after nearest-neighbor atlas sampling.
    alpha = image.getchannel("A").point(lambda value: 255 if value >= 96 else 0)
    image.putalpha(alpha)
    canvas = Image.new("RGBA", (TILE_SIZE, TILE_SIZE))
    canvas.alpha_composite(image, ((TILE_SIZE - image.width) // 2, (TILE_SIZE - image.height) // 2))
    return canvas


def verify_group_edges(frames: list[Image.Image], indices: tuple[int, int, int], label: str) -> None:
    reference = frames[indices[0]].load()
    for index in indices:
        pixels = frames[index].load()
        for coordinate in range(TILE_SIZE):
            if pixels[0, coordinate] != reference[0, coordinate] or pixels[TILE_SIZE - 1, coordinate] != reference[0, coordinate]:
                raise ValueError(f"{label} frame {index} has a horizontal tiling seam")
            if pixels[coordinate, 0] != reference[coordinate, 0] or pixels[coordinate, TILE_SIZE - 1] != reference[coordinate, 0]:
                raise ValueError(f"{label} frame {index} has a vertical tiling seam")


def build_sheet(destination: str, source_dir: Path) -> Image.Image:
    geology = crop_board(source_dir / f"{destination}.png")
    regolith = make_group_edge_compatible(geology[0:3])
    hard_rock = make_group_edge_compatible(geology[3:6])
    bedrock = make_group_edge_compatible(geology[6:9])
    frames = regolith + hard_rock + bedrock
    for overlay_index, (overlay_name, maximum_size) in enumerate(OVERLAYS):
        base = hard_rock[overlay_index % len(hard_rock)].copy()
        base.alpha_composite(prepare_overlay(source_dir / f"{overlay_name}.png", maximum_size))
        frames.append(base)

    if len(frames) != FRAME_COUNT:
        raise AssertionError(f"expected {FRAME_COUNT} frames, got {len(frames)}")
    verify_group_edges(frames, (0, 1, 2), f"{destination} regolith")
    verify_group_edges(frames, (3, 4, 5), f"{destination} hard rock")
    verify_group_edges(frames, (6, 7, 8), f"{destination} bedrock")

    sheet = Image.new("RGBA", (TILE_SIZE * FRAME_COUNT, TILE_SIZE))
    for index, frame in enumerate(frames):
        sheet.alpha_composite(frame, (index * TILE_SIZE, 0))
    return sheet.convert("RGB")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source_dir", type=Path)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--contact-sheet", type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    sheets = []
    for destination in DESTINATIONS:
        sheet = build_sheet(destination, args.source_dir)
        output = args.output_dir / f"mining-tiles-{destination}.png"
        sheet.save(output, optimize=True)
        sheets.append(sheet)
        print(f"wrote {output} ({sheet.width}x{sheet.height})")

    if args.contact_sheet:
        args.contact_sheet.parent.mkdir(parents=True, exist_ok=True)
        contact = Image.new("RGB", (TILE_SIZE * FRAME_COUNT, TILE_SIZE * len(sheets)))
        for index, sheet in enumerate(sheets):
            contact.paste(sheet, (0, index * TILE_SIZE))
        contact.save(args.contact_sheet, optimize=True)
        print(f"wrote {args.contact_sheet} ({contact.width}x{contact.height})")


if __name__ == "__main__":
    main()
