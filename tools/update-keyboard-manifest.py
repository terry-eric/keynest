"""Refresh the build manifest after a successful keyboard build; does not build or flash."""
import argparse
import hashlib
import json
from datetime import date
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--version",required=True);args=parser.parse_args()
manifest=ROOT/"firmware/artifacts/keyboard/manifest.json"
data=json.loads(manifest.read_text(encoding="utf-8"));data.update(built_at=date.today().isoformat(),version=args.version,validation="Host tests and ESP-IDF build only; this image has not been flashed or hardware-verified.")
data.pop("physical_validation",None)  # A new build cannot inherit another image's hardware results.
data.pop("validated",None)
for artifact in data["files"]:
    path=manifest.parent/artifact["path"]
    if not path.resolve().is_relative_to(manifest.parent.resolve()):raise ValueError("Invalid artifact path")
    artifact.update(bytes=path.stat().st_size,sha256=hashlib.sha256(path.read_bytes()).hexdigest())
sources={entry["path"] for entry in data["sources"]} | {"firmware/keyboard/main/macro_store.c","firmware/keyboard/main/macro_store.h"}
data["sources"]=[{"path":name,"sha256":hashlib.sha256((ROOT/name).read_bytes()).hexdigest()} for name in sorted(sources)]
manifest.write_text(json.dumps(data,ensure_ascii=False,indent=2)+"\n",encoding="utf-8",newline="\n")
print(args.version+": "+str(len(data["files"]))+" artifacts, "+str(len(data["sources"]))+" source hashes")
