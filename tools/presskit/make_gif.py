#!/usr/bin/env python3
# BLOCKWAVE — square-wave-only synthesizer
# Copyright (C) 2026 Kirill Boyko
# Licensed under the GNU General Public License v3 or later.
#
# Assembles the press-kit discovery GIF from the PNG frame sequence written by
# `blockwave_presskit frames`. Reads frames.txt (one "<file> <hold-ms>" line per
# frame) so the GIF keeps the tool's per-frame timing: one tick per frame while
# a pixel animation plays, long holds on the states a reader has to read.
#
# Pixel-art rules. The palette is the EXACT set of colours the UI drew (the
# whole CRAFT screen uses well under 256), mapped index-for-index — no
# quantiser, no dithering, no resampling. Every output pixel is bit-identical
# to the rendered frame, which the script asserts before writing.
#
# Only dependency is Pillow.
#
#   python3 tools/presskit/make_gif.py <framesDir> <out.gif>

import sys
from pathlib import Path

from PIL import Image


def load_manifest(frames_dir: Path):
    manifest = frames_dir / "frames.txt"
    if not manifest.exists():
        sys.exit(f"no manifest at {manifest} — run `blockwave_presskit frames` first")
    entries = []
    for line in manifest.read_text().splitlines():
        line = line.strip()
        if not line:
            continue
        name, ms = line.rsplit(" ", 1)
        path = frames_dir / name
        if not path.exists():
            sys.exit(f"manifest lists a missing frame: {path}")
        # GIF stores delays in centiseconds; round here so the written file
        # plays at exactly the timing this script reports.
        entries.append((path, max(20, int(round(int(ms) / 10.0)) * 10)))
    if not entries:
        sys.exit("manifest is empty")
    return entries


def to_indexed(img: Image.Image, lut: dict, palette: list) -> Image.Image:
    out = Image.frombytes("P", img.size, bytes(map(lut.__getitem__, img.getdata())))
    out.putpalette(palette)
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: make_gif.py <framesDir> <out.gif>")
    frames_dir, out = Path(sys.argv[1]), Path(sys.argv[2])
    entries = load_manifest(frames_dir)

    rgb = [Image.open(p).convert("RGB") for p, _ in entries]
    durations = [ms for _, ms in entries]
    size = rgb[0].size
    if any(im.size != size for im in rgb):
        sys.exit("frames are not all the same size")

    colours = sorted({c for im in rgb for _, c in im.getcolors(1 << 24)})
    print(f"{len(rgb)} frames, {size[0]}x{size[1]}, {len(colours)} distinct colours")
    if len(colours) > 256:
        sys.exit("more than 256 distinct colours — a lossless GIF is impossible; "
                 "reduce the captured area or accept a quantised palette")

    lut = {c: i for i, c in enumerate(colours)}
    flat = [v for c in colours for v in c] + [0] * (768 - 3 * len(colours))
    frames = [to_indexed(im, lut, flat) for im in rgb]

    for i, (a, b) in enumerate(zip(frames, rgb)):
        if a.convert("RGB").tobytes() != b.tobytes():
            sys.exit(f"frame {i} did not round-trip losslessly")

    out.parent.mkdir(parents=True, exist_ok=True)
    frames[0].save(out, save_all=True, append_images=frames[1:],
                   duration=durations, loop=0, optimize=True, disposal=1)

    total = sum(durations)
    written = Image.open(out)
    print(f"wrote {out}")
    print(f"  {out.stat().st_size / 1024:.0f} KB, {written.n_frames} stored frames "
          f"(identical neighbours merged), {total} ms / {total / 1000:.1f} s, loops forever")


if __name__ == "__main__":
    main()
