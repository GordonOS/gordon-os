"""Publish GordonOS binaries via F5 (Run and Debug).

The F5 dropdown can only run debug configurations, so this tiny script is the
`"type": "python"` target that forwards to the real PowerShell publisher
(`tools/publish-binaries.ps1`). Any extra CLI args are passed through,
e.g. `python tools/publish.py -SkipPush`.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# The build scripts are PowerShell on both platforms: Windows PowerShell 5.1
# ("powershell") on Windows, PowerShell 7 ("pwsh") on Linux/macOS. Pick whichever
# this host has rather than hard-coding a name that exists on only one of them.
powershell = shutil.which("pwsh") or shutil.which("powershell")
if powershell is None:
    print("publish: no PowerShell interpreter found - install pwsh (Linux/macOS) "
          "or use Windows PowerShell", file=sys.stderr, flush=True)
    os._exit(1)

rc = subprocess.run(
    [
        powershell,
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(ROOT / "tools" / "publish-binaries.ps1"),
        *sys.argv[1:],
    ],
    cwd=str(ROOT),
).returncode

if rc != 0:
    # os._exit avoids raising SystemExit, which the debugger would surface as
    # an "exception occurred" banner. The PowerShell output above has details.
    print(f"\nPublish failed (exit code {rc}). See the output above.", file=sys.stderr, flush=True)
    os._exit(rc)
