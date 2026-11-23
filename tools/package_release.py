#!/usr/bin/env python3
"""Builds a release: the program, its data, the Stockfish engine and every license.

Usage (from the repository root):

    python3 tools/package_release.py [--engine <stockfish executable>] [--out <folder>] [--no-build]

The script configures and builds the release version first (unless --no-build). Without --engine,
the first executable named stockfish* inside engines/ is used. Stockfish is GPL version 3: its
license (Copying.txt) and a pointer to the source of that exact version always ship with it, so
the result can be published as is.

Windows and Linux: <out>/Chess-<version>-<platform>.zip, containing

    Chess-<version>-<platform>/
        chess(.exe)
        msvcp140.dll, vcruntime140*.dll             (Windows: the Visual C++ runtime)
        data/
        engines/stockfish/<engine>, Copying.txt, SOURCE.txt
        LICENSE, THIRD_PARTY_NOTICES.md, README.md

macOS: <out>/Chess-<version>-macos-arm64.dmg with Chess.app, a link to Applications and the
licenses. The program runs on macOS 13 or later. The app is signed ad hoc unless --sign names a
Developer ID identity; --notarize <keychain profile> then has Apple notarize the disk image
(create the profile once with `xcrun notarytool store-credentials`).

    Chess.app/Contents/
        Info.plist
        MacOS/chess
        Helpers/engines/stockfish/stockfish        (the engine, a separate program)
        Resources/data/, AppIcon.icns, LICENSE, THIRD_PARTY_NOTICES.md
        Resources/licenses/stockfish/Copying.txt, SOURCE.txt
"""

import argparse
import os
import platform
import plistlib
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GPL3_URL = "https://www.gnu.org/licenses/gpl-3.0.txt"
STOCKFISH_REPO = "https://github.com/official-stockfish/Stockfish"

APP_NAME = "Chess"
BUNDLE_ID = "com.moonscorched.chess"
COPYRIGHT = "Copyright © 2026 Moonscorched Productions. Free software under the GNU GPL v3."
MACOS_MIN_VERSION = "13.0"  # SFML 3 needs macOS 13
ICON = ROOT / "data" / "moon-pawn-icon.png"
DOCUMENTS = ("LICENSE", "THIRD_PARTY_NOTICES.md", "README.md")


def fail(message: str) -> None:
    sys.exit(f"package_release: {message}")


def run(command: list[str], **kwargs) -> subprocess.CompletedProcess:
    result = subprocess.run(command, **kwargs)
    if result.returncode != 0:
        fail(f"`{' '.join(command)}` failed with exit code {result.returncode}")
    return result


def is_macos() -> bool:
    return platform.system() == "Darwin"


def project_version() -> str:
    match = re.search(r'set_version\("([^"]+)"\)', (ROOT / "xmake.lua").read_text(encoding="utf-8"))
    return match.group(1) if match else "0.0.0"


def platform_name() -> str:
    system = {"Darwin": "macos", "Windows": "windows", "Linux": "linux"}.get(platform.system(), platform.system().lower())
    machine = platform.machine().lower()
    arch = {"amd64": "x86_64", "x86_64": "x86_64", "arm64": "arm64", "aarch64": "arm64"}.get(machine, machine)
    return f"{system}-{arch}"


# --- Build -------------------------------------------------------------------

def build() -> None:
    configure = ["xmake", "f", "-m", "release", "-y"]
    env = dict(os.environ)
    if is_macos():
        # The program and every library it links must target the oldest supported macOS.
        # xmake passes --target_minver to its own builds; CMake-built packages read the variable.
        configure.append(f"--target_minver={MACOS_MIN_VERSION}")
        env["MACOSX_DEPLOYMENT_TARGET"] = MACOS_MIN_VERSION
    run(configure, cwd=ROOT, env=env)
    run(["xmake", "build", "-y", "chess"], cwd=ROOT, env=env)


def find_program() -> Path:
    name = "chess.exe" if platform.system() == "Windows" else "chess"
    candidates = sorted((ROOT / "build").glob(f"**/release/{name}"), key=lambda p: p.stat().st_mtime, reverse=True)
    if not candidates:
        fail(f"no release build of {name} under build/; run without --no-build")
    return candidates[0]


def check_macos_program(program: Path) -> None:
    """Refuses a program that would not start on other Macs."""
    load = run(["otool", "-l", str(program)], capture_output=True, text=True).stdout
    match = re.search(r"LC_BUILD_VERSION.*?minos (\S+)", load, re.DOTALL)
    if not match or match.group(1) != MACOS_MIN_VERSION:
        fail(f"{program} targets macOS {match.group(1) if match else '?'}, not {MACOS_MIN_VERSION}; "
             "rebuild without --no-build")
    libraries = run(["otool", "-L", str(program)], capture_output=True, text=True).stdout.splitlines()[1:]
    foreign = [line.split()[0] for line in libraries if not line.strip().startswith(("/System/", "/usr/lib/"))]
    if foreign:
        fail("the program links libraries that other Macs do not have: " + ", ".join(foreign))


# --- Engine ------------------------------------------------------------------

def is_executable(path: Path) -> bool:
    if not path.is_file():
        return False
    if platform.system() == "Windows":
        return path.suffix.lower() == ".exe"
    return os.access(path, os.X_OK)


def find_engine(explicit: str | None) -> Path:
    if explicit:
        engine = Path(explicit).resolve()
        if not is_executable(engine):
            fail(f"{engine} is not an executable file")
        return engine
    for path in sorted((ROOT / "engines").rglob("*")):
        if path.name.lower().startswith("stockfish") and is_executable(path):
            return path
    fail("no Stockfish executable found in engines/; download one or pass --engine")


def engine_version(engine: Path) -> str:
    """The version from the engine's "id name" line, e.g. "19" for "Stockfish 19"."""
    try:
        result = subprocess.run([str(engine)], input="uci\nquit\n", capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired) as error:
        fail(f"could not run {engine}: {error}")
    match = re.search(r"^id name Stockfish (\S+)", result.stdout, re.MULTILINE)
    if not match:
        fail(f"{engine} does not identify itself as Stockfish")
    return match.group(1)


def engine_license(engine: Path) -> str:
    """Stockfish's Copying.txt from the engine's folder, or the GPL v3 text from gnu.org."""
    for folder in (engine.parent, engine.parent.parent):
        copying = folder / "Copying.txt"
        if copying.is_file():
            return copying.read_text(encoding="utf-8")
    with urllib.request.urlopen(GPL3_URL, timeout=30) as response:
        return response.read().decode("utf-8")


def source_notice(version: str) -> str:
    # Release tags are "sf_19"; development builds ("dev-20250101-abcdef") have no tag.
    if re.fullmatch(r"\d+(\.\d+)?", version):
        tag = f"sf_{version}"
        where = (f"    {STOCKFISH_REPO}/tree/{tag}\n"
                 f"    {STOCKFISH_REPO}/archive/refs/tags/{tag}.tar.gz\n")
    else:
        where = f"    {STOCKFISH_REPO} (development build {version}; see the commit in its name)\n"
    return (
        f"Stockfish {version}\n"
        "\n"
        "Stockfish is free software, distributed under the GNU General Public License version 3\n"
        "(see Copying.txt in this folder). It is a separate program; this chess application starts it\n"
        "and talks to it over the UCI protocol.\n"
        "\n"
        "The complete source code of this exact version is available at:\n"
        "\n"
        f"{where}"
        "\n"
        "More about Stockfish: https://stockfishchess.org\n"
    )


def write_engine_licenses(folder: Path, engine: Path, version: str) -> None:
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "Copying.txt").write_text(engine_license(engine), encoding="utf-8")
    (folder / "SOURCE.txt").write_text(source_notice(version), encoding="utf-8")


def copy_executable(source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    target.chmod(target.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


# --- Windows and Linux: a folder in a zip ------------------------------------

# Files in data/ that are not part of a release: the icon source and private sound files.
DATA_EXCLUDED = (ICON.name, "private", ".DS_Store")
# The Visual C++ runtime the program is linked against. Copied next to the program so that it
# starts on computers without the Visual C++ Redistributable.
MSVC_RUNTIME = ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")


def msvc_runtime_folder() -> Path | None:
    """The x64 runtime folder of the newest Visual Studio, e.g. .../Redist/MSVC/14.x/x64/Microsoft.VC145.CRT."""
    roots = []
    if os.environ.get("VCToolsRedistDir"):
        roots.append(Path(os.environ["VCToolsRedistDir"]).parent)
    vswhere = Path(os.environ.get("ProgramFiles(x86)", "")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if vswhere.is_file():
        found = subprocess.run([str(vswhere), "-latest", "-products", "*", "-property", "installationPath"],
                               capture_output=True, text=True)
        if found.returncode == 0 and found.stdout.strip():
            roots.append(Path(found.stdout.strip().splitlines()[0]) / "VC" / "Redist" / "MSVC")

    def version_key(folder: Path) -> tuple[int, ...]:
        return tuple(int(part) for part in folder.parent.parent.name.split(".") if part.isdigit())

    for root in roots:
        candidates = [folder for folder in root.glob("*/x64/Microsoft.VC*.CRT")
                      if all((folder / dll).is_file() for dll in MSVC_RUNTIME)]
        if candidates:
            return max(candidates, key=version_key)
    return None


def copy_msvc_runtime(stage: Path) -> None:
    folder = msvc_runtime_folder()
    if folder is None:
        print("package_release: warning: Visual C++ runtime DLLs not found; the program will need the "
              "Visual C++ Redistributable (x64) on computers that do not have it")
        return
    for dll in MSVC_RUNTIME:
        shutil.copy2(folder / dll, stage / dll)


def package_folder(program: Path, engine: Path, version: str, out: Path) -> Path:
    name = f"{APP_NAME}-{project_version()}-{platform_name()}"
    stage = out / name
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)

    copy_executable(program, stage / program.name)
    if platform.system() == "Windows":
        copy_msvc_runtime(stage)
    shutil.copytree(ROOT / "data", stage / "data", ignore=shutil.ignore_patterns(*DATA_EXCLUDED))
    for document in DOCUMENTS:
        shutil.copy2(ROOT / document, stage / document)
    engine_dir = stage / "engines" / "stockfish"
    copy_executable(engine, engine_dir / engine.name)
    write_engine_licenses(engine_dir, engine, version)

    return Path(shutil.make_archive(str(out / name), "zip", root_dir=out, base_dir=name))


# --- macOS: Chess.app in a disk image ----------------------------------------

def make_icns(png: Path, target: Path) -> None:
    with tempfile.TemporaryDirectory() as temp:
        iconset = Path(temp) / "AppIcon.iconset"
        iconset.mkdir()
        for size in (16, 32, 128, 256, 512):
            for scale in (1, 2):
                pixels = size * scale
                suffix = "" if scale == 1 else "@2x"
                run(["sips", "-z", str(pixels), str(pixels), str(png), "--out",
                     str(iconset / f"icon_{size}x{size}{suffix}.png")], capture_output=True)
        run(["iconutil", "-c", "icns", str(iconset), "-o", str(target)])


def write_info_plist(contents: Path, has_icon: bool) -> None:
    version = project_version()
    info = {
        "CFBundleDevelopmentRegion": "en",
        "CFBundleExecutable": "chess",
        "CFBundleIdentifier": BUNDLE_ID,
        "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundleName": APP_NAME,
        "CFBundleDisplayName": APP_NAME,
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": version,
        "CFBundleVersion": version,
        "LSMinimumSystemVersion": MACOS_MIN_VERSION,
        "LSApplicationCategoryType": "public.app-category.board-games",
        "NSHighResolutionCapable": True,
        "NSHumanReadableCopyright": COPYRIGHT,
    }
    if has_icon:
        info["CFBundleIconFile"] = "AppIcon"
    with open(contents / "Info.plist", "wb") as file:
        plistlib.dump(info, file)


def codesign(path: Path, identity: str | None) -> None:
    command = ["codesign", "--force", "--sign", identity or "-"]
    if identity:
        # Notarization requires the hardened runtime and a secure timestamp.
        command += ["--options", "runtime", "--timestamp"]
    run(command + [str(path)])


def build_app(program: Path, engine: Path, version: str, stage: Path, identity: str | None) -> Path:
    app = stage / f"{APP_NAME}.app"
    contents = app / "Contents"
    resources = contents / "Resources"

    copy_executable(program, contents / "MacOS" / "chess")
    shutil.copytree(ROOT / "data", resources / "data", ignore=shutil.ignore_patterns(ICON.name))
    for document in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
        shutil.copy2(ROOT / document, resources / document)
    # The engine goes where macOS expects nested programs. The app is arm64 only, so is the engine.
    bundled_engine = contents / "Helpers" / "engines" / "stockfish" / "stockfish"
    copy_executable(engine, bundled_engine)
    archs = run(["lipo", "-archs", str(bundled_engine)], capture_output=True, text=True).stdout.split()
    if len(archs) > 1 and "arm64" in archs:
        run(["lipo", "-thin", "arm64", str(bundled_engine), "-output", str(bundled_engine)])
    write_engine_licenses(resources / "licenses" / "stockfish", engine, version)

    has_icon = ICON.is_file()
    if has_icon:
        make_icns(ICON, resources / "AppIcon.icns")
    write_info_plist(contents, has_icon)
    (contents / "PkgInfo").write_text("APPL????", encoding="ascii")

    # Sign from the inside out: the engine first, then the app around it.
    codesign(bundled_engine, identity)
    codesign(app, identity)
    run(["codesign", "--verify", "--strict", "--deep", str(app)])
    return app


def package_macos(program: Path, engine: Path, version: str, out: Path, identity: str | None,
                  notary_profile: str | None) -> Path:
    check_macos_program(program)
    name = f"{APP_NAME}-{project_version()}-{platform_name()}"
    stage = out / name
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)

    build_app(program, engine, version, stage, identity)
    # Next to the app in the disk image: a shortcut to drag it into, and the licenses in plain view.
    (stage / "Applications").symlink_to("/Applications")
    for document in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
        shutil.copy2(ROOT / document, stage / document)
    write_engine_licenses(stage / "Stockfish license", engine, version)

    dmg = out / f"{name}.dmg"
    dmg.unlink(missing_ok=True)
    # hdiutil's own size estimate can come out too small; give it the contents plus room for the file system.
    megabytes = sum(f.stat().st_size for f in stage.rglob("*") if f.is_file() and not f.is_symlink()) // 2**20
    run(["hdiutil", "create", "-volname", APP_NAME, "-srcfolder", str(stage), "-size", f"{megabytes + 40}m",
         "-fs", "HFS+", "-ov", "-format", "UDZO", "-quiet", str(dmg)])
    if identity:
        codesign(dmg, identity)
    if notary_profile:
        if not identity:
            fail("--notarize needs --sign with a Developer ID Application identity")
        run(["xcrun", "notarytool", "submit", str(dmg), "--keychain-profile", notary_profile, "--wait"])
        run(["xcrun", "stapler", "staple", str(dmg)])
    return dmg


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--engine", help="Stockfish executable to bundle (default: the first one in engines/)")
    parser.add_argument("--out", default=str(ROOT / "release"), help="output folder (default: release/)")
    parser.add_argument("--no-build", action="store_true", help="package the existing release build")
    parser.add_argument("--sign", metavar="IDENTITY",
                        help='macOS: Developer ID identity, e.g. "Developer ID Application: Name (TEAMID)"')
    parser.add_argument("--notarize", metavar="PROFILE", help="macOS: notarytool keychain profile")
    args = parser.parse_args()

    if not args.no_build:
        build()
    program = find_program()
    engine = find_engine(args.engine)
    version = engine_version(engine)
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)

    if is_macos():
        result = package_macos(program, engine, version, out, args.sign, args.notarize)
    else:
        result = package_folder(program, engine, version, out)
    print(f"Stockfish {version} bundled from {engine}")
    print(f"Release: {result}")


if __name__ == "__main__":
    main()
