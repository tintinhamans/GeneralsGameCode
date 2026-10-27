import argparse
import base64
import hashlib
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts/openal"
BEFORE = "7157d8120f979015927c2fe3a7c481c1cc802f4483fe14e2e86264ff0f1a6dde"
AFTER = "a2e0b882dde4f9a937f1f1bc46473f4f122956f4f98375675e9689309e26ec40"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class PatchTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir=ARGS.work_directory)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        (self.source / "core").mkdir(parents=True)
        self.voice = self.source / "core/voice.cpp"
        self.voice.write_bytes(PRISTINE)
        shutil.copyfile(ARGS.source / "CMakeLists.txt", self.source / "CMakeLists.txt")
        self.scripts = self.root / "scripts"
        self.scripts.mkdir()
        for name in ["apply-float-loader.ps1", "openal-soft-1.25.2-float-loader.inc"]:
            shutil.copyfile(SCRIPTS / name, self.scripts / name)

    def apply(self, success=True, expected_error=None):
        result = subprocess.run([str(ARGS.powershell), "-NoProfile", "-NonInteractive", "-File",
                                 str(self.scripts / "apply-float-loader.ps1"),
                                 "-SourceDirectory", str(self.source)], capture_output=True, text=True)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(expected_error, result.stdout + result.stderr)
        return result

    def test_apply_and_repeat_are_byte_identical(self):
        self.apply()
        self.assertEqual(digest(self.voice), AFTER)
        self.apply()
        self.assertEqual(digest(self.voice), AFTER)

    def test_wrong_version_is_rejected_without_writing(self):
        cmake = self.source / "CMakeLists.txt"
        cmake.write_bytes(cmake.read_bytes().replace(b"VERSION 1.25.2", b"VERSION 1.25.3"))
        self.apply(False, "requires OpenAL Soft 1.25.2")
        self.assertEqual(digest(self.voice), BEFORE)

    def test_unknown_source_is_rejected_without_writing(self):
        self.voice.write_bytes(PRISTINE + b"\n// changed\n")
        before = self.voice.read_bytes()
        self.apply(False, "Unexpected OpenAL Soft voice.cpp")
        self.assertEqual(self.voice.read_bytes(), before)

    def test_source_line_endings_are_not_silently_normalized(self):
        self.voice.write_bytes(PRISTINE.replace(b"\n", b"\r\n"))
        before = self.voice.read_bytes()
        self.apply(False, "Unexpected OpenAL Soft voice.cpp")
        self.assertEqual(self.voice.read_bytes(), before)

    def test_fragment_line_endings_are_portable(self):
        fragment = self.scripts / "openal-soft-1.25.2-float-loader.inc"
        fragment.write_bytes(fragment.read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))
        self.apply()
        self.assertEqual(digest(self.voice), AFTER)

    def test_corrupted_fragment_cannot_modify_source(self):
        fragment = self.scripts / "openal-soft-1.25.2-float-loader.inc"
        fragment.write_bytes(fragment.read_bytes().replace(b"source[i].c_val", b"0.0f"))
        self.apply(False, "Float loader patch digest mismatch")
        self.assertEqual(digest(self.voice), BEFORE)

    def test_build_keeps_fresh_directory_guard(self):
        source = self.root / "openal-soft-1.25.2"
        source.mkdir()
        sentinel = source / "sentinel"
        sentinel.write_bytes(b"keep")
        script = ("function cmake {}\nfunction ninja {}\nfunction cl {}\nfunction tar {}\n"
                  "$env:VSCMD_ARG_TGT_ARCH='x86'\n& '" +
                  str(SCRIPTS / "build-soft.ps1").replace("'", "''") +
                  "' -WorkDirectory '" + str(self.root).replace("'", "''") + "'")
        encoded = base64.b64encode(script.encode("utf-16-le")).decode("ascii")
        result = subprocess.run([str(ARGS.powershell), "-NoProfile", "-NonInteractive",
                                 "-EncodedCommand", encoded], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Already exists:", result.stderr)
        self.assertEqual(sentinel.read_bytes(), b"keep")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--powershell", type=Path, required=True)
    parser.add_argument("--work-directory", type=Path, required=True)
    ARGS = parser.parse_args()
    ARGS.work_directory.mkdir(parents=True, exist_ok=True)
    PRISTINE = (ARGS.source / "core/voice.cpp").read_bytes()
    # Accept a fresh public-script build as input, but reconstruct only its known original bytes.
    fragment = (SCRIPTS / "openal-soft-1.25.2-float-loader.inc").read_bytes().replace(b"\r\n", b"\n")
    if hashlib.sha256(PRISTINE).hexdigest() == AFTER:
        if PRISTINE.count(fragment) != 1:
            raise RuntimeError("Patched source does not contain the canonical fragment")
        PRISTINE = PRISTINE.replace(fragment, b"", 1)
    if hashlib.sha256(PRISTINE).hexdigest() != BEFORE:
        raise RuntimeError("Tests require the pinned pristine or patched source")
    unittest.main(argv=[__file__], verbosity=2)
