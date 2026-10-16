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
    output = Path(temporary)
    retired = module.build(output, "pcb2")
    assert retired.is_dir(), "Seed the previous deployment output"
    destination = module.build(output)
    assert destination.name == "keynest" and not retired.exists(), "Production must remove the retired entry"
    assert "/pcb2/" in (output / ".assetsignore").read_text()
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
    # GitHub Pages publishes only a fixed redirect; USB stays on the Cloudflare origin.
    import json
    from urllib.parse import urljoin
    pages_output = output / "github-pages"
    module.build(pages_output)  # Simulate upgrading an existing Pages console artifact.
    pages = module.build_pages_redirect(pages_output)
    assert {p.name for p in pages.iterdir()} == {"index.html"}
    redirect_html = (pages / "index.html").read_text(encoding="utf-8")
    target = "https://demo.terry878.org/keynest/"
    assert f'content="0;url={target}"' in redirect_html
    assert f'rel="canonical" href="{target}"' in redirect_html
    assert f'<a href="{target}">' in redirect_html
    assert "usb-serial.js" not in redirect_html and "<script" not in redirect_html
    headers = (output / "_headers").read_text()
    assert '/pcb2/*' not in headers and '/keynest/*' in headers
    assert json.loads((destination / "build-info.json").read_text())["base_path"] == "/keynest/"
    page_url = target
    for asset in re.findall(r'(?:src|href)="(\./[^"?]+)"', html):
        assert (destination / asset[2:]).is_file(), asset
        assert urljoin(page_url, asset).startswith(page_url)
    routes = json.loads((root / "wrangler.jsonc").read_text())["routes"]
    assert {r["pattern"] for r in routes} == {"demo.terry878.org/keynest", "demo.terry878.org/keynest/*"}
    # Never remove user files when cleaning an older generated entry.
    retired.mkdir()
    unknown = retired / "keep.txt"
    unknown.write_text("preserve")
    try:
        module.build(output)
        raise AssertionError("Unexpected files in a retired entry were deleted")
    except ValueError:
        assert unknown.read_text() == "preserve"
    unknown.unlink(); retired.rmdir()
    try:
        module.build_pages_redirect(root / "dist-web")
        raise AssertionError("Redirect overwrote the Cloudflare output")
    except ValueError:
        pass
    for invalid in ("../outside", "/", "", "pcb2/../outside"):
        try:
            module.build(Path(temporary), invalid)
            raise AssertionError("Unsafe base path accepted")
        except ValueError:
            pass
print("PASS: maintained controls, fixed Pages redirect, KeyNest-only routes/assets, safe legacy cleanup and relative links")
