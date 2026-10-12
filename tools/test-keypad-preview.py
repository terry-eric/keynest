import importlib.util
import struct
import time
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("preview", Path(__file__).with_name("preview-keypad.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class Device:
    def __init__(self, busy=False, native=False, lost=False):
        self.busy, self.native, self.lost = busy, native, lost
        self.owner = None
        self.started = self.stopped = self.reads = 0

    def json(self, path, body=None):
        if path == "/api/state":
            return {"audio_ready": True, "mic_preview": self.native, "recording": {"state": 2 if self.busy else 0}}
        if body["action"] == "start":
            self.owner = body["owner"]
            self.started += 1
        else:
            self.stopped += 1
        return {"ok": True}

    def request(self, path, body=None, headers=None):
        if path == "/api/microphone":
            return b"native", {"X-PCB2-Sample-Rate": "16000"}
        if headers["X-PCB2-Recording"] != self.owner:
            raise ValueError("Not my recording")
        self.reads += 1
        return struct.pack("<hh", -120, 454), {"X-PCB2-State": "2", "X-PCB2-Dropped": "1" if self.lost else "0"}


class Tests(unittest.TestCase):
    def test_native_is_read_only(self):
        device = Device(native=True)
        preview = module.Preview(device)
        self.assertEqual(preview.read()[0], b"native")
        self.assertEqual(device.started, 0)

    def test_legacy_pcm_and_owner_cleanup(self):
        device = Device()
        preview = module.Preview(device)
        try:
            pcm, headers = preview.read()
            self.assertEqual(struct.unpack("<hh", pcm[:4]), (-120, 454))
            self.assertGreater(int(headers["X-PCB2-Frames"]), 0)
        finally:
            preview.close()
        self.assertEqual(device.started, 1)
        self.assertEqual(device.stopped, 1)

    def test_busy_recording_is_untouched(self):
        device = Device(busy=True)
        preview = module.Preview(device)
        try:
            with self.assertRaises(ValueError):
                preview.read()
        finally:
            preview.close()
        self.assertEqual((device.started, device.stopped), (0, 0))

    def test_lost_frames_stop_preview(self):
        device = Device(lost=True)
        preview = module.Preview(device)
        try:
            with self.assertRaises(ValueError):
                preview.read()
        finally:
            preview.close()
        self.assertEqual(device.stopped, 1)

    def test_other_owner_is_not_stopped(self):
        device = Device()
        preview = module.Preview(device)
        preview.stop_owned("another-owner")
        self.assertEqual(device.stopped, 0)


if __name__ == "__main__":
    unittest.main()
