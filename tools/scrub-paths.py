#!/usr/bin/env python3
"""Reversible scrub of the author's local filesystem paths in archived evidence.

Archived configs, harnesses and logs under debug-evidence/ were recorded on the author's machine and
contained absolute paths (e.g. the Zephyr workspace under the author's home directory). For
publication those prefixes were replaced with the placeholders in MAPPING below. The substitution is
deterministic and exactly reversible (none of the placeholders occurred in any file beforehand), so
hashes recorded in manifests / PROVENANCE files still verify against the *un-scrubbed* bytes:

    python3 tools/scrub-paths.py sha256 <file> [...]   # sha256 of the original (pre-scrub) bytes
    python3 tools/scrub-paths.py unscrub <file>        # original bytes to stdout
    python3 tools/scrub-paths.py scrub <file> [...]    # apply the scrub in place (round-trip checked)
    python3 tools/scrub-paths.py unscrub-tree <src-dir> <dst-dir>   # copy a tree, restoring originals
    python3 tools/scrub-paths.py verify <SHA256SUMS> [...]  # check every listed file (see below)

`verify` accepts a file when its recorded hash matches the raw bytes, the un-scrubbed bytes, or the
un-scrubbed bytes with `$HOME` reversed: an earlier history rewrite (2026-09-05) replaced the home
directory with the literal `$HOME` in some archived text files (no placeholder), so those files only
verify that way. Listed paths may be relative to the SHA256SUMS file or to the repository root.

Tools that re-hash archived files byte for byte (e.g. the observer ABBA combiners in
apps/misc/q2-central/, which re-verify firmware/*.config against each cell's manifest) must be run
on an `unscrub-tree` copy, not on the scrubbed files in place.
"""
import hashlib
import os
import shutil
import sys

ORIG_HOME = '/Users/e'
# Longest prefix first; the final catch-all covers any remaining home-relative path.
MAPPING = [
    ('/private/tmp/claude-501/-Users-e-Desktop-develop-zenoh-pico-ble-test', '<SCRATCH>'),
    (ORIG_HOME + '/Desktop/develop/zenoh-pico-ble-test', '<REPO>'),
    (ORIG_HOME + '/zephyrproject', '<ZEPHYR_WS>'),
    (ORIG_HOME + '/ncs', '<NCS_WS>'),
    (ORIG_HOME + '/zephyr-sdk-1.0.1', '<ZEPHYR_SDK>'),
    (ORIG_HOME + '/', '<HOME>/'),
]


def scrub(b: bytes) -> bytes:
    for orig, ph in MAPPING:
        b = b.replace(orig.encode(), ph.encode())
    return b


def unscrub(b: bytes) -> bytes:
    for orig, ph in MAPPING:
        b = b.replace(ph.encode(), orig.encode())
    return b


def main(argv):
    if len(argv) == 4 and argv[1] == 'unscrub-tree':
        src, dst = argv[2], argv[3]
        if os.path.exists(dst):
            sys.exit(f'refusing: {dst} already exists')
        shutil.copytree(src, dst)
        for root, _, names in os.walk(dst):
            for n in names:
                f = os.path.join(root, n)
                data = open(f, 'rb').read()
                out = unscrub(data)
                if out != data:
                    open(f, 'wb').write(out)
        print(f'unscrubbed copy: {dst}')
        return
    if len(argv) >= 3 and argv[1] == 'verify':
        import re
        repo = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
        counts = {'ok': 0, 'ok-unscrubbed': 0, 'ok-legacy-$HOME': 0, 'MISMATCH': 0, 'MISSING': 0}
        for sums in argv[2:]:
            d = os.path.dirname(sums)
            for line in open(sums):
                m = re.match(r'([0-9a-f]{64})\s+\*?(.+)', line.strip())
                if not m:
                    continue
                h, name = m.group(1), m.group(2).strip()
                path = next((c for c in (os.path.normpath(os.path.join(d, name)), os.path.join(repo, name))
                             if os.path.exists(c)), None)
                if not path:
                    counts['MISSING'] += 1; print(f'MISSING   {sums}: {name}'); continue
                raw = open(path, 'rb').read()
                un = unscrub(raw)
                if hashlib.sha256(raw).hexdigest() == h: verdict = 'ok'
                elif hashlib.sha256(un).hexdigest() == h: verdict = 'ok-unscrubbed'
                elif hashlib.sha256(un.replace(b'$HOME', ORIG_HOME.encode())).hexdigest() == h: verdict = 'ok-legacy-$HOME'
                else: verdict = 'MISMATCH'
                counts[verdict] += 1
                if verdict == 'MISMATCH':
                    print(f'MISMATCH  {path}')
        print('  '.join(f'{k}={v}' for k, v in counts.items()))
        sys.exit(1 if counts['MISMATCH'] or counts['MISSING'] else 0)
    if len(argv) < 3 or argv[1] not in ('scrub', 'unscrub', 'sha256'):
        sys.exit(__doc__)
    cmd, files = argv[1], argv[2:]
    for f in files:
        data = open(f, 'rb').read()
        if cmd == 'sha256':
            print(f'{hashlib.sha256(unscrub(data)).hexdigest()}  {f}')
        elif cmd == 'unscrub':
            sys.stdout.buffer.write(unscrub(data))
        else:
            if any(ph.encode() in data for _, ph in MAPPING):
                sys.exit(f'refusing: {f} already contains a placeholder (scrub would not be reversible)')
            out = scrub(data)
            if unscrub(out) != data:
                sys.exit(f'round-trip mismatch: {f}')
            if out != data:
                open(f, 'wb').write(out)
                print(f'scrubbed {f}')


if __name__ == '__main__':
    main(sys.argv)
