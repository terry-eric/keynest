import base64
import json
import struct
import unittest
import zlib
from types import SimpleNamespace
from keypad_usb import UsbDevice, DeviceError


def binary(pcm):
    return {"pcm": base64.b64encode(pcm).decode(), "bytes": len(pcm), "crc32": zlib.crc32(pcm)}


class Serial:
    def __init__(self):
        self.incoming = bytearray()
        self.commands = []
        self.open_states = []
        self.reply = lambda request: {"id": request["id"], "protocol": 1, "status": 200, "data": {"ok": True}}
        self.closed = False

    def open(self):
        self.open_states.append((self.dtr, self.rts))
        self.closed = False

    def close(self):
        self.closed = True

    def reset_input_buffer(self):
        self.incoming.clear()

    def write(self, raw):
        request = json.loads(raw[6:])
        self.commands.append(request)
        response = self.reply(request)
        self.incoming.extend(b"I (10) keypad: diagnostic log\r\n")
        # Both stale responses and arbitrary packet boundaries must be tolerated.
        self.incoming.extend(b'PCB2_RPC {"id":0,"protocol":1,"status":200,"data":{}}\n')
        self.incoming.extend(b"PCB2_RPC " + json.dumps(response).encode() + b"\r\n")
        return len(raw)

    @property
    def in_waiting(self):
        return len(self.incoming)

    def read(self, size):
        chunk = self.incoming[:min(size, 17)]
        del self.incoming[:len(chunk)]
        return bytes(chunk)


class Tests(unittest.TestCase):
    def setUp(self):
        self.serial = Serial()
        self.device = UsbDevice(serial_factory=lambda: self.serial,
                                port_provider=lambda: [SimpleNamespace(device="COM_TEST", vid=0x303A, pid=0x4005)])

    def tearDown(self):
        self.device.close()

    def respond(self, data, status=200):
        self.serial.reply = lambda request: {"id": request["id"], "protocol": 1, "status": status,
                                             "data": data, "error": "test error"}

    def test_fragmented_rpc_and_no_reset(self):
        self.assertEqual(self.device.json("/api/state"), {"ok": True})
        self.assertEqual(self.serial.open_states, [(False, False)])
        self.assertTrue(self.serial.dtr)
        self.assertFalse(self.serial.rts)
        self.assertEqual(self.serial.port, "COM_TEST")
        self.device.close()
        self.assertFalse(self.serial.dtr)

    def test_complete_configuration_sent_once(self):
        config = {"mic_gain": 8, "keycodes": [104, 105, 106, 107]}
        self.device.request("/api/config", json.dumps(config).encode())
        self.assertEqual(self.serial.commands, [{"id": 1, "path": "/api/config", "body": config}])

    def test_unicode_macro_fits_the_usb_envelope(self):
        program={"name":"常用回覆","steps":[{"type":"text","text":"中"*170+"!"}]}
        self.device.request("/api/macro",json.dumps({"key":0,"program":program},ensure_ascii=False).encode())
        self.assertEqual(self.serial.commands[-1]["body"]["program"],program)

    def test_invalid_json_does_not_become_a_valid_usb_command(self):
        for raw in (b'{"slot":"left","slot":"right"}',
                    b'{"slot":"left","slo\\u0074":"right"}',
                    b'{"config":{"mic_gain":2,"mic_gain":8}}',
                    b'{"slot":', b'[]', b'null', b'\xff'):
            with self.subTest(raw=raw), self.assertRaises(DeviceError) as caught:
                self.device.request("/api/microphone/channel", raw)
            self.assertEqual(caught.exception.status, 400)
        self.assertEqual(self.serial.commands, [])
        self.assertEqual(self.serial.open_states, [])

    def test_real_signed_pcm_and_metadata(self):
        pcm = struct.pack("<hhhh", -32768, -120, 454, 32767)
        self.respond(dict(binary(pcm), rate=16000, frames=4000, age_ms=10))
        data, headers = self.device.request("/api/microphone")
        self.assertEqual(data, pcm)
        self.assertEqual(headers["X-PCB2-Frames"], "4000")
        self.assertEqual(headers["X-PCB2-Sample-Rate"], "16000")

    def test_bad_audio_checks_fail(self):
        for data in ({"pcm": "!", "bytes": 1, "crc32": 0},
                     dict(binary(b"ab"), bytes=1), dict(binary(b"ab"), crc32=0),
                     binary(b"x" * 2049), {"pcm": None, "bytes": 0, "crc32": 0}):
            with self.subTest(data=data), self.assertRaises(DeviceError):
                self.device.binary(data)

    def test_stale_empty_odd_and_invalid_metadata_fail(self):
        valid = dict(binary(b"ab"), rate=16000, frames=10, age_ms=10)
        for change in ({"rate": 48000}, {"frames": 0}, {"age_ms": 1001}, {"age_ms": -1},
                       {"frames": True}, binary(b""), binary(b"x")):
            self.respond(dict(valid, **change))
            with self.subTest(change=change), self.assertRaises(DeviceError):
                self.device.request("/api/microphone")

    def test_recording_owner_and_empty_chunk(self):
        self.respond(dict(binary(b""), state=2, frames=300, dropped=0))
        data, headers = self.device.request("/api/audio", headers={"X-PCB2-Recording": "0123456789abcdef"})
        self.assertEqual(data, b"")
        self.assertEqual(headers["X-PCB2-State"], "2")
        self.assertEqual(self.serial.commands[-1]["body"], {"owner": "0123456789abcdef"})

    def test_device_error_preserves_status_without_replay(self):
        self.respond({}, 409)
        with self.assertRaises(DeviceError) as caught:
            self.device.json("/api/save", {})
        self.assertEqual(caught.exception.status, 409)
        self.assertEqual(len(self.serial.commands), 1)

    def test_protocol_version_rejected(self):
        self.serial.reply = lambda request: {"id": request["id"], "protocol": 2, "status": 200, "data": {}}
        with self.assertRaises(DeviceError):
            self.device.json("/api/state")

    def test_ambiguous_device_does_not_open(self):
        self.device.port_provider = lambda: [SimpleNamespace(device="A", vid=0x303A, pid=0x4005)] * 2
        with self.assertRaises(DeviceError):
            self.device.json("/api/state")
        self.assertEqual(self.serial.open_states, [])

    def test_occupied_port_reports_handoff_and_can_reconnect(self):
        original = self.serial.open
        def denied(): raise OSError("could not open port 'COM_TEST': PermissionError(13, 'Access denied', None, 5)")
        self.serial.open = denied
        with self.assertRaises(DeviceError) as caught: self.device.json("/api/state")
        self.assertEqual(caught.exception.status, 503)
        self.assertIn("中斷連接",str(caught.exception))
        self.assertEqual(self.serial.commands, [])
        self.assertIsNone(self.device.serial)
        self.serial.open = original
        self.assertEqual(self.device.json("/api/state"), {"ok":True})
        self.assertEqual(len(self.serial.commands), 1)

    def test_download_releases_session_on_crc_failure(self):
        paths = []
        def reply(request):
            path = request["path"]
            paths.append(path)
            data = {"filename": "INTERNAL.WAV"} if path.endswith("begin") else dict(binary(b"ab"), crc32=0)
            return {"id": request["id"], "protocol": 1, "status": 200, "data": data}
        self.serial.reply = reply
        with self.assertRaises(DeviceError):
            self.device.download()
        self.assertEqual(paths, ["/api/file/begin", "/api/file/read", "/api/file/end"])

    def test_download_eof_and_headers(self):
        chunks = iter([binary(b"RIFF"), binary(b"")])
        self.serial.reply = lambda request: {"id": request["id"], "protocol": 1, "status": 200,
            "data": {"filename": "REC00001.WAV"} if request["path"].endswith("begin") else
                    next(chunks) if request["path"].endswith("read") else {}}
        data, headers = self.device.download()
        self.assertEqual(data, b"RIFF")
        self.assertEqual(headers["Content-Type"], "audio/wav")
        self.assertIn("REC00001.WAV", headers["Content-Disposition"])


if __name__ == "__main__":
    unittest.main()
