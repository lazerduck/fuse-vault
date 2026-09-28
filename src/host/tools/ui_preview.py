#!/usr/bin/env python3
"""Render synthetic UI states with the device's C renderer, without USB.

Requires a host C compiler and Pillow. Writes PPMs, a PNG contact sheet, and an
HTML gallery with enlarged and native-size screens. No real credentials are used.
"""
import argparse
import html
import re
from pathlib import Path
import subprocess
import tempfile

from PIL import Image, ImageDraw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('/tmp/fuse-vault-ui-preview'))
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parents[3]
    ui = root / 'src/shared/ui'
    manifest = (ui / 'CMakeLists.txt').read_text().split('add_library(fv_ui STATIC', 1)[1].split(')', 1)[0]
    sources = [str(ui / name) for name in re.findall(r'[\w/]+\.c', manifest)]
    with tempfile.TemporaryDirectory(prefix='fv-ui-preview-') as scratch:
        binary = Path(scratch) / 'preview'
        subprocess.run([
            'cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
            '-I', str(root / 'src/shared/ui'), str(root / 'src/host/tools/ui_preview.c'),
            *sources,
            '-o', str(binary),
        ], check=True)
        subprocess.run([str(binary)], cwd=output, check=True)
    frames = sorted(output.glob('*.ppm'))
    sheet = Image.new('RGB', (1020, ((len(frames) + 2) // 3) * 200), '#20292b')
    draw = ImageDraw.Draw(sheet)
    cards = []
    for index, path in enumerate(frames):
        with Image.open(path) as source:
            screen = source.convert('RGB')
        screen.save(path.with_suffix('.png'))
        x, y = (index % 3) * 340 + 10, (index // 3) * 200
        draw.text((x, y + 8), path.stem, fill='white')
        sheet.paste(screen.resize((320, 160), Image.Resampling.NEAREST), (x, y + 28))
        name = html.escape(path.with_suffix('.png').name, quote=True)
        cards.append(f'<figure><figcaption>{html.escape(path.stem)}</figcaption>'
                     f'<img width="480" height="240" src="{name}">'
                     f'<img width="160" height="80" src="{name}"></figure>')
    sheet.save(output / 'contact-sheet.png')
    (output / 'index.html').write_text(
        '<!doctype html><html lang="en"><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width,initial-scale=1">'
        '<title>Fuse Vault offline UI review</title>'
        '<style>body{background:#20292b;color:#ecf5f1;font:16px sans-serif;margin:24px}'
        'main{display:flex;flex-wrap:wrap;gap:24px}figure{margin:0;max-width:100%}'
        'img{display:block;image-rendering:pixelated;max-width:100%;object-fit:contain;'
        'object-position:left;margin:12px 0}figcaption{font-weight:bold}</style>'
        '<h1>Fuse Vault UI review</h1><p>Synthetic states, actual 160 × 80 firmware '
        'renderer. Enlarged and native-size previews; no device connection.</p><main>'
        + ''.join(cards) + '</main></html>', encoding='utf-8')
    print(output / 'index.html')
    print(output / 'contact-sheet.png')


if __name__ == '__main__':
    main()
