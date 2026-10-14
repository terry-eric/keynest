"""Build the hosted USB console from the maintained firmware UI; no firmware changes."""
import argparse
import hashlib
import json
import re
from html.parser import HTMLParser
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

class Panels(HTMLParser):
    """Retain maintained feature sections, replacing only hosted navigation."""
    def __init__(self, source):
        super().__init__(convert_charrefs=False)
        self.source, self.depth, self.sections = source, 0, []
        self.lines = [0] + [match.end() for match in re.finditer("\n", source)]
        self.feed(source)
    def position(self):
        line, column = self.getpos()
        return self.lines[line - 1] + column
    def handle_starttag(self, tag, attrs):
        if tag == "section":
            if not self.depth: self.start = self.position()
            self.depth += 1
    def handle_endtag(self, tag):
        if tag == "section":
            self.depth -= 1
            if not self.depth: self.sections.append(self.source[self.start:self.position() + len("</section>")])

def studio_html(ui, extra):
    sections = Panels((ui / "index.html").read_text(encoding="utf-8")).sections
    if len(sections) != 6: raise ValueError("Review changed firmware panels before building")
    board, mic, light, sound, mapping, recording = sections
    board = board.replace('data-trigger=', 'data-edit-key=')
    for key in range(4): board = board.replace(f'aria-label="測試 KEY{key+1} 的 LED 與音效"', f'aria-label="設定第 {key+1} 顆鍵"')
    board = board.replace("鍵盤預覽", "選擇要設定的按鍵")
    board = re.sub(r'<p class="caption">.*?</p>', '', board, count=1)
    mapping = mapping[mapping.index('<div class="mapping-header">'):mapping.index('<div class="apply-row">')]
    start = mic.index('<div class="mic-diagnostics">'); end = mic.index('</div>', start) + len('</div>')
    diagnostic = mic[start:end]; mic = mic[:start] + mic[end:]
    channel = mic.index('<label class="field-label" for="mic-channel">')
    mic = mic[:channel] + '<details class="advanced-mic"><summary>進階收音診斷</summary>' + diagnostic + mic[channel:].replace('</section>', '</details></section>')
    sound = sound.replace('<div class="custom-sound">', '<details class="custom-sound"><summary>使用自己的聲音</summary><div>')
    sound = sound.replace('\n      </div>\n    </section>', '\n      </div></details>\n    </section>')
    recording = recording.replace('<div class="sd-audio-files">', '<details class="sd-audio-files"><summary>SD 卡檔案與儲存狀態</summary><div>')
    recording = recording.replace('\n    </section>', '</details>\n    </section>')
    replacements = {"USB":(extra / "usb-connect.html").read_text(encoding="utf-8"), "BOARD":board, "MIC":mic, "LIGHT":light, "SOUND":sound, "MAPPINGS":mapping, "RECORDING":recording,
        "TEST_KEYS":''.join(f'<button class="button secondary" data-trigger="{i}" data-test-key="{i}" {"hidden" if i else ""}>試第 {i+1} 顆鍵的燈光與聲音</button>' for i in range(4))}
    html = (extra / "studio.html").read_text(encoding="utf-8")
    for name, fragment in replacements.items(): html = html.replace("{{" + name + "}}", fragment)
    if re.search(r"{{[A-Z_]+}}", html): raise ValueError("Unresolved studio slot")
    return html


def build(output, base_path="pcb2"):
    if not base_path or any(part in ("", ".", "..") for part in base_path.split("/")) or any(c not in "abcdefghijklmnopqrstuvwxyz0123456789-/" for c in base_path):
        raise ValueError("Use a relative lowercase base path")
    destination = output / base_path
    destination.mkdir(parents=True, exist_ok=True)
    ui = ROOT / "firmware/keyboard/main/web"
    extra = ROOT / "web-console"
    html = studio_html(ui, extra)
    (destination / "index.html").write_text(html, encoding="utf-8", newline="\n")
    # Supply the USB fetch implementation lexically; retain the entire maintained UI.
    app = (ui / "app.js").read_text(encoding="utf-8")
    (destination / "app.js").write_text('(function(fetch) {\n' + app + '\n})((...args) => window.PCB2Transport.fetch(...args));\n', encoding="utf-8", newline="\n")
    files = {"style.css":ui / "style.css", "gsap.min.js":extra / "vendor/gsap.min.js", **{name:extra / name for name in ("usb-serial.js", "usb-connect.js", "usb-connect.css", "studio.js", "studio.css", "companion.js")}}
    for name, source in files.items():
        (destination / name).write_bytes(source.read_bytes())
    (output / "_headers").write_text('/' + base_path + '/*\n  Permissions-Policy: serial=(self)\n  X-Content-Type-Options: nosniff\n  Referrer-Policy: no-referrer\n  X-Frame-Options: DENY\n  Content-Security-Policy: default-src \'self\'; script-src \'self\' https://static.cloudflareinsights.com; style-src \'self\' \'unsafe-inline\'; media-src \'self\' blob:; img-src \'self\' data:; connect-src \'self\' https://cloudflareinsights.com; object-src \'none\'; base-uri \'self\'; frame-ancestors \'none\'\n', encoding="utf-8", newline="\n")
    metadata = {"source":"firmware/keyboard/main/web", "base_path":"/" + base_path + "/",
                "studio_version":4,
                "ui_sha256":{name:hashlib.sha256((ui / name).read_bytes()).hexdigest() for name in ("index.html", "app.js", "style.css")}}
    (destination / "build-info.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8", newline="\n")
    return destination


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "dist-web")
    parser.add_argument("--base-path", default="pcb2")
    args = parser.parse_args()
    print("Hosted USB console: " + str(build(args.out.resolve(), args.base_path)))
