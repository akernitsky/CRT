"""Package real app captures for README. Requires Pillow; does not capture UI."""

import argparse
import json
from pathlib import Path

from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frames", type=Path)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--output", type=Path, default=Path("docs/media"))
    args = parser.parse_args()
    recipe = json.loads(args.manifest.read_text(encoding="utf-8"))
    args.output.mkdir(parents=True, exist_ok=True)

    def load(name, width):
        with Image.open(args.frames / name) as source:
            image = source.convert("RGB")
        if recipe.get("crop"):
            image = image.crop(tuple(recipe["crop"]))
        height = round(image.height * width / image.width)
        return image.resize((width, height), Image.Resampling.LANCZOS)

    for output, source in recipe["screenshots"].items():
        load(source, 1200).save(args.output / output, optimize=True)

    frames = [load(entry["file"], 800).quantize(colors=128) for entry in recipe["animation"]]
    durations = [entry["duration_ms"] for entry in recipe["animation"]]
    frames[0].save(args.output / "demo.gif", save_all=True, append_images=frames[1:],
                   duration=durations, loop=0, disposal=2, optimize=True)
    with Image.open(args.output / "demo.gif") as preview:
        print(f"GIF: {preview.n_frames} frames, {preview.size}, "
              f"{(args.output / 'demo.gif').stat().st_size:,} bytes")


if __name__ == "__main__":
    main()
