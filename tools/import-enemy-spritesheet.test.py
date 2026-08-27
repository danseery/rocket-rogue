#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw


SCRIPT = Path(__file__).with_name("import-enemy-spritesheet.py")
SPEC = importlib.util.spec_from_file_location("enemy_importer", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def main() -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        source = root / "source.png"
        destination = root / "runtime.png"
        contact = Image.new("RGBA", (400, 500), (255, 0, 170, 255))
        draw = ImageDraw.Draw(contact)
        for row in range(5):
            for column in range(4):
                left = column * 100 + 22
                top = row * 100 + 20
                draw.rectangle((left, top, left + 55 + column, top + 63 - row), fill=(120, 80, 40, 255))
        contact.save(source)
        MODULE.import_enemy_sheet(source, destination, floating=False)
        runtime = Image.open(destination).convert("RGBA")
        assert runtime.size == (2560, 128)
        assert runtime.getpixel((0, 0))[3] == 0
        for index in range(20):
            assert runtime.crop((index * 128, 0, (index + 1) * 128, 128)).getbbox() is not None
        print("enemy spritesheet importer test passed")


if __name__ == "__main__":
    main()
