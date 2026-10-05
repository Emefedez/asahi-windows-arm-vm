#!/bin/bash
# Run a command in the Windows guest through the QEMU guest agent and print its output.
#   ./ga.sh ping
#   ./ga.sh 'powershell command text'      (runs via powershell.exe -NoProfile -Command)
#   ./ga.sh --put <local file> <guest path>  (upload a file)
#   ./ga.sh --get <guest path> <local file>  (download a file)
#   ./ga.sh --user '<powershell>'           (run in the logged-on user's desktop session, elevated)
# Env: GA_TIMEOUT seconds to wait for the command (default 600).
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
exec python3 - "$@" <<'PY'
import base64, json, os, socket, sys, time

def call(cmd, args=None, timeout=60):
    s = socket.socket(socket.AF_UNIX); s.settimeout(timeout); s.connect("qga.sock")
    f = s.makefile("rwb")
    # guest-sync flushes stale replies left by earlier, interrupted sessions.
    sid = int(time.time() * 1000) % 2**31
    f.write(json.dumps({"execute": "guest-sync", "arguments": {"id": sid}}).encode() + b"\n"); f.flush()
    while json.loads(f.readline()).get("return") != sid:
        pass
    f.write(json.dumps({"execute": cmd, **({"arguments": args} if args else {})}).encode() + b"\n"); f.flush()
    r = json.loads(f.readline()); s.close()
    if "error" in r: sys.exit(f"guest agent error: {r['error']}")
    return r["return"]

if sys.argv[1] == "--put":            # ./ga.sh --put <local file> <guest path>
    data = open(sys.argv[2], "rb").read()
    h = call("guest-file-open", {"path": sys.argv[3], "mode": "wb"})
    for i in range(0, len(data), 48 * 1024):
        call("guest-file-write", {"handle": h, "buf-b64": base64.b64encode(data[i:i + 48 * 1024]).decode()})
    call("guest-file-close", {"handle": h})
    print(f"{sys.argv[2]} -> {sys.argv[3]} ({len(data)} bytes)"); sys.exit(0)

if sys.argv[1] == "--get":            # ./ga.sh --get <guest path> <local file>
    h = call("guest-file-open", {"path": sys.argv[2], "mode": "rb"}); data = b""
    while True:
        r = call("guest-file-read", {"handle": h, "count": 1024 * 1024})
        data += base64.b64decode(r["buf-b64"])
        if r["eof"] or not r["count"]: break
    call("guest-file-close", {"handle": h}); open(sys.argv[3], "wb").write(data)
    print(f"{sys.argv[2]} -> {sys.argv[3]} ({len(data)} bytes)"); sys.exit(0)

user_mode = sys.argv[1] == "--user"   # ./ga.sh --user '<powershell>': run in the logged-on user's
if user_mode:                           # desktop session (needed for GPU/DC access), via a one-shot task
    sys.argv.pop(1)

if sys.argv[1:] == ["ping"]:
    call("guest-ping"); print("guest agent is up"); sys.exit(0)

script = " ".join(sys.argv[1:])
if user_mode:
    inner = base64.b64encode(script.encode("utf-16-le")).decode()
    script = (
        "$out='C:\\Windows\\Temp\\ga-user.txt'; Remove-Item $out -EA SilentlyContinue; "
        "$u=(Get-CimInstance Win32_ComputerSystem).UserName; "
        "$a=New-ScheduledTaskAction -Execute powershell.exe -Argument "
        f"\"-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command & {{ [Text.Encoding]::Unicode.GetString([Convert]::FromBase64String('{inner}')) | Invoke-Expression *>&1 | Out-File -Encoding utf8 $out }}\"; "
        "$p=New-ScheduledTaskPrincipal -UserId $u -LogonType Interactive -RunLevel Highest; "
        "Register-ScheduledTask -TaskName ga-user -Action $a -Principal $p -Force | Out-Null; "
        "Start-ScheduledTask ga-user; Start-Sleep 1; "
        # The task can still be 'Ready' (not started yet) right after Start-ScheduledTask: also wait for
        # the output file, which the task writes when its command finishes.
        "$t0=Get-Date; while (((Get-ScheduledTask ga-user).State -eq 'Running') -or "
        "(-not (Test-Path $out) -and ((Get-Date)-$t0).TotalSeconds -lt 30)) { Start-Sleep -Milliseconds 300 }; "
        "Unregister-ScheduledTask ga-user -Confirm:$false; Get-Content $out -EA SilentlyContinue")
pid = call("guest-exec", {"path": "powershell.exe",
                          "arg": ["-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", script],
                          "capture-output": True})["pid"]
deadline = time.time() + int(os.environ.get("GA_TIMEOUT", "600"))
while True:
    st = call("guest-exec-status", {"pid": pid})
    if st["exited"]: break
    if time.time() > deadline: sys.exit(f"timed out; guest pid {pid} still running")
    time.sleep(1)
for k, out in (("out-data", sys.stdout), ("err-data", sys.stderr)):
    if k in st: out.write(base64.b64decode(st[k]).decode("utf-8", "replace"))
sys.exit(st.get("exitcode", 0))
PY
