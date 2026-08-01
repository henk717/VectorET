#!/usr/bin/env python3
"""Decide whether a run's warnings are trim damage or upstream noise.

Paste the engine console into a file (or pipe it in) and this pulls out every
asset named in a warning, then asks the only question that matters: does that
asset exist in the *stock* paks?

  - absent from stock too -> upstream ET:Legacy, nothing we did
  - in stock but not in the trimmed pak -> a trim regression, fix it

Written because grepping for a few hand-picked warning patterns missed real
regressions (server models and MDC shaders warn in wordings that "not found"
does not match). Check every warning line, not the ones you thought of.

  ./scripts/check-warnings.py run.log
"""

import glob
import os
import re
import sys
import zipfile

IMG = ('.tga', '.jpg', '.jpeg', '.png')
WARN_RE = re.compile(r'warning|failed|not found|does not exist|no valid file',
                     re.IGNORECASE)
QUOTED_RE = re.compile(r"'([A-Za-z0-9_\-/\.]+)'")
ANSI_RE = re.compile(r'\x1b?\[[0-9;]*m')

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')


def load(patterns):
    out = set()
    for pat in patterns:
        for p in glob.glob(os.path.join(ROOT, pat)):
            out |= {n.lower() for n in zipfile.ZipFile(p).namelist()}
    return out


def main():
    text = open(sys.argv[1]).read() if len(sys.argv) > 1 else sys.stdin.read()

    stock = load(['assets/etmain/*.pk3', 'build/web/legacy/legacy_*.pk3'])
    trimmed = load(['assets-trimmed/etmain/*.pk3', 'build/web/legacy/legacy_*.pk3'])
    if not stock:
        sys.exit('no stock paks under assets/etmain - nothing to compare against')

    named = set()
    warn_lines = 0
    for raw in text.splitlines():
        line = ANSI_RE.sub('', raw)
        if not WARN_RE.search(line):
            continue
        warn_lines += 1
        for q in QUOTED_RE.findall(line):
            if '/' in q:
                named.add(q.lower())

    print(f'{warn_lines} warning lines, {len(named)} naming an asset\n')
    regressions = []
    for a in sorted(named):
        cands = [a] + [a + e for e in IMG]
        in_stock = any(c in stock for c in cands)
        in_trim = any(c in trimmed for c in cands)
        if not in_stock:
            verdict = 'upstream (absent from stock too)'
        elif in_trim:
            verdict = 'present - warning is not about a missing file'
        else:
            verdict = '*** TRIM REGRESSION ***'
            regressions.append(a)
        print(f'  {a:<46}{verdict}')

    print()
    if regressions:
        print(f'{len(regressions)} trim regression(s); add the directory to '
              f'ALWAYS_PREFIXES or seed the walk from whatever references it')
        return 1
    print('no trim regressions')
    return 0


if __name__ == '__main__':
    sys.exit(main())
