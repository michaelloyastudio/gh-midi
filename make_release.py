#!/usr/bin/env python3
"""Cut the GH MIDI release zip from whatever is currently built.

    python3 make_release.py [version]

Re-runnable on purpose. The binaries and the docs are produced by different
work at different times -- the app gets rebuilt, the guide gets rewritten --
and hand-copying one without the other is exactly how the site ended up
offering a build that its own screenshots and README did not match.

Docs (README.txt, the Guide PDF) are taken from release/ if they are there,
so rewriting the guide and re-running this is all it takes to ship it.
"""
import hashlib, shutil, subprocess, sys, pathlib

VER   = sys.argv[1] if len(sys.argv) > 1 else 'v1.1'
ROOT  = pathlib.Path(__file__).parent
BUILT = ROOT / 'plugin/build/GHMidi_artefacts/Release'
REL   = ROOT / 'release'
STAGE = REL / f'GH-MIDI-{VER}'
ZIP   = REL / f'GH-MIDI-{VER}-macOS.zip'

if STAGE.exists():
    shutil.rmtree(STAGE)
STAGE.mkdir(parents=True)

for src in [BUILT / 'VST3/GH MIDI.vst3', BUILT / 'Standalone/GH MIDI.app']:
    if not src.exists():
        sys.exit(f'missing build: {src}\nrun: cmake --build plugin/build -j8')
    shutil.copytree(src, STAGE / src.name, symlinks=True)

for doc in ['README.txt', 'GH MIDI Guide.pdf']:
    p = REL / doc
    if p.exists():
        shutil.copy2(p, STAGE / doc)
    else:
        print(f'  ! {doc} not found in release/, shipping without it')

if ZIP.exists():
    ZIP.unlink()
# No --sequesterRsrc: that is what leaves a __MACOSX folder sitting next to
# the app when someone unzips it. ditto still carries the code signature.
# --norsrc: no ._ AppleDouble sidecars in the zip. Nothing in here needs
# xattrs; the app's signature lives in _CodeSignature, not in metadata.
subprocess.run(['ditto', '-c', '-k', '--norsrc', '--keepParent', str(STAGE), str(ZIP)], check=True)

size = ZIP.stat().st_size
print(f'\n{ZIP.name}  {size:,} bytes  ({size/1e6:.1f} MB)')
print('md5', hashlib.md5(ZIP.read_bytes()).hexdigest())
print('contents:')
for p in sorted(STAGE.iterdir()):
    print('  ', p.name)
# Sanity: the things that have silently shipped wrong before.
app = STAGE / 'GH MIDI.app'
print('icon     :', 'YES' if (app / 'Contents/Resources/Icon.icns').exists() else 'MISSING')
plist = subprocess.run(['plutil', '-p', str(app / 'Contents/Info.plist')],
                       capture_output=True, text=True).stdout
print('bluetooth:', 'YES' if 'NSBluetoothAlwaysUsageDescription' in plist else 'MISSING')
