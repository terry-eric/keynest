"""Serve the keypad UI locally, including live PCM preview on older firmware.

The compatibility preview only uses the device's RAM/browser stream. It never
saves audio to disk, Flash or SD, and releases its recording session when idle.
"""
import argparse
import json
import re
import secrets
import subprocess
import tempfile
import threading
import time
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.error import HTTPError
from urllib.request import Request, build_opener, ProxyHandler
from xml.sax.saxutils import escape
from keypad_usb import UsbDevice, DeviceError

WEB = Path(__file__).resolve().parents[1] / "firmware/keyboard/main/web"
CONTROL = {"Content-Type": "application/json", "X-PCB2-Control": "1"}


class Device:
    def __init__(self, url):
        self.url = url.rstrip("/")
        self.opener = build_opener(ProxyHandler({}))

    def request(self, path, body=None, headers=None):
        request = Request(self.url + path, data=body, headers=headers or {})
        with self.opener.open(request, timeout=2) as response:
            return response.read(), dict(response.headers)

    def json(self, path, body=None):
        raw, _ = self.request(path, None if body is None else json.dumps(body).encode(), CONTROL if body is not None else {})
        return json.loads(raw)


class Preview:
    def __init__(self, device):
        self.device = device
        self.lock = threading.RLock()
        self.ready = threading.Event()
        self.closing = threading.Event()
        self.thread = None
        self.native = None
        self.active = False
        self.touched = 0
        self.updated = 0
        self.frames = 0
        self.pcm = b""
        self.error = None

    def read(self):
        if self.native is None:
            self.native = bool(self.device.json("/api/state").get("mic_preview"))
        if self.native:
            return self.device.request("/api/microphone")
        with self.lock:
            self.touched = time.monotonic()
            if self.thread is None or not self.thread.is_alive():
                self.ready.clear()
                self.error = None
                self.pcm = b""
                self.thread = threading.Thread(target=self.pump, daemon=True)
                self.thread.start()
        self.ready.wait(.8)
        with self.lock:
            if self.error:
                raise ValueError(self.error)
            age = (time.monotonic() - self.updated) * 1000
            if not self.pcm or age > 1000:
                raise RuntimeError("等待麥克風樣本")
            return self.pcm, {"Content-Type": "application/octet-stream", "X-PCB2-Sample-Rate": "16000",
                              "X-PCB2-Frames": str(self.frames), "X-PCB2-Age-Ms": str(int(age))}

    def stop_owned(self, owner):
        if not owner:
            return
        try:
            # The owner-protected read must succeed before stopping a session.
            _, headers = self.device.request("/api/audio", headers={"X-PCB2-Recording": owner})
            if int(headers["X-PCB2-State"]) in (1, 2):
                self.device.json("/api/record", {"action": "stop"})
        except (OSError, ValueError):
            pass

    def pump(self):
        owner = None
        try:
            while not self.closing.is_set() and time.monotonic() - self.touched < 2:
                if owner is None:
                    state = self.device.json("/api/state")
                    rec = state["recording"]
                    if not state.get("audio_ready") or rec["state"] not in (0, 4) or rec.get("mounting") or rec.get("downloading"):
                        raise ValueError("裝置忙碌；請先停止錄音，再開啟相容預覽。")
                    token = secrets.token_hex(8)
                    self.device.json("/api/record", {"action": "start", "target": "browser", "owner": token})
                    owner = token
                    with self.lock:
                        self.active = True
                pcm, headers = self.device.request("/api/audio", headers={"X-PCB2-Recording": owner})
                if len(pcm) % 2 or len(pcm) > 2048 or int(headers["X-PCB2-Dropped"]):
                    raise RuntimeError("麥克風串流遺失資料，請重新連線。")
                if pcm:
                    with self.lock:
                        self.pcm = (self.pcm + pcm)[-2048:]
                        self.frames += len(pcm) // 2
                        self.updated = time.monotonic()
                    self.ready.set()
                status = int(headers["X-PCB2-State"])
                if status in (0, 4) and not pcm:
                    owner = None
                    with self.lock:
                        self.active = False
                    if status == 4:
                        raise RuntimeError("麥克風串流已停止，請重新連線。")
                self.closing.wait(.025)
        except (OSError, ValueError, KeyError, RuntimeError) as error:
            with self.lock:
                self.error = str(error)
            self.ready.set()
        finally:
            self.stop_owned(owner)
            with self.lock:
                self.active = False
                self.pcm = b""

    def close(self):
        self.closing.set()
        if self.thread:
            self.thread.join(timeout=5)


class Wifi:
    def __init__(self, args):
        self.args = args
        self.profile = "PCB2-Live-" + secrets.token_hex(6)
        self.path = None
        self.added = False

    def netsh(self, *arguments):
        return subprocess.run(["netsh", "wlan", *arguments], capture_output=True, check=True)

    def ssid(self):
        output = self.netsh("show", "interfaces").stdout.decode("utf-8", errors="replace")
        match = re.search(r"^\s*SSID\s*:\s*(.*?)\s*$", output, re.MULTILINE)
        return match[1] if match else None

    def connect(self):
        if not self.args.wifi_ssid:
            return
        ssid, password = escape(self.args.wifi_ssid), escape(self.args.wifi_password)
        xml = f'''<?xml version="1.0"?><WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1"><name>{self.profile}</name><SSIDConfig><SSID><name>{ssid}</name></SSID></SSIDConfig><connectionType>ESS</connectionType><connectionMode>manual</connectionMode><MSM><security><authEncryption><authentication>WPA2PSK</authentication><encryption>AES</encryption><useOneX>false</useOneX></authEncryption><sharedKey><keyType>passPhrase</keyType><protected>false</protected><keyMaterial>{password}</keyMaterial></sharedKey></security></MSM></WLANProfile>'''
        with tempfile.NamedTemporaryFile(suffix=".xml", delete=False) as file:
            self.path = Path(file.name)
            file.write(xml.encode("utf-8"))
        self.netsh("add", "profile", "filename=" + str(self.path), "user=current")
        self.added = True
        self.netsh("connect", "name=" + self.profile, "interface=" + self.args.wifi_interface)
        deadline = time.monotonic() + 20
        while self.ssid() != self.args.wifi_ssid:
            if time.monotonic() > deadline:
                raise RuntimeError("Unable to connect to PCB Wi-Fi; restoring original connection")
            time.sleep(.5)

    def close(self):
        try:
            if self.added:
                try:
                    # Respect a manual network switch made while previewing.
                    if self.args.restore_profile and self.ssid() in (None, self.args.wifi_ssid):
                        self.netsh("connect", "name=" + self.args.restore_profile, "interface=" + self.args.wifi_interface)
                finally:
                    self.netsh("delete", "profile", "name=" + self.profile)
        finally:
            if self.path:
                self.path.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", default="http://192.168.4.1")
    parser.add_argument("--usb", action="store_true", help="Use PCB USB CDC; never change Wi-Fi")
    parser.add_argument("--usb-port", help="Override automatic PCB USB port detection")
    parser.add_argument("--port", type=int, default=8028)
    parser.add_argument("--wifi-ssid")
    parser.add_argument("--wifi-password", default="pcb2play")
    parser.add_argument("--wifi-interface", default="Wi-Fi")
    parser.add_argument("--restore-profile")
    parser.add_argument("--idle-timeout", type=int, default=600)
    args = parser.parse_args()
    if args.usb and (args.wifi_ssid or args.restore_profile):
        parser.error("--usb cannot be combined with Wi-Fi switching options")
    if args.usb_port and not args.usb:
        parser.error("--usb-port requires --usb")
    if args.wifi_ssid and not args.restore_profile:
        parser.error("--wifi-ssid requires --restore-profile to restore your connection on exit")
    device = UsbDevice(args.usb_port) if args.usb else Device(args.device)
    preview = Preview(device)
    wifi = Wifi(args)

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def reply(self, status, body, headers=None):
            self.send_response(status)
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            for key, value in (headers or {}).items():
                if key.lower() in {"content-type", "content-disposition"} or key.lower().startswith("x-pcb2-"):
                    self.send_header(key, value)
            self.end_headers()
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                pass  # The browser may cancel a request when leaving the page.

        def dispatch(self):
            self.server.last_request = time.monotonic()
            path = self.path.split("?", 1)[0]
            try:
                if self.command == "GET" and path in ("/", "/app.js", "/style.css"):
                    name, mime = {"/": ("index.html", "text/html; charset=utf-8"), "/app.js": ("app.js", "text/javascript; charset=utf-8"), "/style.css": ("style.css", "text/css; charset=utf-8")}[path]
                    return self.reply(200, (WEB / name).read_bytes(), {"Content-Type": mime})
                if self.command == "GET" and path == "/api/state":
                    state = device.json(path)
                    preview.native = bool(state.get("mic_preview"))
                    state.update(mic_preview=True, mic_preview_legacy=not preview.native, local_preview=True,
                                 preview_wifi_managed=bool(args.wifi_ssid), transport="usb" if args.usb else "wifi")
                    if preview.active:
                        state["recording"].update(state=0, frames=0, duration_ms=0, error="", dropped_frames=0)
                    return self.reply(200, json.dumps(state).encode(), {"Content-Type": "application/json"})
                if self.command == "GET" and path == "/api/microphone":
                    pcm, headers = preview.read()
                    return self.reply(200, pcm, headers)
                if self.command == "GET" and path in ("/api/audio", "/api/recording.wav", "/api/sd/files", "/api/sd/recording.wav"):
                    pcm, headers = device.request(self.path if path == "/api/sd/recording.wav" else path, headers={"X-PCB2-Recording": self.headers.get("X-PCB2-Recording", "")})
                    return self.reply(200, pcm, headers)
                if self.command == "POST" and path in ("/api/config", "/api/trigger", "/api/record", "/api/save", "/api/sd", "/api/microphone/channel", "/api/sound/begin", "/api/sound/chunk", "/api/sound/commit", "/api/sound/cancel", "/api/sound/default", "/api/device/restart", "/api/preview/close"):
                    if self.headers.get("X-PCB2-Control") != "1":
                        return self.reply(403, b'{"error":"Missing control header"}', {"Content-Type": "application/json"})
                    size = int(self.headers.get("Content-Length", "0"))
                    if not 1 <= size <= 1536:
                        return self.reply(413, b'{}', {"Content-Type": "application/json"})
                    body = self.rfile.read(size)
                    if path == "/api/preview/close":
                        self.reply(200, b'{"ok":true}', {"Content-Type": "application/json"})
                        threading.Thread(target=self.server.shutdown, daemon=True).start()
                        return
                    if preview.active and path in ("/api/record", "/api/save", "/api/sd"):
                        raise ValueError("請先停止即時監測，等待暫存串流結束。")
                    data, headers = device.request(path, body, CONTROL)
                    return self.reply(200, data, headers)
                self.reply(404, b'{}', {"Content-Type": "application/json"})
            except HTTPError as error:
                self.reply(error.code, error.read(), dict(error.headers))
            except DeviceError as error:
                if path.startswith('/api/sound/'):
                    try:
                        fields=json.loads(body)
                        print('Sound request rejected:',path,'status',error.status,'offset',fields.get('offset'),
                              'frames',fields.get('frames'),'pcm_chars',len(fields.get('pcm','')),'error',str(error),flush=True)
                        if path=='/api/sound/chunk':
                            import base64,zlib
                            block=base64.b64decode(fields['pcm'],validate=True)
                            print('Sound integrity:',len(block),'bytes; CRC matches',zlib.crc32(block)==fields.get('crc32'),
                                  'expected_crc',fields.get('crc32'),flush=True)
                    except (ValueError,UnboundLocalError,TypeError,KeyError,AttributeError):pass
                self.reply(error.status, json.dumps({"error": str(error)}).encode(), {"Content-Type": "application/json"})
            except (OSError, ValueError, RuntimeError, KeyError) as error:
                status = 409 if isinstance(error, ValueError) else 503
                self.reply(status, json.dumps({"error": str(error)}).encode(), {"Content-Type": "application/json"})

        do_GET = do_POST = dispatch

    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.daemon_threads = True
    server.last_request = time.monotonic()

    def expire():
        while not preview.closing.wait(5):
            if time.monotonic() - server.last_request > args.idle_timeout:
                server.shutdown()
                break

    try:
        if not args.usb:
            wifi.connect()
        threading.Thread(target=expire, daemon=True).start()
        print(f"PCB2 local preview: http://127.0.0.1:{args.port}", flush=True)
        server.serve_forever(poll_interval=.1)
    except KeyboardInterrupt:
        pass
    finally:
        preview.close()
        server.server_close()
        if args.usb:
            device.close()
        else:
            wifi.close()
        print("Preview closed.", flush=True)


if __name__ == "__main__":
    main()
