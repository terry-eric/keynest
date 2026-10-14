"""Local USB console and opt-in Unicode macro input; never switches Wi-Fi."""
import argparse
import json
import mimetypes
import secrets
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit
from keypad_usb import UsbDevice, DeviceError
from macro_input import NativeInput, execute, validate_program

ROOT = Path(__file__).resolve().parents[1]
ASSETS = {"index.html","app.js","style.css","usb-serial.js","usb-connect.js","usb-connect.css","studio.js","studio.css","companion.js","build-info.json","gsap.min.js"}
POSTS = {"/api/config","/api/trigger","/api/record","/api/save","/api/sd","/api/microphone/channel","/api/sound/begin","/api/sound/chunk","/api/sound/commit","/api/sound/cancel", "/api/sound/default","/api/device/restart","/api/macro"}
GETS = {"/api/state","/api/microphone","/api/audio","/api/recording.wav","/api/sd/files","/api/sd/recording.wav"} | {f"/api/macro/{i}" for i in range(4)}

class MacroRunner:
    def __init__(self, device, backend):
        self.device, self.backend = device, backend
        self.closing, self.ready, self.busy = threading.Event(), threading.Event(), threading.Event()
        self.token = secrets.token_hex(8)
        self.connection_error, self.input_error = "", ""
        self.activity_lock = threading.Lock()
        self.activity = {"received":0,"completed":0,"failed":0,"last_key":None,"result":"waiting"}
        self.threads = []
    @property
    def error(self):
        return self.connection_error or self.input_error
    def diagnostics(self):
        with self.activity_lock: return dict(self.activity)
    def refresh_host(self):
        try:
            self.device.json("/api/macro/host", {"token":self.token,"busy":self.busy.is_set()})
            self.connection_error = ""
            self.ready.set()
        except (OSError, DeviceError, ValueError) as error:
            self.ready.clear()
            self.connection_error = str(error) if isinstance(error,DeviceError) else "鍵盤 USB 連線中斷，請確認資料線及其他程式的連線。"
    def heartbeat(self):
        while not self.closing.is_set():
            self.refresh_host()
            self.closing.wait(.4)
    def handle(self, event):
        if not event: return
        if not isinstance(event,dict) or type(event.get("id")) is not int or not 1 <= event["id"] <= 0xffffffff or type(event.get("key")) is not int or not 0 <= event["key"] < 4:
            self.input_error = "收到無效的按鍵事件；未送出輸入。請重新連接鍵盤。"
            raise ValueError("Invalid event")
        self.busy.set()
        with self.activity_lock:
            self.activity.update(received=self.activity["received"]+1,last_key=event["key"]+1,result="running")
        executing = False
        try:
            self.device.json("/api/macro/host", {"token":self.token,"busy":True})
            # Acknowledge before input: uncertain responses are never replayed into another app.
            self.device.json("/api/macro/ack", {"token":self.token,"id":event["id"]})
            validate_program(event.get("program"))
            executing = True
            execute(event["program"], self.backend, self.closing)
            self.input_error = ""
            with self.activity_lock:
                self.activity.update(completed=self.activity["completed"]+(not self.closing.is_set()),result="cancelled" if self.closing.is_set() else "completed")
        except (OSError, DeviceError, ValueError, RuntimeError, AttributeError) as error:
            with self.activity_lock: self.activity.update(failed=self.activity["failed"]+1,result="failed")
            if executing or isinstance(error,(ValueError,AttributeError)):
                self.input_error = "巨集未執行或已中止（"+type(error).__name__+"）。請確認目標程式的輸入權限與按鍵設定。"
            else:
                self.connection_error = str(error) if isinstance(error,DeviceError) else "鍵盤 USB 中斷，本次巨集未執行；重新連接後請再按實體鍵。"
                self.ready.clear()
            raise
        finally:
            self.busy.clear()
            try: self.device.json("/api/macro/host", {"token":self.token,"busy":False})
            except (OSError, DeviceError, ValueError): self.ready.clear()
    def pump(self):
        while not self.closing.is_set():
            if self.ready.is_set():
                try: event=self.device.json("/api/macro/event")
                except (OSError, DeviceError, ValueError) as error:
                    self.ready.clear()
                    self.connection_error = str(error) if isinstance(error,DeviceError) else "鍵盤 USB 連線中斷，正在等待重新連接。"
                else:
                    try: self.handle(event)
                    except (OSError, DeviceError, ValueError, RuntimeError, AttributeError): pass
            self.closing.wait(.1)
    def start(self):
        for target in (self.heartbeat, self.pump):
            thread=threading.Thread(target=target,daemon=True);thread.start();self.threads.append(thread)
    def close(self):
        self.closing.set()
        for thread in self.threads: thread.join(timeout=3)

def make_handler(device, runner, assets, port):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_): pass
        def reply(self, status, body, headers=None):
            self.send_response(status);self.send_header("Content-Length",str(len(body)));self.send_header("Cache-Control","no-store");self.send_header("X-Content-Type-Options","nosniff")
            for key,value in (headers or {}).items():
                if key.lower() in {"content-type","content-disposition"} or key.lower().startswith("x-pcb2-"): self.send_header(key,value)
            self.end_headers()
            try: self.wfile.write(body)
            except (BrokenPipeError,ConnectionResetError): pass
        def dispatch(self):
            allowed={f"127.0.0.1:{port}",f"localhost:{port}"}
            origin=self.headers.get("Origin")
            if self.headers.get("Host") not in allowed or origin and origin not in {"http://"+host for host in allowed}:
                return self.reply(403,b'{"error":"Local origin required"}',{"Content-Type":"application/json"})
            parsed=urlsplit(self.path);path=parsed.path
            try:
                if self.command=="GET" and (path=="/pcb2/" or path.startswith("/pcb2/")):
                    name=path.removeprefix("/pcb2/") or "index.html"
                    if name not in ASSETS:return self.reply(404,b"{}")
                    return self.reply(200,(assets/name).read_bytes(),{"Content-Type":mimetypes.guess_type(name)[0] or "application/octet-stream"})
                if self.command=="GET" and path in GETS:
                    if path=="/api/state":
                        data=device.json(path); data.update(transport="usb",local_preview=False,companion_ready=bool(runner and runner.ready.is_set()),companion_error=runner.error if runner else "桌面輸入尚未啟用",companion_activity=runner.diagnostics() if runner else None)
                        return self.reply(200,json.dumps(data,ensure_ascii=False).encode(),{"Content-Type":"application/json"})
                    body,headers=device.request(self.path,headers={"X-PCB2-Recording":self.headers.get("X-PCB2-Recording","")});return self.reply(200,body,headers)
                if self.command=="POST" and path in POSTS:
                    if self.headers.get("X-PCB2-Control")!="1" or self.headers.get("Content-Type","").split(";",1)[0]!="application/json":return self.reply(403,b"{}")
                    size=int(self.headers.get("Content-Length","0"))
                    if not 1<=size<=1536:return self.reply(413,b"{}")
                    body,headers=device.request(path,self.rfile.read(size),{"Content-Type":"application/json","X-PCB2-Control":"1"});return self.reply(200,body,headers)
                self.reply(404,b"{}",{"Content-Type":"application/json"})
            except DeviceError as error:self.reply(error.status,json.dumps({"error":str(error)}).encode(),{"Content-Type":"application/json"})
            except (OSError,ValueError,RuntimeError) as error:self.reply(503,json.dumps({"error":str(error)}).encode(),{"Content-Type":"application/json"})
        do_GET=do_POST=dispatch
    return Handler

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--usb-port");parser.add_argument("--port",type=int,default=8029)
    parser.add_argument("--enable-input",action="store_true",help="Allow physical PCB keys to type macros in the focused application")
    args=parser.parse_args()
    if not 1024 <= args.port <= 65535:parser.error("Use port 1024..65535")
    assets=ROOT/"dist-web/pcb2"
    if not (assets/"studio.js").is_file():parser.error("先執行 python tools/build-web-console.py")
    device=UsbDevice(args.usb_port);runner=MacroRunner(device,NativeInput()) if args.enable_input else None
    server=ThreadingHTTPServer(("127.0.0.1",args.port),make_handler(device,runner,assets,args.port));server.daemon_threads=True
    try:
        if runner:runner.start()
        print(f"PCB2 控制台：http://127.0.0.1:{args.port}/pcb2/?companion=1",flush=True)
        print("文字與巨集已啟用，將輸入到目前使用的程式。" if runner else "只控制鍵盤；需要文字巨集時加上 --enable-input。",flush=True)
        server.serve_forever(poll_interval=.1)
    except KeyboardInterrupt:pass
    finally:
        if runner:runner.close()
        server.server_close();device.close()
if __name__=="__main__":main()
