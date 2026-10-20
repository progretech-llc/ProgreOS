# Progre Portable Setup

## First launch

With Python 3 installed, run:

    python main.py

The launcher creates `.progre-venv`, installs only the small Cockpit Python
requirements, and starts the local Cockpit at `http://127.0.0.1:8786`.

Large runtimes, models, and voices are deliberately not installed silently.
Cockpit reports each dependency independently and exposes an Install action.
The local AI model installer is active when Ollama is present. Platform-safe
installers for the remaining large components are represented by the same API
contract and can be added without changing Progre firmware.

## Device detection

Use **Detect AIPI** with the device connected by USB. Cockpit identifies
Espressif USB hardware, probes for a Progre console signature when the serial
port is available, and distinguishes Progre, stock/unknown, and busy/recovery
states conservatively.

A stock/unknown device must not be destructively converted until Cockpit has
created and verified a full recovery backup. The destructive backup/flash
backend is intentionally not enabled in this portability checkpoint.

## Runtime ownership

Portable assets belong under `runtime/` and must not depend on Rend or a
specific user's home directory.

## USB commissioning (Phase 1L)

After flashing the Phase 1L firmware once, Wi-Fi and Voice Bridge settings are runtime NVS configuration. In Cockpit: Detect AIPI -> Scan Wi-Fi -> choose SSID -> enter password -> Connect Progre. The password is sent over the local USB serial connection and is not persisted by Cockpit. Bridge host/port are stored on Progre so moving to another computer does not require rebuilding firmware.

## Local robot companion (v1.1)

The local Cockpit uses port 8786; the LAN voice bridge uses 8785 to avoid
collisions with other workstation applications. Override these with
`PROGRE_COCKPIT_PORT` and `PROGRE_BRIDGE_PORT` in the launcher environment.
Detect AIPI, then commission its Wi-Fi and set the bridge host to the
workstation LAN address. The AIPI and workstation need network connectivity;
USB provides configuration and firmware access, not an audio network tunnel.

Choose an installed Ollama model and a Piper voice under Voice Character.
Apply the robot face color while connected by USB; it is stored on the AIPI.
The default face/eyes are white on black, with breathing, blinking, listening,
thinking and audio-amplitude-driven mouth movement. The right button records
while held, up to eight seconds, and sends the recording when released.
The left button retains its hardware power-management role; GPIO10 is held
high early in startup to retain battery power. Battery retention must be
checked with USB unplugged; USB-powered boot alone cannot prove it.

The voice pipeline uses local Whisper, Ollama and Piper after initial model
downloads. It does not require Muse or a factory reset. The reference bridge at
https://github.com/noise754/AIPI-Lite-Voice-Bridge was consulted for hardware
behavior (GPIO10 power hold); no code was copied from it.

Before replacing firmware, read all 16 MiB and verify the flash digest.
Keep recovery images private and outside Git. A backup of the currently
installed ProgreOS is not the original factory firmware.

Build with ESP-IDF v5.4.3, target esp32s3. ESP-IDF's generated build commands
require a path without spaces; on this workstation `/opt/progre-esp-idf` is a
bind mount of the SDK in Tools & Assets. Use `/opt/progre-espressif` as
IDF_TOOLS_PATH. Flash only the application at 0x10000 when the existing
partition table has the 1 MiB factory application there; preserve NVS.

On the current workstation, the wired connection and AIPI Wi-Fi use different
routers. The on-demand `progre-aipi.service` activates the saved NetworkManager
profile `AIPI companion Wi-Fi`, which has no default route and ignores Wi-Fi
DNS. Ethernet remains the default. Cockpit selects a host address using the
route to the device IP reported by Detect AIPI. Re-run detection after moving
networks or reconnecting USB (the tty number can change).

Launch from the app list using **AIPI Control Center**, or visit
http://127.0.0.1:8786 while the service is running. The service is not enabled
at login. The owner confirmed battery retention with USB unplugged after the
GPIO10 fix. Initial runtime reset testing exposed an undersized Wi-Fi event
stack; v1.1 sets that stack to 6144 bytes and the voice/main stack to 8192.

## Phone hotspots and remote voice (v1.2)

Use **Wi-Fi & Bridge → Connection → Phone hotspot / remote HTTPS**.
Standard phone tethering does not share the phone's Tailscale VPN with the
AIPI. The ESP32 uses HTTPS to a Tailscale Funnel endpoint on the workstation;
it does not run a native Tailscale client.

1. Start AIPI Control Center. The workstation needs Tailscale signed in and
   must remain awake while the AIPI is away.
2. Connect the AIPI by USB and choose Detect AIPI.
3. Enable hotspot access after acknowledging that its URL will be reachable
   from the internet. All remote requests require the device's private key.
4. Select Phone hotspot / remote HTTPS, then Apply connection mode to pair
   the AIPI. The key is sent over USB and stored in device NVS; it is never
   displayed in the browser. No Tailscale auth key is stored on the AIPI.
5. Turn on the phone's 2.4 GHz hotspot, scan for it, enter its password and
   choose Connect Progre. Configure this before leaving the workstation.
   Once configured, USB is not required for voice use.

Funnel listens on HTTPS 8443 and forwards only to the authenticated loopback
voice service on 8787. Cockpit, Ollama and company files are not exposed.
The existing Tailscale mappings are preserved. Disable remote access removes
only this voice mapping. Home network mode restores the local bridge path.

The firmware validates the HTTPS certificate with the ESP-IDF common-root
bundle, waits for network time, refuses redirects and sends the device key
only over TLS. Ensure the hotspot permits time synchronization (NTP).
`data/device/remote.key` is an owner-only local credential, excluded from Git;
protect full flash backups because NVS also contains the paired credential.
To revoke the current key, disable remote access, stop the service, replace
that local file, restart and pair again. Key rotation affects this AIPI only.

Private testing uses `tailscale serve --bg --https=8443
http://127.0.0.1:8787`. Public activation uses `tailscale funnel` on the same
port, through the explicit control-center action. Never use `serve reset` or
`funnel reset`; other workstation mappings belong to other applications.
The workstation's Tailscale operator is the desktop user so these controls
work without a root password.

Runtime reset diagnostics are available in USB status: reset reason, uptime,
current/minimum free heap. Firmware logs also report voice task stack headroom.
A completed long reply is evidence of that run, not proof that every historical
reset was caused by insufficient stack space.

References:
- https://tailscale.com/docs/features/tailscale-funnel
- https://github.com/tailscale/tailscale/issues/2259

## Japanese practice and Kokoro voices

Voice Character now offers an English voice and an independent Japanese
Kokoro voice (female and male choices), recognition language Auto/English/
Japanese, and Japanese practice partner mode. Japanese script is automatically
routed to the selected Japanese voice, including inside an English explanation.
Both languages use the same pitch/tempo/volume conversion with one settings
snapshot for the whole reply; changing a setting mid-reply affects the next
reply. Different voice identities naturally retain their own vocal character.

The current setup uses Kokoro af_heart for English and jf_alpha for Japanese,
with the owner's existing pitch and volume preserved. Kokoro runs locally on
CPU via ONNX. Whisper small multilingual replaces base.en for recognition;
Auto detects the spoken language, and Japanese can be forced for short practice
phrases. Japanese practice mode asks the selected model for brief, beginner-
friendly Japanese, gentle corrections and English explanations when useful.

For another installation, install `bridge/requirements-japanese.txt` into
`.progre-venv`; place `kokoro-v1.0.onnx` and `voices-v1.0.bin` in
`runtime/kokoro`, and multilingual `ggml-small.bin` in `runtime/models`.
Initialize the Japanese phonemizer once while online to download its dictionary.
Voice/model sources are recorded in `runtime/voices/SOURCES.md`. These local
assets and caches are excluded from Git. Existing Piper voices remain selectable.
