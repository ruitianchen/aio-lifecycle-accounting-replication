"""Explicit Linux-only quota experiment adapter; not called by offline replay."""
import argparse
from pathlib import Path
import sys
from execution_support import settings, save
import resource_diagnostics_fixed

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',required=True,type=Path)
    p.add_argument('--execute',action='store_true')
    a=p.parse_args()
    if not a.execute:
        print('Plan only: 1/4/5 processes x 3 scenes, 13 contexts per process, depth 1024; explicit --execute required.')
        return
    if sys.platform!='linux':p.error('Native Linux required')
    out=a.output.resolve()
    if out.exists():p.error('Output exists; retain it and choose a new authorized run directory')
    out.mkdir(parents=True)
    expected=settings()
    save(out/'portable_adapter_settings.json',expected)
    records=resource_diagnostics_fixed.run(out,expected)
    save(out/'portable_adapter_results.json',records)
    print('New quota scenes completed; these are not part of the archived performance matrix.')

if __name__=='__main__':
    main()
