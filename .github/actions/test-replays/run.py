"""Stage an isolated Zero Hour runtime and check replay compatibility."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess


def register_generals(generals_directory):
    """Zero Hour resolves base-game assets through this per-user registry key."""
    import winreg

    with winreg.CreateKey(winreg.HKEY_CURRENT_USER,
                          r"SOFTWARE\Electronic Arts\EA Games\Generals") as key:
        winreg.SetValueEx(key, "InstallPath", 0, winreg.REG_SZ,
                         str(generals_directory.resolve()) + "\\")


def stage_runtime(userdata, build_artifacts_directory, runtime_directory,
                  game_data_directory, user_directory=None):
    userdata = Path(userdata)
    build_artifacts_directory = Path(build_artifacts_directory)
    runtime_directory = Path(runtime_directory).resolve()
    game_data_directory = Path(game_data_directory)
    for directory in (game_data_directory / "Generals", game_data_directory / "GeneralsMD",
                      build_artifacts_directory, userdata / "Replays", userdata / "Maps"):
        if not directory.is_dir():
            raise ValueError(f"Required replay source directory not found: {directory}")
    if not (build_artifacts_directory / "generalszh.exe").is_file():
        raise ValueError(f"Replay executable missing from build artifacts: {build_artifacts_directory}")
    if not any(p.is_file() and p.suffix.lower() == ".rep" for p in (userdata / "Replays").iterdir()):
        raise ValueError(f"No replay fixtures found in: {userdata / 'Replays'}")
    if runtime_directory.exists() and (not runtime_directory.is_dir() or any(runtime_directory.iterdir())):
        raise ValueError(f"Replay runtime must be empty to avoid stale binaries or logs: {runtime_directory}")

    # Cached proprietary data stays immutable. Built binaries always win.
    shutil.copytree(game_data_directory / "GeneralsMD", runtime_directory, dirs_exist_ok=True)
    shutil.copytree(build_artifacts_directory, runtime_directory, dirs_exist_ok=True)
    register_generals(game_data_directory / "Generals")
    if user_directory is None:
        user_directory = Path(os.environ["USERPROFILE"]) / "Documents" / "Command and Conquer Generals Zero Hour Data"
    for fixture_type in ("Replays", "Maps"):
        shutil.copytree(userdata / fixture_type, Path(user_directory) / fixture_type, dirs_exist_ok=True)
    return runtime_directory


def run_replays(runtime_directory, timeout_seconds=600):
    if timeout_seconds <= 0:
        raise ValueError("Replay timeout must be positive")
    runtime_directory = Path(runtime_directory).resolve()
    executable = runtime_directory / "generalszh.exe"
    if not executable.is_file():
        raise ValueError(f"Replay executable not found: {executable}")
    logs = [runtime_directory / "stdout.log", runtime_directory / "stderr.log"]
    kwargs = {}
    if os.name == "nt":
        startupinfo = subprocess.STARTUPINFO()
        startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startupinfo.wShowWindow = subprocess.SW_HIDE
        kwargs.update(startupinfo=startupinfo, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        with logs[0].open("w") as stdout, logs[1].open("w") as stderr:
            process = subprocess.Popen(
                [str(executable), "-jobs", "4", "-headless", "-replay", "*.rep"],
                cwd=runtime_directory, stdout=stdout, stderr=stderr, **kwargs)
            try:
                exit_code = process.wait(timeout=timeout_seconds)
            except subprocess.TimeoutExpired:
                try:
                    if os.name == "nt":
                        subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                                       check=True, creationflags=subprocess.CREATE_NO_WINDOW)
                    else:
                        process.kill()
                finally:
                    # Ensure the parent is reaped even when taskkill reports a race.
                    if process.poll() is None:
                        process.kill()
                    process.wait()
                raise RuntimeError(f"Replay tests timed out after {timeout_seconds} seconds") from None
            if exit_code != 0:
                raise RuntimeError(f"Replay tests failed with exit code {exit_code}")
        print("Replay compatibility tests passed.")
    finally:
        for log in logs:
            print(f"=== {log.name} ===")
            if log.exists():
                print(log.read_text(errors="replace"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--userdata", required=True, type=Path)
    parser.add_argument("--build-artifacts-directory", required=True, type=Path)
    parser.add_argument("--runtime-directory", required=True, type=Path)
    parser.add_argument("--game-data-directory", type=Path, required=True)
    parser.add_argument("--timeout-seconds", type=int, default=600)
    args = parser.parse_args()
    runtime = stage_runtime(args.userdata, args.build_artifacts_directory,
                            args.runtime_directory, args.game_data_directory)
    run_replays(runtime, args.timeout_seconds)


if __name__ == "__main__":
    main()
