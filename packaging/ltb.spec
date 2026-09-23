# PyInstaller spec for the Windows/macOS app bundle.
# Build from the repo root:  pyinstaller packaging/ltb.spec --noconfirm
import os

from PyInstaller.utils.hooks import collect_all, collect_submodules

ROOT = os.path.abspath(os.path.join(SPECPATH, ".."))

datas = [(os.path.join(ROOT, "ltb", "static"), os.path.join("ltb", "static"))]
binaries = []
# mido picks its backend by name at runtime, so PyInstaller can't see the import.
hiddenimports = collect_submodules("mido") + ["rtmidi"]
for pkg in ("webview",):
    d, b, h = collect_all(pkg)
    datas += d
    binaries += b
    hiddenimports += h

a = Analysis(
    [os.path.join(SPECPATH, "launcher.py")],
    pathex=[ROOT],
    datas=datas,
    binaries=binaries,
    hiddenimports=hiddenimports,
    excludes=["tkinter", "pytest"],
)
pyz = PYZ(a.pure)
exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name="ListenToTheBroadcast",
    console=False,
)
coll = COLLECT(exe, a.binaries, a.datas, name="ListenToTheBroadcast")
