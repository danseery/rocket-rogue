#!/usr/bin/env python3

import argparse
from pathlib import Path

from PIL import Image


def is_chroma(pixel: tuple[int, int, int, int]) -> bool:
    red, green, blue, _alpha = pixel
    return (
        red > 180
        and green < 80
        and 100 < blue < 210
        and red > blue + 35
    )


def import_sprite(source: Path, destination: Path, canvas_size: int, subject_size: int) -> None:
    image = Image.open(source).convert("RGBA")
    source_pixels = image.get_flattened_data() if hasattr(image, "get_flattened_data") else image.getdata()
    cleaned_pixels = [
        (red, green, blue, 0) if is_chroma((red, green, blue, alpha)) else (red, green, blue, alpha)
        for red, green, blue, alpha in source_pixels
    ]
    image.putdata(cleaned_pixels)

    bounds = image.getbbox()
    if bounds is None:
        raise ValueError(f"No opaque subject found in {source}")

    cropped = image.crop(bounds)
    scale = min(subject_size / cropped.width, subject_size / cropped.height)
    resized_size = (
        max(1, round(cropped.width * scale)),
        max(1, round(cropped.height * scale)),
    )
    sprite = cropped.resize(resized_size, Image.Resampling.NEAREST)
    canvas = Image.new("RGBA", (canvas_size, canvas_size), (0, 0, 0, 0))
    canvas.alpha_composite(
        sprite,
        ((canvas_size - resized_size[0]) // 2, (canvas_size - resized_size[1]) // 2),
    )

    destination.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(destination, optimize=True)
    corner_alpha = [
        canvas.getpixel((0, 0))[3],
        canvas.getpixel((canvas_size - 1, 0))[3],
        canvas.getpixel((0, canvas_size - 1))[3],
        canvas.getpixel((canvas_size - 1, canvas_size - 1))[3],
    ]
    max_alpha = canvas.getchannel("A").getextrema()[1]
    if any(corner_alpha) or max_alpha != 255:
        raise ValueError(
            f"Alpha verification failed for {destination}: corners={corner_alpha}, max={max_alpha}"
        )
    print(
        f"{destination}: source bounds={bounds}, packed size={resized_size}, "
        f"corner alpha={corner_alpha}, max alpha={max_alpha}"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Chroma-key, crop, and pack a RocketGame sprite.")
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--canvas-size", type=int, default=512)
    parser.add_argument("--subject-size", type=int, default=448)
    args = parser.parse_args()

    if args.subject_size > args.canvas_size:
        parser.error("--subject-size must not exceed --canvas-size")
    import_sprite(args.source, args.destination, args.canvas_size, args.subject_size)


if __name__ == "__main__":
    main()
