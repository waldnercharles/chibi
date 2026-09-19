#!/usr/bin/env python3
"""Compare gameplay captures with recording frames; requires Pillow and NumPy."""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, required=True,
                    help='Directory containing source frames named 00.png, 02.png, etc.')
parser.add_argument('--captures', type=Path, required=True)
args = parser.parse_args()
times = [0, 2, 4, 6, 8, 12, 18, 24]
sheet = Image.new('RGB', (960, 8 * 290), (13, 14, 18))
metrics = []
for index, second in enumerate(times):
    source = Image.open(args.source / f'{second:02d}.png').convert('RGB')
    source = source.crop((1, 33, 1277, 751)).resize((960, 540), Image.Resampling.LANCZOS)
    capture = Image.open(args.captures / f'{second:02d}.png').convert('RGB')
    a, b = np.asarray(source, dtype=float), np.asarray(capture, dtype=float)
    mask = np.ones((540, 960), bool)
    mask[:32, :228] = False
    difference = (a - b)[mask]
    metrics.append(dict(seconds=second,
                        mae_rgb_255=round(float(np.abs(difference).mean()), 3),
                        rmse_rgb_255=round(float(np.sqrt((difference ** 2).mean())), 3)))
    sheet.paste(source.resize((480, 270)), (0, index * 290 + 20))
    sheet.paste(capture.resize((480, 270)), (480, index * 290 + 20))
    ImageDraw.Draw(sheet).text((8, index * 290 + 4),
                              f'{second}s source                                      {second}s sandbox',
                              (220, 220, 230))
sheet.save(args.captures / 'comparison.png')
(args.captures / 'image-metrics.json').write_text(json.dumps(dict(
    method='Source chrome cropped and resized to 960x540; debug strip excluded. '
           'Inputs differ: these are image differences, not matched trajectory error.',
    frames=metrics), indent=2))
print(json.dumps(metrics))
