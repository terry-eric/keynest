"""Build the hosted USB console from the maintained firmware UI; no firmware changes."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def build(output, base_path="pcb2"):
    if not base_path or any(part in ("", ".", "..") for part in base_path.split("/")) or any(c not in "abcdefghijklmnopqrstuvwxyz0123456789-/" for c in base_path):
        raise ValueError("Use a relative lowercase base path")
    destination = output / base_path
    destination.mkdir(parents=True, exist_ok=True)
    ui = ROOT / "firmware/keyboard/main/web"
    extra = ROOT / "web-console"
    html = (ui / "index.html").read_text(encoding="utf-8")
    marker = '  <div class="offline" id="offline" hidden>'
    if html.count(marker) != 1 or html.count('src="/app.js"') != 1:
        raise ValueError("Firmware UI layout changed; review the hosted connection insertion")
    html = html.replace('href="/style.css"', 'href="./style.css"').replace('href="/"', 'href="./"')
    html = html.replace('<script src="/app.js" defer></script>', '<link rel="stylesheet" href="./usb-connect.css"><script src="./usb-serial.js" defer></script><script src="./usb-connect.js" defer></script><script src="./app.js" defer></script>')
    html = html.replace(marker, (extra / "usb-connect.html").read_text(encoding="utf-8") + "\n" + marker)
    (destination / "index.html").write_text(html, encoding="utf-8", newline="\n")
    # Supply the USB fetch implementation lexically; retain the entire maintained UI.
    app = (ui / "app.js").read_text(encoding="utf-8")
    (destination / "app.js").write_text('(function(fetch) {\n' + app + '\n})(window.PCB2Transport.fetch.bind(window.PCB2Transport));\n', encoding="utf-8", newline="\n")
    files = {"style.css":ui / "style.css", **{name:extra / name for name in ("usb-serial.js", "usb-connect.js", "usb-connect.css")}}
    for name, source in files.items():
        (destination / name).write_bytes(source.read_bytes())
    (output / "_headers").write_text('/' + base_path + '/*\n  Permissions-Policy: serial=(self)\n  X-Content-Type-Options: nosniff\n  Referrer-Policy: no-referrer\n  X-Frame-Options: DENY\n  Content-Security-Policy: default-src \'self\'; script-src \'self\' https://static.cloudflareinsights.com; style-src \'self\' \'unsafe-inline\'; media-src \'self\' blob:; img-src \'self\' data:; connect-src \'self\' https://cloudflareinsights.com; object-src \'none\'; base-uri \'self\'; frame-ancestors \'none\'\n', encoding="utf-8", newline="\n")
    metadata = {"source":"firmware/keyboard/main/web", "base_path":"/" + base_path + "/",
                "ui_sha256":{name:hashlib.sha256((ui / name).read_bytes()).hexdigest() for name in ("index.html", "app.js", "style.css")}}
    (destination / "build-info.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8", newline="\n")
    return destination


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "dist-web")
    parser.add_argument("--base-path", default="pcb2")
    args = parser.parse_args()
    print("Hosted USB console: " + str(build(args.out.resolve(), args.base_path)))
