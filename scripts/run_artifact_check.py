"""Check exported integrity and disclosure scope; never launch research workloads."""
import collections
import csv
import hashlib
import json
from pathlib import Path, PurePosixPath
import re

R = Path(__file__).resolve().parents[1]

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def inside(relative):
    rel = PurePosixPath(relative)
    if rel.is_absolute() or '..' in rel.parts or '\\' in relative or ':' in rel.parts[0]:
        raise ValueError('Non-portable manifest path')
    p = R.joinpath(*rel.parts)
    if p.is_symlink() or not p.resolve().is_relative_to(R.resolve()) or not p.is_file():
        raise ValueError('Missing or escaping manifest entry: ' + relative)
    return p

manifest = json.loads((R / 'artifact_manifest.json').read_text(encoding='utf-8'))
if not __debug__:
    raise RuntimeError('Integrity checks require Python assertions enabled; omit -O')
expected_sums=''.join(x['sha256']+'  '+x['path']+'\n' for x in manifest['files'])
assert (R/'SHA256SUMS').read_text(encoding='utf-8')==expected_sums, 'SHA256SUMS differs from manifest'
seen = set()
for item in manifest['files']:
    assert item['path'] not in seen, 'Duplicate manifest entry'
    seen.add(item['path'])
    p = inside(item['path'])
    assert digest(p) == item['sha256'], item['path']
    assert p.stat().st_size == item['bytes'], item['path']
actual=set()
for p in R.rglob('*'):
    if not p.is_file():continue
    rel=p.relative_to(R)
    if any(k in rel.parts for k in ('build','outputs','__pycache__','.git','.venv','external_inputs')):continue
    if p.suffix=='.pyc' or rel.as_posix() in ('artifact_manifest.json','SHA256SUMS'):continue
    actual.add(rel.as_posix())
assert actual==seen, 'Unlisted or missing release files: '+repr(sorted(actual.symmetric_difference(seen)))
for x in json.loads((R / 'data/lineage.json').read_text(encoding='utf-8')):
    assert digest(inside(x['bundled_path'])) == x['source_sha256']
for x in json.loads((R / 'provenance/source_lineage.json').read_text(encoding='utf-8')):
    assert digest(inside(x['path'])) == x['export_sha256'], x['path']

with (R / 'data/formal_windows.csv').open(encoding='utf-8-sig') as f:
    rows = list(csv.DictReader(f))
expected = {'P1':84,'P2':90,'P3':36,'P4':9,'C1':16,'C2':24,'C7':9,'C8':36,'C9':18}
assert collections.Counter(r['group'] for r in rows) == expected
assert len({(r['group'], r['profile'], r['policy'], r['run_id']) for r in rows}) == 322
# Manuscript and bibliography are checked in the separate article build.

bad = []
# Do not emit matched values into output; report file IDs only.
secret_patterns = [r'-----BEGIN [A-Z ]*PRIVATE KEY-----',
                   r'gh[pousr]_[A-Za-z0-9]{25,}', r'github_pat_[A-Za-z0-9_]{30,}',
                   r'AKIA[0-9A-Z]{16}',
                   r'(?im)^\s*(?:password|passwd|ssh_password|access_token)\s*[:=]\s*[\"\x27][^\"\x27\r\n]{3,}[\"\x27]']
for item in manifest['files']:
    p = inside(item['path'])
    if p.suffix.lower() in ('.pem', '.key') or '.ssh' in p.parts:
        bad.append(item['path'])
    if p.suffix.lower() not in ('.pdf', '.png', '.svg'):
        t = p.read_text(encoding='utf-8', errors='ignore')
        if any(re.search(pattern, t) for pattern in secret_patterns):
            bad.append(item['path'])
assert not bad, sorted(set(bad))
out = R / 'outputs'
out.mkdir(exist_ok=True)
result = {'status':'PASS_LOCAL_ARTIFACT_INTEGRITY', 'files_verified':len(seen),
          'formal_windows':322, 'manuscript_included':False,
          'new_scientific_runs':0, 'public_upload_completed':False,
          'author_added_code_license':'MIT',
          'scope':'Checksums, complete inclusion, source/data lineage, formal-window identities and bounded secret-pattern scan; not manuscript citation review or performance qualification.'}
(out / 'artifact_check.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
print(json.dumps(result, indent=2))
