"""Control boundaries and the local device PCM contract; no hardware required."""
import base64
import json
import tempfile
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "bridge"), str(ROOT / "cockpit")]


def module(name, source):
    spec = importlib.util.spec_from_file_location(name, ROOT / source)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


bridge = module("voice_app", "bridge/main.py")
cockpit = module("cockpit_app", "cockpit/main.py")


class Controls(unittest.TestCase):
    def test_color_validation_and_rgb565(self):
        client = cockpit.app.test_client()
        with patch.object(cockpit, "current_device_port", return_value="device"), patch.object(
            cockpit, "device_command", return_value={"ok": True}
        ) as command:
            for invalid in (None, {}, {"color": "white"}, {"color": "#ff"}):
                self.assertEqual(client.post("/api/device/appearance", json=invalid).status_code, 400)
            command.assert_not_called()
            for color, rgb565 in (("#ffffff", 65535), ("#000000", 0), ("#ff0000", 63488)):
                self.assertEqual(client.post("/api/device/appearance", json={"color": color}).status_code, 200)
                command.assert_called_with("device", "face_set", color=rgb565)

    def test_color_requires_detected_device(self):
        with patch.object(cockpit, "current_device_port", return_value=None):
            self.assertEqual(cockpit.app.test_client().post(
                "/api/device/appearance", json={"color": "#ffffff"}).status_code, 409)

    def test_bridge_address_uses_device_route(self):
        with patch.object(cockpit, "load_lifecycle", return_value={
            "detection": {"status": {"ip": "192.168.1.192"}}
        }), patch.object(cockpit.socket, "socket") as socket:
            socket.return_value.getsockname.return_value = ("192.168.1.228", 42)
            self.assertEqual(cockpit.local_lan_ip(), "192.168.1.228")
            socket.return_value.connect.assert_called_with(("192.168.1.192", 9))
            socket.return_value.close.assert_called_once()

    def test_bridge_address_waits_for_dhcp(self):
        with patch.object(cockpit, "load_lifecycle", return_value={
            "detection": {"state": "progre_detected", "port": "device", "status": {"ip": "0.0.0.0"}}
        }), patch.object(cockpit, "device_command", return_value={"ip": "192.168.1.192"}), patch.object(
            cockpit.socket, "socket"
        ) as socket:
            socket.return_value.getsockname.return_value = ("192.168.1.228", 42)
            self.assertEqual(cockpit.local_lan_ip(), "192.168.1.228")
            socket.return_value.connect.assert_called_with(("192.168.1.192", 9))

    def test_backup_records_progre_snapshot_provenance(self):
        recovery = module("recovery_test", "cockpit/device_recovery.py")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            lifecycle = root / "lifecycle.json"
            lifecycle.write_text(json.dumps({"detection": {"state": "progre_detected"}}))
            def read_flash(cmd, **kwargs):
                Path(cmd[-1]).write_bytes(b"x" * 16)
            with patch.multiple(recovery, ROOT=root, DATA=root, BACKUPS=root / "backups",
                                LIFECYCLE=lifecycle, FLASH_SIZE=16), patch.object(
                recovery, "_esptool_command", return_value=["esptool"]
            ), patch.object(recovery.subprocess, "run", side_effect=read_flash):
                result = recovery.backup("device")
                self.assertTrue(result["verified"])
                self.assertEqual(result["backup_kind"], "progre_snapshot")
                self.assertTrue(recovery.verified_backup()["verified"])

    def test_pcm_boundary_and_local_response(self):
        client = bridge.app.test_client()
        for invalid in (b"", b"x"):
            self.assertEqual(client.post("/api/v1/audio", data=invalid).status_code, 400)
        self.assertEqual(client.post("/api/v1/audio", data=b"x" * 256002).status_code, 413)
        with patch.object(bridge, "_transcribe", return_value="Hello"), patch.object(
            bridge, "_ask_progre", return_value="Hello Ed"
        ), patch.object(bridge, "_synthesize", return_value=b"\x01\x00" * 100):
            response = client.post("/api/v1/audio", data=b"\x00\x00" * 100)
            self.assertEqual(response.status_code, 200)
            self.assertEqual(len(response.data), 200)
            self.assertEqual(base64.b64decode(response.headers["X-Progre-Text-B64"]), b"Hello Ed")
        with patch.object(bridge, "_transcribe", side_effect=RuntimeError("offline")):
            response = client.post("/api/v1/audio", data=b"\x00\x00")
            self.assertEqual(response.status_code, 500)
            self.assertTrue(response.is_json)

class RemoteAccess(unittest.TestCase):
    def test_remote_listener_requires_device_key_and_limits_routes(self):
        client = bridge.remote_app.test_client()
        with patch.object(bridge, "device_key", return_value="k" * 43):
            self.assertEqual(client.get("/health").status_code, 401)
            self.assertEqual(client.post("/api/v1/audio", data=b"xx").status_code, 401)
            headers = {"Authorization": "Bearer " + "k" * 43}
            self.assertEqual(client.get("/health", headers=headers).status_code, 200)
            self.assertEqual(client.get("/api/config", headers=headers).status_code, 404)
            self.assertEqual(client.post("/api/v1/audio", data=b"x" * 256002, headers=headers).status_code, 413)
            bridge.remote_audio_lock.acquire()
            try:
                self.assertEqual(client.post("/api/v1/audio", data=b"xx", headers=headers).status_code, 429)
            finally:
                bridge.remote_audio_lock.release()

    def test_public_exposure_requires_explicit_confirmation(self):
        with patch.object(cockpit.remote_access, "enable") as enable:
            response = cockpit.app.test_client().post("/api/remote/enable", json={})
            self.assertEqual(response.status_code, 409)
            enable.assert_not_called()

    def test_funnel_preserves_other_services(self):
        import remote_access
        state = {"BackendState": "Running", "Self": {"DNSName": "device.example.ts.net."}}
        config = {"TCP": {"8443": {"HTTPS": True}}, "Web": {
            "device.example.ts.net:8443": {"Handlers": {"/": {"Proxy": "http://127.0.0.1:9999"}}}}}
        with patch.object(remote_access, "_run", side_effect=[json.dumps(state), json.dumps(config)]) as run:
            with self.assertRaisesRegex(RuntimeError, "another service"):
                remote_access.enable(public=True)
            self.assertEqual(run.call_count, 2)


class JapaneseVoice(unittest.TestCase):
    def test_bilingual_voice_selection_and_shared_pitch_snapshot(self):
        from progre_config import DEFAULT_CONFIG
        config = {**DEFAULT_CONFIG, "voice": "kokoro:af_heart", "japanese_voice": "jm_kumo", "pitch_semitones": 5.5}
        with patch.object(bridge, "load_config", return_value=config) as load, patch.object(
            bridge.multilingual_voice, "synthesize"
        ) as synthesize, patch.object(bridge, "_wav_to_pcm_16k_mono", return_value=b"xx") as convert:
            result = bridge._synthesize("Hello. こんにちは。", "/tmp")
            self.assertTrue(result)
            self.assertEqual([(c.args[1], c.args[2]) for c in synthesize.call_args_list],
                             [("af_heart", False), ("jm_kumo", True)])
            self.assertEqual(load.call_count, 1)
            for call in convert.call_args_list:
                self.assertEqual(call.args[1]["pitch_semitones"], 5.5)

    def test_multilingual_settings_reject_invalid_values(self):
        from progre_config import _coerce
        for invalid in ({"recognition_language": "xx"}, {"japanese_voice": "missing"}, {"japanese_practice": "false"}):
            with self.assertRaises(ValueError):
                _coerce(invalid)

    def test_segmentation_keeps_numbers_with_japanese(self):
        import multilingual_voice
        self.assertEqual(list(multilingual_voice.segments("Say 3時です。 Then hello.")),
                         [("Say", False), ("3時です。", True), ("Then hello.", False)])


if __name__ == "__main__":
    unittest.main()
