"""Single offline evidence-replay entry point. No native executables are run."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bootstrap', action='store_true')
    args = parser.parse_args()
    if not __debug__:
        parser.error('Run without Python -O; integrity assertions must remain enabled')
    commands = [('run_artifact_check.py', []),
                ('check_entrypoints.py', []),
                ('reproduce_tables.py', ['--bootstrap'] if args.bootstrap else []),
                ('reproduce_accounting.py', []),
                ('reproduce_sensitivity.py', []),
                ('reproduce_diagnostics.py', [])]
    for script, extra in commands:
        subprocess.run([sys.executable, '-B', str(ROOT / 'scripts' / script), *extra],
                       cwd=ROOT, check=True)
    report = {'status': 'PASS_LOCAL_EVIDENCE_REPLAY', 'formal_windows': 322,
              'accepted_ann_windows_in_study': 421,
              'independent_diagnostic_processes_or_scenes': 38,
              'bootstrap_requested': args.bootstrap,
              'new_scientific_runs': 0, 'remote_uploads': 0,
              'scope': 'Archived numerical replay, not new performance qualification.'}
    (ROOT / 'outputs/REPRODUCTION_REPORT.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
