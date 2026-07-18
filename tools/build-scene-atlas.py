#!/usr/bin/env python3

"""Build the deterministic, padded scene-texture atlas used by both backends."""

from __future__ import annotations

import argparse
import hashlib
import json
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from PIL import Image, ImageChops


GENERATOR_VERSION = 1
DEFAULT_MANIFEST = Path("assets/art/scene-textures.json")
DEFAULT_OUTPUT_DIRECTORY = Path("assets/scene-atlas")
DEFAULT_HEADER = Path("src/render/SceneAtlas.generated.h")


@dataclass(frozen=True)
class Rect:
    x: int
    y: int
    width: int
    height: int

    @property
    def right(self) -> int:
        return self.x + self.width

    @property
    def bottom(self) -> int:
        return self.y + self.height


@dataclass(frozen=True)
class FrameItem:
    texture_index: int
    frame_index: int
    source_rect: Rect
    packed_width: int
    packed_height: int
    stable_key: str


@dataclass(frozen=True)
class Placement:
    item: FrameItem
    page: int
    padded_rect: Rect


@dataclass
class PageState:
    free_rects: list[Rect]
    placements: list[Placement]


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def intersects(left: Rect, right: Rect) -> bool:
    return not (
        left.right <= right.x
        or right.right <= left.x
        or left.bottom <= right.y
        or right.bottom <= left.y
    )


def contains(outer: Rect, inner: Rect) -> bool:
    return (
        inner.x >= outer.x
        and inner.y >= outer.y
        and inner.right <= outer.right
        and inner.bottom <= outer.bottom
    )


def split_free_rect(free: Rect, used: Rect) -> list[Rect]:
    if not intersects(free, used):
        return [free]

    result: list[Rect] = []
    if used.x < free.right and used.right > free.x:
        if used.y > free.y:
            result.append(Rect(free.x, free.y, free.width, used.y - free.y))
        if used.bottom < free.bottom:
            result.append(Rect(free.x, used.bottom, free.width, free.bottom - used.bottom))
    if used.y < free.bottom and used.bottom > free.y:
        if used.x > free.x:
            result.append(Rect(free.x, free.y, used.x - free.x, free.height))
        if used.right < free.right:
            result.append(Rect(used.right, free.y, free.right - used.right, free.height))
    return [rect for rect in result if rect.width > 0 and rect.height > 0]


def prune_free_rects(rects: list[Rect]) -> list[Rect]:
    unique = sorted(set(rects), key=lambda rect: (rect.y, rect.x, rect.height, rect.width))
    return [
        rect
        for index, rect in enumerate(unique)
        if not any(index != other_index and contains(other, rect) for other_index, other in enumerate(unique))
    ]


def place_item(page: PageState, item: FrameItem, page_index: int) -> Placement | None:
    candidates: list[tuple[int, int, int, int, int, int, Rect]] = []
    for free in page.free_rects:
        if item.packed_width <= free.width and item.packed_height <= free.height:
            remaining_width = free.width - item.packed_width
            remaining_height = free.height - item.packed_height
            candidates.append(
                (
                    min(remaining_width, remaining_height),
                    max(remaining_width, remaining_height),
                    free.y,
                    free.x,
                    free.height,
                    free.width,
                    free,
                )
            )
    if not candidates:
        return None

    free = min(candidates)[-1]
    used = Rect(free.x, free.y, item.packed_width, item.packed_height)
    split: list[Rect] = []
    for candidate in page.free_rects:
        split.extend(split_free_rect(candidate, used))
    page.free_rects = prune_free_rects(split)
    placement = Placement(item, page_index, used)
    page.placements.append(placement)
    return placement


def pack_frames(items: list[FrameItem], page_size: int) -> tuple[list[PageState], list[Placement]]:
    sorted_items = sorted(
        items,
        key=lambda item: (
            -max(item.packed_width, item.packed_height),
            -(item.packed_width * item.packed_height),
            -min(item.packed_width, item.packed_height),
            item.stable_key,
            item.frame_index,
        ),
    )
    pages: list[PageState] = []
    placements: list[Placement] = []
    for item in sorted_items:
        if item.packed_width > page_size or item.packed_height > page_size:
            raise ValueError(
                f"{item.stable_key} frame {item.frame_index} is "
                f"{item.packed_width}x{item.packed_height} including padding, "
                f"which exceeds the {page_size}x{page_size} atlas limit"
            )
        placement = None
        for page_index, page in enumerate(pages):
            placement = place_item(page, item, page_index)
            if placement is not None:
                break
        if placement is None:
            page = PageState([Rect(0, 0, page_size, page_size)], [])
            pages.append(page)
            placement = place_item(page, item, len(pages) - 1)
            if placement is None:
                raise RuntimeError(f"Could not place {item.stable_key} despite allocating a new page")
        placements.append(placement)
    return pages, placements


def extrude_frame(frame: Image.Image, padding: int) -> Image.Image:
    if padding <= 0:
        return frame.copy()
    width, height = frame.size
    result = Image.new("RGBA", (width + padding * 2, height + padding * 2), (0, 0, 0, 0))
    result.paste(frame, (padding, padding))
    result.paste(frame.crop((0, 0, 1, height)).resize((padding, height)), (0, padding))
    result.paste(
        frame.crop((width - 1, 0, width, height)).resize((padding, height)),
        (padding + width, padding),
    )
    result.paste(frame.crop((0, 0, width, 1)).resize((width, padding)), (padding, 0))
    result.paste(
        frame.crop((0, height - 1, width, height)).resize((width, padding)),
        (padding, padding + height),
    )
    result.paste(frame.getpixel((0, 0)), (0, 0, padding, padding))
    result.paste(frame.getpixel((width - 1, 0)), (padding + width, 0, padding * 2 + width, padding))
    result.paste(frame.getpixel((0, height - 1)), (0, padding + height, padding, padding * 2 + height))
    result.paste(
        frame.getpixel((width - 1, height - 1)),
        (padding + width, padding + height, padding * 2 + width, padding * 2 + height),
    )
    return result


def load_manifest(repo_root: Path, manifest_path: Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schemaVersion") != 1:
        raise ValueError("Scene texture manifest schemaVersion must be 1")
    maximum_page_size = manifest.get("maxPageSize")
    page_size = manifest.get("packingPageSize", maximum_page_size)
    padding = manifest.get("padding")
    textures = manifest.get("textures")
    if not isinstance(maximum_page_size, int) or maximum_page_size <= 0 or maximum_page_size > 4096:
        raise ValueError("maxPageSize must be an integer in the WebGL-safe range 1..4096")
    if not isinstance(page_size, int) or page_size <= 0 or page_size > maximum_page_size:
        raise ValueError("packingPageSize must be positive and no larger than maxPageSize")
    if not isinstance(padding, int) or padding < 1:
        raise ValueError("padding must be at least one pixel")
    if not isinstance(textures, list) or not textures:
        raise ValueError("textures must be a non-empty array")

    prepared: list[dict[str, Any]] = []
    seen_texture_ids: set[str] = set()
    seen_keys: set[str] = set()
    for texture_index, descriptor in enumerate(textures):
        texture_id = descriptor.get("textureId")
        key = descriptor.get("key")
        source_relative = descriptor.get("source")
        columns = descriptor.get("columns", 1)
        rows = descriptor.get("rows", 1)
        if not all(isinstance(value, str) and value for value in (texture_id, key, source_relative)):
            raise ValueError(f"Texture entry {texture_index} is missing textureId, key, or source")
        if texture_id in seen_texture_ids or key in seen_keys:
            raise ValueError(f"Duplicate scene texture id or key: {texture_id}/{key}")
        if not isinstance(columns, int) or not isinstance(rows, int) or columns <= 0 or rows <= 0:
            raise ValueError(f"{texture_id} columns and rows must be positive integers")
        source_path = repo_root / source_relative
        if not source_path.is_file():
            raise ValueError(f"Scene texture source does not exist: {source_relative}")
        with Image.open(source_path) as opened:
            width, height = opened.size
        if width % columns != 0 or height % rows != 0:
            raise ValueError(
                f"{texture_id} dimensions {width}x{height} are not divisible by its {columns}x{rows} frame grid"
            )
        seen_texture_ids.add(texture_id)
        seen_keys.add(key)
        prepared.append(
            {
                "textureIndex": texture_index,
                "textureId": texture_id,
                "key": key,
                "source": source_relative,
                "sourcePath": source_path,
                "sourceSha256": sha256_bytes(source_path.read_bytes()),
                "width": width,
                "height": height,
                "columns": columns,
                "rows": rows,
                "frameWidth": width // columns,
                "frameHeight": height // rows,
            }
        )
    return manifest, prepared


def create_items(textures: list[dict[str, Any]], padding: int) -> list[FrameItem]:
    items: list[FrameItem] = []
    for texture in textures:
        frame_index = 0
        for row in range(texture["rows"]):
            for column in range(texture["columns"]):
                source_rect = Rect(
                    column * texture["frameWidth"],
                    row * texture["frameHeight"],
                    texture["frameWidth"],
                    texture["frameHeight"],
                )
                items.append(
                    FrameItem(
                        texture["textureIndex"],
                        frame_index,
                        source_rect,
                        source_rect.width + padding * 2,
                        source_rect.height + padding * 2,
                        texture["key"],
                    )
                )
                frame_index += 1
    return items


def build_header(metadata: dict[str, Any]) -> str:
    page_lines = [
        f'    {{"{page["key"]}", "{page["path"]}", {page["width"]}U, {page["height"]}U}},'
        for page in metadata["pages"]
    ]
    frame_lines: list[str] = []
    texture_lines = ["    {}, // TextureId::None"]
    first_frame = 0
    for texture in metadata["textures"]:
        for frame in texture["frames"]:
            rect = frame["atlasRect"]
            frame_lines.append(
                f'    {{{frame["page"]}U, {rect["x"]}U, {rect["y"]}U, '
                f'{rect["width"]}U, {rect["height"]}U}}, // TextureId::{texture["textureId"]} frame {frame["index"]}'
            )
        texture_lines.append(
            f'    {{{texture["width"]}U, {texture["height"]}U, '
            f'{texture["frameWidth"]}U, {texture["frameHeight"]}U, {first_frame}U, '
            f'{texture["columns"]}U, {texture["rows"]}U, {len(texture["frames"])}U}}, '
            f'// TextureId::{texture["textureId"]}'
        )
        first_frame += len(texture["frames"])

    return f'''// Generated by tools/build-scene-atlas.py. Do not edit by hand.
#pragma once

#include "render/ScenePacket.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace rocket {{

struct SceneAtlasPage {{
    const char* key = nullptr;
    const char* relativePath = nullptr;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
}};

struct SceneAtlasFrame {{
    std::uint8_t page = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
}};

struct SceneAtlasTexture {{
    std::uint16_t sourceWidth = 0;
    std::uint16_t sourceHeight = 0;
    std::uint16_t frameWidth = 0;
    std::uint16_t frameHeight = 0;
    std::uint16_t firstFrame = 0;
    std::uint8_t columns = 0;
    std::uint8_t rows = 0;
    std::uint8_t frameCount = 0;
}};

inline constexpr std::array<SceneAtlasPage, {len(metadata["pages"])}U> kSceneAtlasPages {{{{
{chr(10).join(page_lines)}
}}}};

inline constexpr std::array<SceneAtlasFrame, {sum(len(texture["frames"]) for texture in metadata["textures"])}U>
    kSceneAtlasFrames {{{{
{chr(10).join(frame_lines)}
}}}};

inline constexpr std::array<SceneAtlasTexture, {len(metadata["textures"]) + 1}U>
    kSceneAtlasTextures {{{{
{chr(10).join(texture_lines)}
}}}};

static_assert(kSceneAtlasTextures.size() == textureIndex(TextureId::Count));

}} // namespace rocket
'''


def build_atlas(
    repo_root: Path,
    manifest_path: Path,
    output_directory: Path,
    header_path: Path,
) -> dict[str, Any]:
    manifest, textures = load_manifest(repo_root, manifest_path)
    page_size = manifest.get("packingPageSize", manifest["maxPageSize"])
    padding = manifest["padding"]
    items = create_items(textures, padding)
    pages, placements = pack_frames(items, page_size)
    placement_lookup = {
        (placement.item.texture_index, placement.item.frame_index): placement
        for placement in placements
    }
    output_directory.mkdir(parents=True, exist_ok=True)
    header_path.parent.mkdir(parents=True, exist_ok=True)
    for stale in output_directory.glob("scene-atlas-*.png"):
        stale.unlink()

    page_images: list[Image.Image] = []
    page_metadata: list[dict[str, Any]] = []
    for page_index, page in enumerate(pages):
        width = max(placement.padded_rect.right for placement in page.placements)
        height = max(placement.padded_rect.bottom for placement in page.placements)
        page_images.append(Image.new("RGBA", (width, height), (0, 0, 0, 0)))
        page_metadata.append(
            {
                "index": page_index,
                "key": f"scene_atlas_{page_index}",
                "path": f"assets/scene-atlas/scene-atlas-{page_index}.png",
                "width": width,
                "height": height,
            }
        )

    metadata_textures: list[dict[str, Any]] = []
    for texture in textures:
        with Image.open(texture["sourcePath"]) as opened:
            source = opened.convert("RGBA")
        frame_metadata: list[dict[str, Any]] = []
        for frame_index in range(texture["columns"] * texture["rows"]):
            placement = placement_lookup[(texture["textureIndex"], frame_index)]
            source_rect = placement.item.source_rect
            frame = source.crop((source_rect.x, source_rect.y, source_rect.right, source_rect.bottom))
            extruded = extrude_frame(frame, padding)
            page_images[placement.page].paste(
                extruded,
                (placement.padded_rect.x, placement.padded_rect.y),
            )
            atlas_rect = Rect(
                placement.padded_rect.x + padding,
                placement.padded_rect.y + padding,
                source_rect.width,
                source_rect.height,
            )
            frame_metadata.append(
                {
                    "index": frame_index,
                    "page": placement.page,
                    "sourceRect": {
                        "x": source_rect.x,
                        "y": source_rect.y,
                        "width": source_rect.width,
                        "height": source_rect.height,
                    },
                    "atlasRect": {
                        "x": atlas_rect.x,
                        "y": atlas_rect.y,
                        "width": atlas_rect.width,
                        "height": atlas_rect.height,
                    },
                    "paddedRect": {
                        "x": placement.padded_rect.x,
                        "y": placement.padded_rect.y,
                        "width": placement.padded_rect.width,
                        "height": placement.padded_rect.height,
                    },
                }
            )
        metadata_textures.append(
            {
                "textureId": texture["textureId"],
                "key": texture["key"],
                "source": texture["source"],
                "sourceSha256": texture["sourceSha256"],
                "width": texture["width"],
                "height": texture["height"],
                "columns": texture["columns"],
                "rows": texture["rows"],
                "frameWidth": texture["frameWidth"],
                "frameHeight": texture["frameHeight"],
                "frames": frame_metadata,
            }
        )

    for page_index, image in enumerate(page_images):
        page_metadata[page_index]["pixelSha256"] = sha256_bytes(image.tobytes())
        image.save(
            output_directory / f"scene-atlas-{page_index}.png",
            format="PNG",
            optimize=False,
            compress_level=9,
        )

    metadata = {
        "schemaVersion": 1,
        "generatorVersion": GENERATOR_VERSION,
        "maxPageSize": manifest["maxPageSize"],
        "packingPageSize": page_size,
        "padding": padding,
        "pageCount": len(page_images),
        "textureCount": len(metadata_textures),
        "frameCount": len(placements),
        "pages": page_metadata,
        "textures": metadata_textures,
    }
    (output_directory / "scene-atlas.json").write_text(
        json.dumps(metadata, indent=2, ensure_ascii=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    header_path.write_text(build_header(metadata), encoding="utf-8", newline="\n")
    return metadata


def image_pixels_equal(left: Path, right: Path) -> bool:
    with Image.open(left) as left_opened, Image.open(right) as right_opened:
        if left_opened.size != right_opened.size:
            return False
        return ImageChops.difference(left_opened.convert("RGBA"), right_opened.convert("RGBA")).getbbox() is None


def check_outputs(repo_root: Path, manifest_path: Path, output_directory: Path, header_path: Path) -> bool:
    with tempfile.TemporaryDirectory(prefix="orebit-scene-atlas-") as temporary:
        temporary_root = Path(temporary)
        generated_directory = temporary_root / "atlas"
        generated_header = temporary_root / "SceneAtlas.generated.h"
        metadata = build_atlas(repo_root, manifest_path, generated_directory, generated_header)
        expected_text = ["scene-atlas.json"]
        expected_png = [f"scene-atlas-{page['index']}.png" for page in metadata["pages"]]
        failures: list[str] = []
        for name in expected_text:
            committed = output_directory / name
            generated = generated_directory / name
            if not committed.is_file() or committed.read_bytes() != generated.read_bytes():
                failures.append(str(committed))
        if not header_path.is_file() or header_path.read_bytes() != generated_header.read_bytes():
            failures.append(str(header_path))
        for name in expected_png:
            committed = output_directory / name
            generated = generated_directory / name
            if not committed.is_file() or not image_pixels_equal(committed, generated):
                failures.append(str(committed))
        committed_pages = sorted(path.name for path in output_directory.glob("scene-atlas-*.png"))
        if committed_pages != expected_png:
            failures.append(f"atlas page set ({', '.join(committed_pages)})")
        if failures:
            print("Scene atlas outputs are stale or missing:")
            for failure in failures:
                print(f"  {failure}")
            print("Run tools/build-scene-atlas.py to regenerate them.")
            return False
    print("Scene atlas metadata, header, layout, and decoded pixels are current.")
    return True


def main() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--output-directory", type=Path, default=DEFAULT_OUTPUT_DIRECTORY)
    parser.add_argument("--header", type=Path, default=DEFAULT_HEADER)
    parser.add_argument("--check", action="store_true", help="Verify committed outputs without replacing them")
    args = parser.parse_args()

    manifest_path = args.manifest if args.manifest.is_absolute() else repo_root / args.manifest
    output_directory = args.output_directory if args.output_directory.is_absolute() else repo_root / args.output_directory
    header_path = args.header if args.header.is_absolute() else repo_root / args.header
    if args.check:
        raise SystemExit(0 if check_outputs(repo_root, manifest_path, output_directory, header_path) else 1)

    metadata = build_atlas(repo_root, manifest_path, output_directory, header_path)
    dimensions = ", ".join(f"{page['width']}x{page['height']}" for page in metadata["pages"])
    print(
        f"Built {metadata['textureCount']} scene textures / {metadata['frameCount']} frames "
        f"into {metadata['pageCount']} page(s): {dimensions}"
    )


if __name__ == "__main__":
    main()
