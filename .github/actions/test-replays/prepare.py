"""Download and verify trimmed game assets on a replay cache miss."""

import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile


ARCHIVES = (
    ("Generals", "generals108_gamedata_trimmed.7z",
     "37A351AA430199D1F05DEB9E404857DCE7B461A6AC272C5D4A0B5652CDB06372"),
    ("GeneralsMD", "zerohour104_gamedata_trimmed.7z",
     "6837FE1E3009A4C239406C39B1598216C0943EE8ED46BB10626767029AC05E21"),
)


def prepare_game_data(game_data_directory):
    if not all(os.environ.get(key) for key in (
            "AWS_ACCESS_KEY_ID", "AWS_SECRET_ACCESS_KEY", "AWS_ENDPOINT_URL")):
        raise RuntimeError("Game data cache is cold and required R2 credentials are unavailable. "
                           "Replay validation cannot run.")
    game_data_directory = Path(game_data_directory)
    with tempfile.TemporaryDirectory(prefix="replay-download-", dir=os.environ.get("RUNNER_TEMP")) as temporary:
        for game, filename, expected_hash in ARCHIVES:
            archive = Path(temporary) / filename
            print(f"Downloading trimmed game data for {game}.")
            subprocess.run(["aws", "s3", "cp", f"s3://github-ci/{filename}", str(archive),
                            "--endpoint-url", os.environ["AWS_ENDPOINT_URL"]], check=True)
            digest = hashlib.sha256()
            with archive.open("rb") as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(chunk)
            if digest.hexdigest().upper() != expected_hash:
                raise RuntimeError(f"Hash verification failed for {game} game data")
            destination = game_data_directory / game
            destination.mkdir(parents=True, exist_ok=True)
            subprocess.run(["7z", "x", str(archive), f"-o{destination}", "-y"], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-data-directory", type=Path, required=True)
    args = parser.parse_args()
    prepare_game_data(args.game_data_directory)


if __name__ == "__main__":
    main()
