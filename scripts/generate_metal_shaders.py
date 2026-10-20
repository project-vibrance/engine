#!/usr/bin/env python3
"""Regenerate native MSL from the shared shader maths (maintainer tool only).
Requires glslc and SPIRV-Cross; normal macOS builds use the checked-in MSL.
"""
from pathlib import Path
import argparse
import re
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[1]
destination = root / 'engine/src/renderer/metal/shaders'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--shaders', nargs='+', help='Shared shader stems to regenerate (for example shape_2d text_msdf)')
parser.add_argument('--check', action='store_true', help='Fail if selected checked-in shaders differ from generated source')
args = parser.parse_args()
sources = [source for source in sorted((root / 'engine/shaders').iterdir())
    if source.suffix in ('.comp', '.vert', '.frag') and source.stem != 'rasterise_small']
if args.shaders:
    unknown = set(args.shaders) - {source.stem for source in sources}
    if unknown:
        parser.error('Unknown shared shaders: ' + ', '.join(sorted(unknown)))
    sources = [source for source in sources if source.stem in args.shaders]
destination.mkdir(exist_ok=True)
changed = []
with tempfile.TemporaryDirectory() as temporary:
    for source in sources:
        binary = Path(temporary) / 'shader.spv'
        subprocess.run(['glslc', '--target-env=vulkan1.3', '-DVIBRANCE_METAL=1', '-O0', str(source), '-o', str(binary)], check=True)
        stage = {'comp': 'comp', 'vert': 'vert', 'frag': 'frag'}[source.suffix[1:]]
        entry = source.stem + '_' + stage
        generated = Path(temporary) / (entry + '.metal')
        subprocess.run(['spirv-cross', str(binary), '--msl', '--msl-version', '30000',
            '--rename-entry-point', 'main', entry, stage, '--output', str(generated)], check=True)
        target = destination / generated.name
        content = generated.read_text().rstrip() + '\n'
        # MSL imports metal::primitive_type through metal_stdlib. The shared
        # GLSL helper has the same name; qualify it with a project prefix so
        # regenerated shaders compile with current Apple SDKs.
        content = re.sub(r'\bprimitive_type\b', 'vibrance_primitive_type', content)
        if args.check:
            if not target.exists() or target.read_text() != content:
                changed.append(target.name)
        else:
            target.write_text(content)
if changed:
    print('Outdated Metal shaders: ' + ', '.join(changed), file=sys.stderr)
    sys.exit(1)
