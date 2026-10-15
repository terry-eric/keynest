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
    assert files == {"index.html", "app.js", "style.css", "usb-serial.js", "usb-connect.js", "usb-connect.css", "studio.js", "studio.css", "actions.js", "build-info.json", "gsap.min.js"}
    assert "127.0.0.1" not in html and "companion.js" not in html, "Production must use one cloud console"
    import re
    ids=re.findall(r'\bid="([^"]+)"',html)
    assert len(ids)==len(set(ids)), "Duplicate controls break the maintained app"
    original_html=(root/"firmware/keyboard/main/web/index.html").read_text(encoding="utf-8")
    assert set(re.findall(r'\bid="([^"]+)"',original_html)).issubset(ids)
    assert 'id="page-keys"' in html and 'id="page-effects"' in html and 'id="page-audio"' in html
    assert html.count('data-edit-key=')==4 and html.count('data-trigger=')==4
    assert "serial=(self)" in (Path(temporary) / "_headers").read_text(encoding="utf-8")
    # GitHub Pages mounts the artifact directly at the repository URL, not /pcb2/.
    import json
    from urllib.parse import urljoin
    pages = module.build(Path(temporary), "keynest")
    pages_html = (pages / "index.html").read_text(encoding="utf-8")
    assert pages_html == html
    assert {p.name for p in pages.iterdir()} == files
    for name in files - {"build-info.json"}:
        assert (pages / name).read_bytes() == (destination / name).read_bytes(), name
    assert json.loads((pages / "build-info.json").read_text())["base_path"] == "/keynest/"
    headers = (Path(temporary) / "_headers").read_text()
    assert '/pcb2/*' in headers and '/keynest/*' in headers
    page_url = "https://terry-eric.github.io/keynest/"
    for asset in re.findall(r'(?:src|href)="(\./[^"?]+)"', pages_html):
        assert (pages / asset[2:]).is_file(), asset
        assert urljoin(page_url, asset).startswith(page_url)
    for invalid in ("../outside", "/", "", "pcb2/../outside"):
        try:
            module.build(Path(temporary), invalid)
            raise AssertionError("Unsafe base path accepted")
        except ValueError:
            pass
print("PASS: maintained controls, Cloudflare/Pages identical assets, repository-relative links, asset allowlist and safe paths")
