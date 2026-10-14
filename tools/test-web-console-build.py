import importlib.util
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("build_console", root / "tools/build-web-console.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
cache = root / ".tools"
cache.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix="pcb2-web-build-", dir=cache) as temporary:
    assert Path(temporary).resolve().is_relative_to(cache.resolve())
    destination = module.build(Path(temporary))
    html = (destination / "index.html").read_text(encoding="utf-8")
    app = (destination / "app.js").read_text(encoding="utf-8")
    original = (root / "firmware/keyboard/main/web/app.js").read_text(encoding="utf-8")
    assert original in app, "Hosted app must retain the maintained UI without rewriting its behavior"
    assert 'href="./style.css"' in html and 'src="./app.js"' in html
    assert 'src="/app.js"' not in html and 'href="/style.css"' not in html
    assert html.index('src="./usb-serial.js"') < html.index('src="./usb-connect.js"') < html.index('src="./app.js"')
    assert 'id="mic-waveform"' in html and 'id="sound-upload"' in html and 'id="serial-choose"' in html
    files = {p.name for p in destination.iterdir()}
    assert files == {"index.html", "app.js", "style.css", "usb-serial.js", "usb-connect.js", "usb-connect.css", "build-info.json"}
    assert "serial=(self)" in (Path(temporary) / "_headers").read_text(encoding="utf-8")
    for invalid in ("../outside", "/", "", "pcb2/../outside"):
        try:
            module.build(Path(temporary), invalid)
            raise AssertionError("Unsafe base path accepted")
        except ValueError:
            pass
print("PASS: same firmware UI, relative assets, transport order, deployment file allowlist, safe base paths")
