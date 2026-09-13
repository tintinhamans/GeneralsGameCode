"""Install the pinned portable VC6 toolchain and export its build environment."""

import argparse
import hashlib
import os
from pathlib import Path, PureWindowsPath
import shutil
import tempfile
import urllib.request
import zipfile


COMMIT = "001c4bafdcf2ef4b474d693acccd35a91e848f40"
EXPECTED_HASH = "D0EE1F6DCEF7DB3AD703120D9FB4FAD49EBCA28F44372E40550348B1C00CA583"
def environment(base, current=None):
    """Return VC6 variables, retaining the runner's existing search paths."""
    current = {key.upper(): value for key, value in (os.environ if current is None else current).items()}
    base = PureWindowsPath(base) / "VC6SP6"
    common = base / "Common"
    dev = common / "msdev98"
    vc = base / "VC98"
    return {
        "VSCommonDir": str(common),
        "MSDevDir": str(dev),
        "MSVCDir": str(vc),
        "VcOsDir": "WINNT",
        "PATH": ";".join(map(str, [dev / "BIN", vc / "BIN", common / "TOOLS" / "WINNT", common / "TOOLS", current.get("PATH", "")])),
        "INCLUDE": ";".join(map(str, [vc / "ATL" / "INCLUDE", vc / "INCLUDE", vc / "MFC" / "INCLUDE", current.get("INCLUDE", "")])),
        "LIB": ";".join(map(str, [vc / "LIB", vc / "MFC" / "LIB", current.get("LIB", "")])),
    }


def install(root):
    """Verify the download before extracting any files into the install root."""
    root = Path(root).resolve()
    target = root / "VC6SP6"
    extracted = root / f"MSVC600-{COMMIT}"
    if target.exists() or extracted.exists():
        raise FileExistsError(f"Refusing to overwrite an existing VC6 installation in {root}")
    with tempfile.NamedTemporaryFile(
            suffix=".zip", delete=False, dir=os.environ.get("RUNNER_TEMP")) as temporary:
        archive = Path(temporary.name)
    try:
        print("Downloading VC6 Portable Installation", flush=True)
        urllib.request.urlretrieve(f"https://github.com/itsmattkc/MSVC600/archive/{COMMIT}.zip", archive)
        digest = hashlib.sha256()
        with archive.open("rb") as hash_source:
            for chunk in iter(lambda: hash_source.read(1024 * 1024), b""):
                digest.update(chunk)
        actual_hash = digest.hexdigest().upper()
        print(f"Downloaded file SHA256: {actual_hash}")
        if actual_hash != EXPECTED_HASH:
            raise ValueError(f"VC6 hash verification failed. Expected {EXPECTED_HASH}")
        with zipfile.ZipFile(archive) as zip_source:
            for member in zip_source.infolist():
                name = PureWindowsPath(member.filename)
                destination = (root / member.filename).resolve()
                if (name.is_absolute() or name.drive or ".." in name.parts
                        or ":" in member.filename or not destination.is_relative_to(root)
                        or not name.parts or name.parts[0] != extracted.name):
                    raise ValueError(f"Unsafe VC6 archive path: {member.filename}")
            zip_source.extractall(root)
        shutil.move(str(extracted), str(target))
    finally:
        archive.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    operation = parser.add_mutually_exclusive_group(required=True)
    operation.add_argument("--install", action="store_true")
    operation.add_argument("--export-environment", action="store_true")
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    if args.install:
        install(args.root)
    else:
        with Path(os.environ["GITHUB_ENV"]).open("a", encoding="utf-8") as output:
            for key, value in environment(args.root).items():
                if "\n" in value or "\r" in value:
                    raise ValueError(f"Invalid newline in environment variable {key}")
                output.write(f"{key}={value}\n")


if __name__ == "__main__":
    main()
