import subprocess
import sys

fault = sys.argv[2]
operation = {"pending": "pending operation", "detach": "detach buffer", "source": "delete source",
             "buffer": "delete buffer", "stop": "stop source"}[fault]
expected = 0
if fault == "stop":
    expected = 73
process = subprocess.run([sys.argv[1], fault], capture_output=True, text=True, timeout=10,
                         creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
print(process.stdout, end="")
print(process.stderr, end="", file=sys.stderr)
if process.returncode != expected or "cleanup " + operation + " failed" not in process.stderr:
    raise SystemExit("Cleanup failure did not produce its expected exit code and diagnostic")
if expected == 0 and "safe cleanup failure reported without termination" not in process.stdout:
    raise SystemExit("Safe cleanup did not reach its final success marker")
