#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import importlib.util
import json
import re
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image


REPO_ROOT = Path(__file__).resolve().parents[1]
GENERATOR_PATH = REPO_ROOT / "tools/build-scene-atlas.py"
SPEC = importlib.util.spec_from_file_location("build_scene_atlas", GENERATOR_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"Could not load {GENERATOR_PATH}")
GENERATOR = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = GENERATOR
SPEC.loader.exec_module(GENERATOR)


def rect_from_json(value: dict[str, int]) -> tuple[int, int, int, int]:
    return (
        value["x"],
        value["y"],
        value["x"] + value["width"],
        value["y"] + value["height"],
    )


class SceneAtlasTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.manifest = json.loads(
            (REPO_ROOT / "assets/art/scene-textures.json").read_text(encoding="utf-8")
        )
        cls.metadata = json.loads(
            (REPO_ROOT / "assets/scene-atlas/scene-atlas.json").read_text(encoding="utf-8")
        )

    def test_manifest_matches_runtime_texture_id_order(self) -> None:
        source = (REPO_ROOT / "src/render/ScenePacket.h").read_text(encoding="utf-8")
        match = re.search(r"enum class TextureId\s*:[^{]+\{(?P<body>.*?)\};", source, re.DOTALL)
        self.assertIsNotNone(match)
        runtime_ids = [
            identifier
            for identifier in re.findall(r"^\s*(\w+)(?:\s*=\s*\d+)?\s*,?\s*$", match.group("body"), re.MULTILINE)
            if identifier not in {"None", "Count"}
        ]
        manifest_ids = [texture["textureId"] for texture in self.manifest["textures"]]
        self.assertEqual(runtime_ids, manifest_ids)

    def test_generated_metadata_covers_every_source_frame(self) -> None:
        self.assertEqual(self.metadata["textureCount"], len(self.manifest["textures"]))
        self.assertEqual(self.metadata["pageCount"], len(self.metadata["pages"]))
        frame_count = 0
        for declared, generated in zip(
            self.manifest["textures"], self.metadata["textures"], strict=True
        ):
            self.assertEqual(generated["textureId"], declared["textureId"])
            self.assertEqual(generated["key"], declared["key"])
            self.assertEqual(generated["source"], declared["source"])
            source_path = REPO_ROOT / declared["source"]
            self.assertEqual(
                generated["sourceSha256"],
                hashlib.sha256(source_path.read_bytes()).hexdigest(),
            )
            self.assertEqual(len(generated["frames"]), declared["columns"] * declared["rows"])
            frame_count += len(generated["frames"])
            for frame_index, frame in enumerate(generated["frames"]):
                self.assertEqual(frame["index"], frame_index)
                source_rect = frame["sourceRect"]
                expected_column = frame_index % declared["columns"]
                expected_row = frame_index // declared["columns"]
                self.assertEqual(source_rect["x"], expected_column * generated["frameWidth"])
                self.assertEqual(source_rect["y"], expected_row * generated["frameHeight"])
                self.assertEqual(source_rect["width"], generated["frameWidth"])
                self.assertEqual(source_rect["height"], generated["frameHeight"])
        self.assertEqual(self.metadata["frameCount"], frame_count)

    def test_padded_rectangles_do_not_overlap_or_exceed_webgl_limit(self) -> None:
        page_rects: dict[int, list[tuple[int, int, int, int]]] = {
            page["index"]: [] for page in self.metadata["pages"]
        }
        pages = {page["index"]: page for page in self.metadata["pages"]}
        for texture in self.metadata["textures"]:
            for frame in texture["frames"]:
                page = pages[frame["page"]]
                padded = rect_from_json(frame["paddedRect"])
                self.assertGreaterEqual(padded[0], 0)
                self.assertGreaterEqual(padded[1], 0)
                self.assertLessEqual(padded[2], page["width"])
                self.assertLessEqual(padded[3], page["height"])
                for other in page_rects[frame["page"]]:
                    overlaps = not (
                        padded[2] <= other[0]
                        or other[2] <= padded[0]
                        or padded[3] <= other[1]
                        or other[3] <= padded[1]
                    )
                    self.assertFalse(overlaps, f"Atlas rectangles overlap: {padded} and {other}")
                page_rects[frame["page"]].append(padded)
        for page in self.metadata["pages"]:
            self.assertLessEqual(page["width"], self.manifest["maxPageSize"])
            self.assertLessEqual(page["height"], self.manifest["maxPageSize"])
            self.assertLessEqual(page["width"], self.manifest["packingPageSize"])
            self.assertLessEqual(page["height"], self.manifest["packingPageSize"])

    def test_committed_pages_preserve_pixels_and_extrude_edges(self) -> None:
        padding = self.metadata["padding"]
        pages: dict[int, Image.Image] = {}
        try:
            for descriptor in self.metadata["pages"]:
                page = Image.open(REPO_ROOT / descriptor["path"]).convert("RGBA")
                pages[descriptor["index"]] = page
                self.assertEqual(page.size, (descriptor["width"], descriptor["height"]))
                self.assertEqual(hashlib.sha256(page.tobytes()).hexdigest(), descriptor["pixelSha256"])

            for texture in self.metadata["textures"]:
                with Image.open(REPO_ROOT / texture["source"]) as opened:
                    source = opened.convert("RGBA")
                for frame in texture["frames"]:
                    page = pages[frame["page"]]
                    source_rect = rect_from_json(frame["sourceRect"])
                    atlas_rect = rect_from_json(frame["atlasRect"])
                    expected = source.crop(source_rect)
                    actual = page.crop(atlas_rect)
                    self.assertEqual(actual.tobytes(), expected.tobytes())

                    left = page.crop(
                        (atlas_rect[0] - padding, atlas_rect[1], atlas_rect[0], atlas_rect[3])
                    )
                    right = page.crop(
                        (atlas_rect[2], atlas_rect[1], atlas_rect[2] + padding, atlas_rect[3])
                    )
                    top = page.crop(
                        (atlas_rect[0], atlas_rect[1] - padding, atlas_rect[2], atlas_rect[1])
                    )
                    bottom = page.crop(
                        (atlas_rect[0], atlas_rect[3], atlas_rect[2], atlas_rect[3] + padding)
                    )
                    self.assertEqual(
                        left.tobytes(),
                        expected.crop((0, 0, 1, expected.height)).resize(left.size).tobytes(),
                    )
                    self.assertEqual(
                        right.tobytes(),
                        expected.crop((expected.width - 1, 0, expected.width, expected.height))
                        .resize(right.size)
                        .tobytes(),
                    )
                    self.assertEqual(
                        top.tobytes(),
                        expected.crop((0, 0, expected.width, 1)).resize(top.size).tobytes(),
                    )
                    self.assertEqual(
                        bottom.tobytes(),
                        expected.crop((0, expected.height - 1, expected.width, expected.height))
                        .resize(bottom.size)
                        .tobytes(),
                    )
        finally:
            for page in pages.values():
                page.close()

    def test_generator_is_deterministic_for_split_sprite_sheets(self) -> None:
        with tempfile.TemporaryDirectory(prefix="orebit-atlas-determinism-") as temporary:
            temporary_root = Path(temporary)
            source_directory = temporary_root / "assets/art"
            source_directory.mkdir(parents=True)
            source = Image.new("RGBA", (8, 4))
            source.putdata(
                [
                    ((x * 31) % 256, (y * 63) % 256, ((x + y) * 29) % 256, 255)
                    for y in range(source.height)
                    for x in range(source.width)
                ]
            )
            source.save(source_directory / "sheet.png")
            manifest = {
                "schemaVersion": 1,
                "maxPageSize": 16,
                "packingPageSize": 16,
                "padding": 2,
                "textures": [
                    {
                        "textureId": "TestSheet",
                        "key": "test_sheet",
                        "source": "assets/art/sheet.png",
                        "columns": 2,
                        "rows": 1,
                    }
                ],
            }
            manifest_path = source_directory / "scene-textures.json"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            outputs: list[dict[str, bytes]] = []
            for run in range(2):
                output_directory = temporary_root / f"output-{run}"
                header = temporary_root / f"SceneAtlas-{run}.generated.h"
                GENERATOR.build_atlas(temporary_root, manifest_path, output_directory, header)
                outputs.append(
                    {
                        path.name: path.read_bytes()
                        for path in sorted(output_directory.iterdir())
                    }
                    | {"header": header.read_bytes()}
                )
            self.assertEqual(outputs[0], outputs[1])


if __name__ == "__main__":
    unittest.main()
