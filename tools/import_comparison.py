"""Import a Board B CI artifact, checking its immutable source checkout first."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
artifact, checkout, run_id = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
if not re.fullmatch(r'\d+', run_id):
    raise SystemExit('Expected numeric GitHub Actions run ID')
commit = subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], text=True).strip()
manifest = json.loads((artifact / 'manifest.json').read_text())
entry = manifest['images']['car']
for name, expected in entry['source_sha256'].items():
    source = (checkout / name).resolve()
    if not source.is_relative_to(checkout.resolve()):
        raise SystemExit('Source path escapes checkout')
    if hashlib.sha256(source.read_bytes()).hexdigest() != expected:
        raise SystemExit(f'Artifact source differs from checkout: {name}')
    # Also require committed content: a local edit must never be labelled as HEAD.
    committed = subprocess.check_output(['git', '-C', str(checkout), 'show', f'{commit}:{name}'])
    if hashlib.sha256(committed).hexdigest() != expected:
        raise SystemExit(f'Artifact source differs from commit: {name}')
if entry['file'] != 'car-merged.bin':
    raise SystemExit('Unexpected image filename')
data = (artifact / entry['file']).read_bytes()
if len(data) != entry['bytes'] or hashlib.sha256(data).hexdigest() != entry['sha256']:
    raise SystemExit('Artifact checksum mismatch')
manifest['images'] = {'car': entry}
manifest['source_commit'] = commit
manifest['build_url'] = f'https://github.com/thomasgregg/DashBridge/actions/runs/{run_id}'
destination = root / 'dist/idf555'
destination.mkdir(parents=True, exist_ok=True)
(destination / entry['file']).write_bytes(data)
(destination / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(f'Imported {entry["version"]} from {commit}; all source and image hashes verified')
