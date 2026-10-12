"""PCB2 USB CDC RPC transport. Opening the port never requests a board reset."""
import base64
import json
import re
import threading
import time
import zlib


class DeviceError(RuntimeError):
    def __init__(self, status, message):
        self.status = status
        super().__init__(message)


class UsbDevice:
    transport = "usb"

    def __init__(self, port=None, timeout=2, serial_factory=None, port_provider=None):
        self.port = port
        self.timeout = timeout
        self.serial_factory = serial_factory
        self.port_provider = port_provider
        self.serial = None
        self.lock = threading.RLock()
        self.sequence = 0
        self.pending = bytearray()

    def open(self):
        if self.serial is not None:
            return
        if self.serial_factory is None:
            try:
                import serial
                from serial.tools import list_ports
            except ImportError as error:
                raise DeviceError(503, "USB 模式需要 pyserial：python -m pip install pyserial") from error
            self.serial_factory = serial.Serial
            self.port_provider = list_ports.comports
        port = self.port
        if not port:
            candidates = [p.device for p in self.port_provider() if p.vid == 0x303A and p.pid == 0x4005]
            if len(candidates) != 1:
                raise DeviceError(503, "找不到唯一的 PCB2 USB 串列埠；請連接 PCB，或用 --usb-port 指定埠。")
            port = candidates[0]
        connection = self.serial_factory()
        connection.port, connection.baudrate = port, 115200
        connection.timeout, connection.write_timeout = .1, 1
        connection.dtr, connection.rts = False, False
        try:
            connection.open()
            connection.dtr = True
            connection.reset_input_buffer()
        except Exception:
            connection.close()
            raise
        self.serial = connection
        self.pending.clear()

    def close(self):
        with self.lock:
            connection, self.serial = self.serial, None
            self.pending.clear()
            if connection is not None:
                try:
                    connection.dtr = False
                except OSError:
                    pass
                finally:
                    connection.close()

    def rpc(self, path, body=None):
        with self.lock:
            self.open()
            self.sequence = self.sequence % 2147483647 + 1
            envelope = {"id": self.sequence, "path": path}
            if body is not None:
                envelope["body"] = body
            request = b"PCB2? " + json.dumps(envelope, separators=(",", ":"), allow_nan=False).encode() + b"\n"
            if len(request) > 1700:
                raise DeviceError(413, "USB request too large")
            try:
                if self.serial.write(request) != len(request):
                    raise OSError("Incomplete USB request")
                deadline = time.monotonic() + self.timeout
                while time.monotonic() < deadline:
                    while b"\n" in self.pending:
                        line, _, remaining = self.pending.partition(b"\n")
                        self.pending[:] = remaining
                        if not line.startswith(b"PCB2_RPC "):
                            continue  # ESP-IDF diagnostic logs share this port.
                        try:
                            response = json.loads(line[9:])
                        except (ValueError, UnicodeError) as error:
                            raise DeviceError(502, "USB 回應損壞，請重新操作。") from error
                        if not isinstance(response, dict):
                            raise DeviceError(502, "Invalid USB response")
                        if type(response.get("id")) is not int or response["id"] != self.sequence:
                            continue  # A timed-out request may finish after the next request.
                        if type(response.get("protocol")) is not int or response["protocol"] != 1 or type(response.get("status")) is not int:
                            raise DeviceError(502, "Unsupported USB protocol")
                        if response["status"] != 200:
                            raise DeviceError(response["status"] if 400 <= response["status"] <= 599 else 502,
                                              response.get("error", "USB command failed"))
                        if not isinstance(response.get("data"), dict):
                            raise DeviceError(502, "Invalid USB payload")
                        return response["data"]
                    chunk = self.serial.read(min(4096, max(1, self.serial.in_waiting)))
                    self.pending.extend(chunk)
                    if len(self.pending) > 8192:
                        raise DeviceError(502, "USB response too large")
                raise DeviceError(503, "USB 未回應；請確認 PCB 已更新為支援 USB 控制的韌體。")
            except (OSError, DeviceError):
                self.close()
                raise

    @staticmethod
    def binary(data):
        try:
            pcm = base64.b64decode(data["pcm"], validate=True)
            if type(data["bytes"]) is not int or len(pcm) != data["bytes"] or len(pcm) > 2048:
                raise ValueError("Invalid byte count")
            if type(data["crc32"]) is not int or zlib.crc32(pcm) != data["crc32"]:
                raise ValueError("CRC mismatch")
            return pcm
        except (KeyError, TypeError, ValueError) as error:
            raise DeviceError(502, "USB 音訊檢查失敗，請重新操作。") from error

    @staticmethod
    def integer(data, name, maximum=2**53 - 1):
        value = data.get(name)
        if type(value) is not int or not 0 <= value <= maximum:
            raise DeviceError(502, "Invalid USB audio metadata")
        return str(value)

    def json(self, path, body=None):
        return self.rpc(path, body)

    def request(self, path, body=None, headers=None):
        if path == "/api/recording.wav":
            return self.download()
        if path in ("/api/audio", "/api/microphone"):
            arguments = {"owner": (headers or {}).get("X-PCB2-Recording", "")} if path == "/api/audio" else None
            data = self.rpc(path, arguments)
            pcm = self.binary(data)
            if len(pcm) % 2:
                raise DeviceError(502, "Odd PCM byte count")
            result = {"Content-Type": "application/octet-stream", "X-PCB2-Frames": self.integer(data, "frames")}
            if path == "/api/microphone":
                if not pcm or data.get("rate") != 16000 or data.get("frames", 0) < len(pcm) // 2:
                    raise DeviceError(502, "Invalid USB microphone window")
                result.update({"X-PCB2-Sample-Rate": "16000", "X-PCB2-Age-Ms": self.integer(data, "age_ms", 1000)})
            else:
                result.update({"X-PCB2-State": self.integer(data, "state", 4), "X-PCB2-Dropped": self.integer(data, "dropped")})
            return pcm, result
        data = self.rpc(path, None if body is None else json.loads(body))
        return json.dumps(data).encode(), {"Content-Type": "application/json"}

    def download(self):
        with self.lock:
            info = self.rpc("/api/file/begin")
            try:
                filename = info.get("filename", "")
                if not isinstance(filename, str) or not re.fullmatch(r"(?:INTERNAL|REC[0-9]{5})\.WAV", filename):
                    raise DeviceError(502, "Invalid recording filename")
                wav = bytearray()
                while True:
                    chunk = self.binary(self.rpc("/api/file/read"))
                    if not chunk:
                        break
                    wav.extend(chunk)
                    if len(wav) > 44 + 16000 * 300 * 2:
                        raise DeviceError(502, "Recording exceeds maximum length")
                return bytes(wav), {"Content-Type": "audio/wav", "Content-Disposition": f'attachment; filename="{filename}"'}
            finally:
                self.rpc("/api/file/end")
