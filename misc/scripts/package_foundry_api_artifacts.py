#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import zipfile
from datetime import datetime, timezone
from pathlib import Path
from typing import TypedDict


class ArtifactSpec(TypedDict):
    name: str
    produced: str
    args: list[str]
    description: str


ARTIFACTS: list[ArtifactSpec] = [
    {
        "name": "extension_api.json",
        "produced": "extension_api.json",
        "args": ["--headless", "--dump-extension-api"],
        "description": "Foundry extension API without inline documentation.",
    },
    {
        "name": "extension_api_with_docs.json",
        "produced": "extension_api.json",
        "args": ["--headless", "--dump-extension-api-with-docs"],
        "description": "Foundry extension API including inline documentation.",
    },
    {
        "name": "foundry_extension_interface.h",
        "produced": "foundry_extension_interface.h",
        "args": ["--headless", "--dump-foundryextension-interface"],
        "description": "FoundryExtension C ABI header.",
    },
    {
        "name": "foundry_extension_interface.json",
        "produced": "foundry_extension_interface.json",
        "args": ["--headless", "--dump-foundryextension-interface-json"],
        "description": "FoundryExtension C ABI JSON description.",
    },
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Package Foundry API release artifacts.")
    parser.add_argument("--binary", required=True, help="Path to the Foundry editor binary.")
    parser.add_argument("--output-dir", required=True, help="Directory to write Foundry_v<version>_api.zip into.")
    parser.add_argument("--version", required=True, help="Release version string, without leading v.")
    parser.add_argument("--tag", required=True, help="Release tag.")
    parser.add_argument("--commit", required=True, help="Git commit SHA used for the release.")
    parser.add_argument(
        "--generated-at",
        default=None,
        help="UTC timestamp for metadata. Defaults to the current time.",
    )
    return parser.parse_args()


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_dump(binary: Path, staging_dir: Path, spec: ArtifactSpec, run_index: int) -> None:
    run_dir = staging_dir.parent / f"run-{run_index}"
    run_dir.mkdir()

    command = [str(binary), *spec["args"]]
    result = subprocess.run(
        command,
        cwd=run_dir,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if result.stdout:
        print(result.stdout, end="", file=sys.stderr)
    result.check_returncode()

    produced = run_dir / spec["produced"]
    if not produced.is_file():
        raise FileNotFoundError(f"Expected {produced} from command: {' '.join(command)}")

    shutil.copy2(produced, staging_dir / spec["name"])


def read_extension_api_header(path: Path) -> dict[str, object]:
    with path.open(encoding="utf-8") as file:
        data = json.load(file)
    header = data.get("header", {})
    if not isinstance(header, dict):
        return {}
    return header


def write_metadata(args: argparse.Namespace, binary: Path, staging_dir: Path) -> None:
    files = []
    for spec in ARTIFACTS:
        path = staging_dir / str(spec["name"])
        files.append(
            {
                "name": spec["name"],
                "description": spec["description"],
                "sha256": sha256(path),
                "size": path.stat().st_size,
            }
        )

    metadata = {
        "version": args.version,
        "tag": args.tag,
        "commit": args.commit,
        "generated_at": args.generated_at or utc_now(),
        "binary": str(binary),
        "commands": [
            {
                "output": spec["name"],
                "command": [str(binary), *spec["args"]],
            }
            for spec in ARTIFACTS
        ],
        "extension_api_header": read_extension_api_header(staging_dir / "extension_api.json"),
        "files": files,
    }

    with (staging_dir / "metadata.json").open("w", encoding="utf-8") as file:
        json.dump(metadata, file, indent=2, sort_keys=True)
        file.write("\n")


def write_zip(staging_dir: Path, output_dir: Path, version: str) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    zip_path = output_dir / f"Foundry_v{version}_api.zip"
    if zip_path.exists():
        zip_path.unlink()

    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(staging_dir.iterdir()):
            archive.write(path, arcname=path.name)

    return zip_path


def main() -> None:
    args = parse_args()
    binary = Path(args.binary).resolve()
    if not binary.is_file():
        raise FileNotFoundError(f"Foundry binary not found: {binary}")

    with tempfile.TemporaryDirectory(prefix="foundry-api-artifacts-") as tmp:
        staging_dir = Path(tmp) / "staging"
        staging_dir.mkdir()

        for index, spec in enumerate(ARTIFACTS):
            run_dump(binary, staging_dir, spec, index)

        write_metadata(args, binary, staging_dir)
        zip_path = write_zip(staging_dir, Path(args.output_dir), args.version)

    print(zip_path)


if __name__ == "__main__":
    main()
