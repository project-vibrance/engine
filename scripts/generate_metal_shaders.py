#!/usr/bin/env python3
"""Regenerate native MSL from the shared shader maths (maintainer tool only).
Requires glslc and SPIRV-Cross; normal macOS builds use the checked-in MSL.
"""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
destination = root / 'engine/src/renderer/metal/shaders'
destination.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory() as temporary:
    for source in sorted((root / 'engine/shaders').iterdir()):
        if source.suffix not in ('.comp', '.vert', '.frag') or source.stem == 'rasterise_small':
            continue
        binary = Path(temporary) / 'shader.spv'
        subprocess.run(['glslc', '--target-env=vulkan1.3', '-O0', str(source), '-o', str(binary)], check=True)
        stage = {'comp': 'comp', 'vert': 'vert', 'frag': 'frag'}[source.suffix[1:]]
        entry = source.stem + '_' + stage
        subprocess.run(['spirv-cross', str(binary), '--msl', '--msl-version', '30000',
            '--rename-entry-point', 'main', entry, stage, '--output', str(destination / (entry + '.metal'))], check=True)
