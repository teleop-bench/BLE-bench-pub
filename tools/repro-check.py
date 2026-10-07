#!/usr/bin/env python3
"""A-Z mechanical check of REPRODUCE.md recipes vs the actual repo.
Catches the surprise classes we've already hit: missing app/overlay/hex, and
-DCONFIG_APP_* symbols the target app doesn't define (=> Kconfig hard-abort)."""
import os, re, glob

import os
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
txt = open(REPO + '/REPRODUCE.md').read()
lines = txt.splitlines()

def app_kconfig_syms(appdir):
    syms = set()
    for kf in glob.glob(os.path.join(appdir, 'Kconfig*')):
        for l in open(kf):
            m = re.match(r'\s*config\s+([A-Z0-9_]+)', l)
            if m: syms.add(m.group(1))
    return syms

problems = []
recipes = 0
for ln, line in enumerate(lines, 1):
    s = line.strip()
    if not s.startswith('west build'):
        continue
    recipes += 1
    # app path
    ma = re.search(r'(apps/[\w./-]+)', s)
    app = ma.group(1) if ma else None
    appdir = os.path.join(REPO, app) if app else None
    if not app:
        problems.append((ln, 'NO_APP', s[:80])); continue
    if not os.path.isdir(appdir):
        problems.append((ln, 'MISSING_APP', app)); continue
    # overlays in EXTRA_CONF_FILE
    me = re.search(r'EXTRA_CONF_FILE="([^"]+)"', s)
    if me:
        for ov in me.group(1).split(';'):
            ov = ov.strip()
            if not ov or '$' in ov:   # skip shell-var overlays (e.g. "$OV;eatt.conf")
                continue
            if not os.path.isfile(os.path.join(appdir, ov)):
                problems.append((ln, 'MISSING_OVERLAY', f'{app} <- {ov}'))
    # -DCONFIG_APP_* symbols must be defined by the app
    syms = app_kconfig_syms(appdir)
    for msym in re.finditer(r'-DCONFIG_(APP_[A-Z0-9_]+)=', s):
        sym = msym.group(1)
        if sym not in syms:
            problems.append((ln, 'UNDEFINED_APP_SYM', f'{app}: -D{sym} (ABORTS BUILD)'))

# prebuilt hex references: flag only strict top-level c-/p- names absent from the WHOLE prebuilt tree
hexrefs = set(re.findall(r'`(c-[\w-]+|p-[\w-]+)`', txt))
have = {os.path.basename(h)[:-4] for h in glob.glob(REPO + '/prebuilt-hexes/**/*.hex', recursive=True)}
for h in sorted(hexrefs):
    if h not in have:
        problems.append((0, 'HEX_REF_NOT_IN_PREBUILT', h))

print(f'recipes scanned: {recipes}')
print(f'problems: {len(problems)}\n')
for ln, kind, detail in problems:
    print(f'  L{ln:<4} {kind:<24} {detail}')
