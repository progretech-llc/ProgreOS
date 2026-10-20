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
