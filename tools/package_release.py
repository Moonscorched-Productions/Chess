#!/usr/bin/env python3
"""Builds a release archive: the program, its data, the Stockfish engine and every license.

Usage (from the repository root, after `xmake f -m release && xmake build chess`):

    python3 tools/package_release.py [--engine <stockfish executable>] [--out <folder>]

Without --engine, the first executable named stockfish* inside engines/ is used. Stockfish is
GPL version 3: its license (Copying.txt) and a pointer to the source of that exact version are
always shipped next to it, so the archive can be published as is.

Result: <out>/Chess-<version>-<platform>.zip, containing

    Chess-<version>-<platform>/
        chess(.exe)
        data/
        engines/stockfish/<engine>, Copying.txt, SOURCE.txt
        LICENSE, THIRD_PARTY_NOTICES.md, README.md
"""

import argparse
import os
import platform
import re
import shutil
import stat
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GPL3_URL = "https://www.gnu.org/licenses/gpl-3.0.txt"
STOCKFISH_REPO = "https://github.com/official-stockfish/Stockfish"


def fail(message: str) -> None:
    sys.exit(f"package_release: {message}")


def project_version() -> str:
    match = re.search(r'set_version\("([^"]+)"\)', (ROOT / "xmake.lua").read_text(encoding="utf-8"))
    return match.group(1) if match else "0.0.0"


def platform_name() -> str:
    system = {"Darwin": "macos", "Windows": "windows", "Linux": "linux"}.get(platform.system(), platform.system().lower())
    machine = platform.machine().lower()
    arch = {"amd64": "x86_64", "x86_64": "x86_64", "arm64": "arm64", "aarch64": "arm64"}.get(machine, machine)
    return f"{system}-{arch}"


def find_program() -> Path:
    name = "chess.exe" if platform.system() == "Windows" else "chess"
    candidates = sorted((ROOT / "build").glob(f"**/release/{name}"), key=lambda p: p.stat().st_mtime, reverse=True)
    if not candidates:
        fail(f"no release build of {name} under build/; run `xmake f -m release && xmake build chess` first")
    return candidates[0]


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


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--engine", help="Stockfish executable to bundle (default: the first one in engines/)")
    parser.add_argument("--out", default=str(ROOT / "release"), help="output folder (default: release/)")
    args = parser.parse_args()

    program = find_program()
    engine = find_engine(args.engine)
    version = engine_version(engine)

    name = f"Chess-{project_version()}-{platform_name()}"
    out = Path(args.out).resolve()
    stage = out / name
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)

    shutil.copy2(program, stage / program.name)
    shutil.copytree(ROOT / "data", stage / "data")
    for document in ("LICENSE", "THIRD_PARTY_NOTICES.md", "README.md"):
        shutil.copy2(ROOT / document, stage / document)

    engine_dir = stage / "engines" / "stockfish"
    engine_dir.mkdir(parents=True)
    bundled = engine_dir / engine.name
    shutil.copy2(engine, bundled)
    bundled.chmod(bundled.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    (engine_dir / "Copying.txt").write_text(engine_license(engine), encoding="utf-8")
    (engine_dir / "SOURCE.txt").write_text(source_notice(version), encoding="utf-8")

    archive = shutil.make_archive(str(out / name), "zip", root_dir=out, base_dir=name)
    print(f"Stockfish {version} bundled from {engine}")
    print(f"Staged:  {stage}")
    print(f"Archive: {archive}")


if __name__ == "__main__":
    main()
