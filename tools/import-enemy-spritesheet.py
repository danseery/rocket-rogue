#!/usr/bin/env python3

"""Import a 4x5 GenAI enemy contact sheet into RocketGame's 20x1 runtime format."""

from __future__ import annotations

import argparse
import importlib.util
import statistics
from pathlib import Path

from PIL import Image


FRAME_SIZE = 128
SUBJECT_ENVELOPE = 112
SOURCE_COLUMNS = 4
SOURCE_ROWS = 5
RUNTIME_FRAMES = SOURCE_COLUMNS * SOURCE_ROWS
GROUNDED_BASELINE = 120


def load_chroma_predicate():
    module_path = Path(__file__).with_name("import-chroma-sprite.py")
    spec = importlib.util.spec_from_file_location("rocket_chroma_import", module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load shared chroma rules from {module_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.is_chroma


is_chroma = load_chroma_predicate()


def clean_chroma(image: Image.Image) -> Image.Image:
    cleaned = image.convert("RGBA")
    pixels = cleaned.get_flattened_data() if hasattr(cleaned, "get_flattened_data") else cleaned.getdata()
    cleaned.putdata([
        (0, 0, 0, 0) if is_chroma(pixel) else pixel
        for pixel in pixels
    ])
    return cleaned


def source_frames(image: Image.Image) -> list[Image.Image]:
    frames: list[Image.Image] = []
    for row in range(SOURCE_ROWS):
        top = round(row * image.height / SOURCE_ROWS)
        bottom = round((row + 1) * image.height / SOURCE_ROWS)
        for column in range(SOURCE_COLUMNS):
            left = round(column * image.width / SOURCE_COLUMNS)
            right = round((column + 1) * image.width / SOURCE_COLUMNS)
            frames.append(clean_chroma(image.crop((left, top, right, bottom))))
    return frames


def import_enemy_sheet(source: Path, destination: Path, floating: bool) -> None:
    image = Image.open(source).convert("RGBA")
    frames = source_frames(image)
    bounds = [frame.getbbox() for frame in frames]
    if any(bound is None for bound in bounds):
        missing = [index for index, bound in enumerate(bounds) if bound is None]
        raise ValueError(f"Empty source frames in {source}: {missing}")

    opaque_bounds = [bound for bound in bounds if bound is not None]
    maximum_width = max(bound[2] - bound[0] for bound in opaque_bounds)
    maximum_height = max(bound[3] - bound[1] for bound in opaque_bounds)
    scale = min(SUBJECT_ENVELOPE / maximum_width, SUBJECT_ENVELOPE / maximum_height)
    if scale <= 0.0:
        raise ValueError(f"Invalid source scale for {source}")

    runtime = Image.new("RGBA", (FRAME_SIZE * RUNTIME_FRAMES, FRAME_SIZE), (0, 0, 0, 0))
    pivot_x_values: list[float] = []
    packed_bounds: list[tuple[int, int, int, int]] = []
    for index, (frame, bound) in enumerate(zip(frames, opaque_bounds, strict=True)):
        cropped = frame.crop(bound)
        resized_size = (
            max(1, round(cropped.width * scale)),
            max(1, round(cropped.height * scale)),
        )
        resized = cropped.resize(resized_size, Image.Resampling.NEAREST)
        resized_bound = resized.getbbox()
        if resized_bound is None:
            raise ValueError(f"Frame {index} lost all opaque pixels while scaling {source}")
        # Nearest scaling can leave transparent fringe when a generated source
        # has sparse alpha at its crop edge. Trim that fringe without changing
        # scale, then align the actual opaque subject to the shared pivot.
        sprite = resized.crop(resized_bound)
        canvas = Image.new("RGBA", (FRAME_SIZE, FRAME_SIZE), (0, 0, 0, 0))
        x = (FRAME_SIZE - sprite.width) // 2
        y = (FRAME_SIZE - sprite.height) // 2 if floating else GROUNDED_BASELINE - sprite.height
        canvas.alpha_composite(sprite, (x, y))
        packed_bound = canvas.getbbox()
        if packed_bound is None:
            raise ValueError(f"Frame {index} became empty while importing {source}")
        if packed_bound[0] < 8 or packed_bound[1] < 8 or packed_bound[2] > 120 or packed_bound[3] > 120:
            raise ValueError(f"Frame {index} exceeds the 112x112 envelope in {source}: {packed_bound}")
        corners = [canvas.getpixel(point)[3] for point in ((0, 0), (127, 0), (0, 127), (127, 127))]
        if any(corners):
            raise ValueError(f"Opaque corner in frame {index} of {source}: {corners}")
        canvas_pixels = canvas.get_flattened_data() if hasattr(canvas, "get_flattened_data") else canvas.getdata()
        for pixel in canvas_pixels:
            if pixel[3] > 0 and is_chroma(pixel):
                raise ValueError(f"Residual chroma in frame {index} of {source}")
        pivot_x_values.append((packed_bound[0] + packed_bound[2]) * 0.5)
        packed_bounds.append(packed_bound)
        runtime.alpha_composite(canvas, (index * FRAME_SIZE, 0))

    median_pivot_x = statistics.median(pivot_x_values)
    maximum_pivot_drift = max(abs(value - median_pivot_x) for value in pivot_x_values)
    if maximum_pivot_drift > 2.0:
        raise ValueError(f"Excessive packed pivot drift in {source}: {maximum_pivot_drift:.2f}px")
    if runtime.size != (2560, 128):
        raise ValueError(f"Unexpected runtime dimensions for {source}: {runtime.size}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    runtime.save(destination, optimize=True)
    print(
        f"{destination}: 20 frames, source={image.size}, scale={scale:.4f}, "
        f"pivot drift={maximum_pivot_drift:.2f}px, bounds={packed_bounds[0]}..{packed_bounds[-1]}"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--floating", action="store_true", help="Center vertically instead of using the grounded baseline.")
    args = parser.parse_args()
    import_enemy_sheet(args.source, args.destination, args.floating)


if __name__ == "__main__":
    main()
