"""Read PCB2 repository metadata and verify images without opening USB or writing files."""
import argparse
import csv
import hashlib
import json
import re
from pathlib import Path


def contained(root, name):
    path = (root / name).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError("Metadata path leaves its expected directory")
    return path


def inspect(repo, verify_sources=False):
    header = repo / "firmware/components/pcb2_board/include/pcb2_pins.h"
    if not header.is_file() or not (repo / "tools/preview-keypad.py").is_file():
        raise ValueError("Not a PCB2 repository: required pin header or preview tool is missing")
    pins = {name: int(value) for name, value in re.findall(
        r"^\s*#define\s+(PCB2_PIN_\w+)\s+(\d+)\s*$", header.read_text(encoding="utf-8"), re.M)}
    if not pins:
        raise ValueError("No literal PCB2 GPIO definitions found; inspect the header manually")
    problems = []
    if len(set(pins.values())) != len(pins):
        problems.append("Duplicate GPIO assignments")
    partitions = []
    for line in (repo / "firmware/keyboard/partitions.csv").read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        fields = next(csv.reader([line], skipinitialspace=True))
        if len(fields) < 5:
            raise ValueError("Invalid partition row")
        partitions.append(dict(zip(("name", "type", "subtype", "offset", "size"),
                                   (field.strip() for field in fields[:5]))))
    artifacts = repo / "firmware/artifacts/keyboard"
    manifest = json.loads((artifacts / "manifest.json").read_text(encoding="utf-8"))
    checked = []
    for item in manifest["files"]:
        path = contained(artifacts, item["path"])
        valid = path.is_file() and path.stat().st_size == item["bytes"]
        valid = valid and hashlib.sha256(path.read_bytes()).hexdigest() == item["sha256"]
        checked.append({"file": item["path"], "verified": valid,
                        "flash_offset": item.get("flash_offset")})
        if not valid:
            problems.append("Artifact missing or hash/size mismatch: " + item["path"])
    if not checked:
        problems.append("Manifest lists no artifacts")
    sources = []
    if verify_sources:
        for item in manifest.get("sources", []):
            path = contained(repo, item["path"])
            match = "missing"
            if path.is_file():
                raw = path.read_bytes()
                if hashlib.sha256(raw).hexdigest() == item["sha256"]:
                    match = "exact"
                else:
                    match = "mismatch"
                    try:
                        raw.decode("utf-8")
                        lf = raw.replace(b"\r\n", b"\n")
                        variants = (lf, lf.replace(b"\n", b"\r\n"))
                        if any(hashlib.sha256(v).hexdigest() == item["sha256"] for v in variants):
                            match = "line_endings_only"
                    except UnicodeError:
                        pass
            sources.append({"file": item["path"], "match": match})
            if match in ("missing", "mismatch"):
                problems.append("Source missing or changed from manifest: " + item["path"])
        if not sources:
            problems.append("Manifest lists no sources")
    return {"repo": str(repo), "version": manifest.get("version"), "idf": manifest.get("idf"),
            "target": manifest.get("target"), "flash_size": manifest.get("flash_size"),
            "gpio": pins, "partitions": partitions, "artifacts": checked,
            "source_checks": sources, "problems": problems,
            "physical_validation": "Not performed; this helper only inspects repository files"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path.cwd(), help="PCB2 repository root")
    parser.add_argument("--verify-sources", action="store_true", help="Compare source files with image manifest")
    args = parser.parse_args()
    try:
        result = inspect(args.repo.resolve(), args.verify_sources)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(2, "PCB2 inspection failed: " + str(error) + "\n")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 1 if result["problems"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
