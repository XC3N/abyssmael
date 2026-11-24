#!/usr/bin/env python3
"""Resolve merge conflicts in a file whose ONLY conflicting hunks are '#define STORE_MAGIC 0x...u' lines: keep the highest.
Exit 1 (file untouched) if any other hunk conflicts."""
import re, sys
p = sys.argv[1]
s = open(p).read()
pat = re.compile(r'<<<<<<< [^\n]*\n(.*?)=======\n(.*?)>>>>>>> [^\n]*\n', re.S)
def pick(m):
    a, b = m.group(1), m.group(2)
    ma, mb = [re.search(r'#define STORE_MAGIC 0x([0-9A-Fa-f]+)u', x) for x in (a, b)]
    if not (ma and mb) or a.count('\n') != 1 or b.count('\n') != 1:
        raise ValueError
    return a if int(ma.group(1), 16) >= int(mb.group(1), 16) else b
try:
    t = pat.sub(pick, s)
except ValueError:
    sys.exit(1)
open(p, 'w').write(t)
