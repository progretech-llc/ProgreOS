"""Tailscale transport metadata and a private, per-device bearer credential."""
import json
import os
from pathlib import Path
import secrets
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
KEY_FILE = ROOT / "data/device/remote.key"
REMOTE_PORT = 8787
HTTPS_PORT = 8443


def device_key():
    KEY_FILE.parent.mkdir(parents=True, exist_ok=True)
    try:
        fd = os.open(KEY_FILE, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except FileExistsError:
        pass
    else:
        with os.fdopen(fd, "w") as f:
            f.write(secrets.token_urlsafe(32))
    value = KEY_FILE.read_text().strip()
    if len(value) != 43 or not all(c.isalnum() or c in "_-" for c in value):
        raise RuntimeError("The saved AIPI device key is invalid.")
    KEY_FILE.chmod(0o600)
    return value


def _run(*args):
    result = subprocess.run(["tailscale", *args], capture_output=True, text=True, timeout=15)
    if result.returncode:
        raise RuntimeError((result.stderr or result.stdout).strip()[-1000:])
    return result.stdout


def status():
    state = json.loads(_run("status", "--json"))
    config = json.loads(_run("serve", "status", "--json"))
    name = state.get("Self", {}).get("DNSName", "").rstrip(".")
    key = f"{name}:{HTTPS_PORT}"
    handler = config.get("Web", {}).get(key, {}).get("Handlers", {}).get("/", {})
    owned = handler.get("Proxy") == f"http://127.0.0.1:{REMOTE_PORT}"
    public = bool(config.get("AllowFunnel", {}).get(key))
    return {
        "running": state.get("BackendState") == "Running",
        "hostname": name,
        "url": f"https://{name}:{HTTPS_PORT}" if name else None,
        "port": HTTPS_PORT,
        "configured": owned,
        "public": owned and public,
        "port_in_use": str(HTTPS_PORT) in config.get("TCP", {}) and not owned,
    }


def enable(public=False):
    state = status()
    if not state["running"] or not state["hostname"]:
        raise RuntimeError("Sign in to Tailscale on this workstation first.")
    if state["port_in_use"]:
        raise RuntimeError("Tailscale port 8443 belongs to another service; it was left unchanged.")
    request = urllib.request.Request(
        f"http://127.0.0.1:{REMOTE_PORT}/health",
        headers={"Authorization": "Bearer " + device_key()},
    )
    try:
        with urllib.request.urlopen(request, timeout=3) as response:
            healthy = json.loads(response.read(4096)).get("bridge") == "progre-voice-bridge"
    except Exception as exc:
        raise RuntimeError("Start AIPI Control Center's voice service before enabling remote access.") from exc
    if not healthy:
        raise RuntimeError("The remote port does not belong to the AIPI voice service.")
    # Change only this service's port; never reset the workstation's Serve configuration.
    _run("funnel" if public else "serve", "--bg", f"--https={HTTPS_PORT}",
         f"http://127.0.0.1:{REMOTE_PORT}")
    return status()


def disable():
    state = status()
    if state["configured"]:
        _run("funnel" if state["public"] else "serve", f"--https={HTTPS_PORT}", "off")
    return status()
