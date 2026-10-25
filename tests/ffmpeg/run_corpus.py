import argparse
import concurrent.futures
import json
from pathlib import Path
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument("executable", type=Path)
parser.add_argument("manifest", type=Path, help="JSON entries: path, offset, size, name, sha256")
parser.add_argument("output", type=Path)
parser.add_argument("--reference", type=Path, help="Optional independent FFmpeg decoding results (JSONL)")
args = parser.parse_args()
entries = json.loads(args.manifest.read_text())
reference = {}
if args.reference:
    reference = {row["sha256"]: row for row in map(json.loads, args.reference.read_text().splitlines())}

def decode(entry):
    row = {"sha256": entry["sha256"], "name": entry["name"]}
    try:
        process = subprocess.run([str(args.executable.resolve()), entry["path"], str(entry["offset"]), str(entry["size"])],
                                 capture_output=True, text=True, timeout=180)
        row.update(returncode=process.returncode, stderr=process.stderr)
        if process.returncode == 0:
            row["decoded"] = json.loads(process.stdout)
            if reference:
                streams = reference[entry["sha256"]]["output"]["streams"]
                expected = {"video_frames": sum(s["frames"] for s in streams if s["codec"] == "binkvideo"),
                            "audio_samples": sum(s["samples"] for s in streams),
                            "pcm_samples": sum(s["converted_samples"] for s in streams)}
                row["reference_match"] = all(row["decoded"][key] == value for key, value in expected.items())
        else:
            row["stdout"] = process.stdout
    except Exception as error:
        row["error"] = str(error)
    return row

start = time.perf_counter()
results = []
args.output.parent.mkdir(parents=True, exist_ok=True)
with args.output.open("w") as output, concurrent.futures.ThreadPoolExecutor(max_workers=4) as workers:
    for index, row in enumerate(workers.map(decode, entries)):
        output.write(json.dumps(row) + "\n")
        output.flush()
        results.append(row)
        if index % 1000 == 0:
            print(index, flush=True)
failures = [r for r in results if r.get("returncode", 1) != 0 or r.get("stderr") or not r.get("reference_match", True)]
summary = {"assets": len(results), "failures": failures, "seconds": time.perf_counter() - start}
args.output.with_suffix(".summary.json").write_text(json.dumps(summary, indent=2))
print(json.dumps(summary, indent=2))
raise SystemExit(bool(failures))
