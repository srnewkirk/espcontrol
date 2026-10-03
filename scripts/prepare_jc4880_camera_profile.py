#!/usr/bin/env python3
"""Prepare an opt-in eight-slot camera deployment in a disposable source snapshot.

This deliberately does not change the stock device manifest. Personal cards and
credentials remain separate. Re-running the command is safe.
"""
from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys


def prepare(root: Path, node: str) -> None:
    sys.path.insert(0, str(root / "scripts"))
    import build
    import generate_device_slots
    import product_schema

    slug = "guition-esp32-p4-jc4880p443"
    device = copy.deepcopy(next(d for d in product_schema.slot_devices() if d["slug"] == slug))
    device.update(slots=8, cols=2, portrait_cols=4, grid="4x2")
    folder = root / "devices" / slug
    (folder / "packages.yaml").write_text(generate_device_slots.package_file_text(device))
    sensors = folder / "device/sensors.yaml"
    sensors.write_text(generate_device_slots.replace_sensor_blocks(sensors.read_text(), device))

    hardware = folder / "device/device.yaml"
    text = hardware.read_text().replace("-DESPCONTROL_MAX_GRID_SLOTS=6", "-DESPCONTROL_MAX_GRID_SLOTS=8")
    text = text.replace('initial_option: "180"', 'initial_option: "0"')
    marker = "# ---------------------------------------------------------------------------\n# ESPHome core"
    if marker not in text:
        raise ValueError("P4 hardware structure changed; review native binding insertion")
    extra = ""
    for slot in (7, 8):
        if f"      - config: button_{slot}_config\n" in text:
            continue
        chunks = [f"subpage_{slot}_config", f"subpage_{slot}_config_ext"]
        chunks += [f"subpage_{slot}_config_ext_{n}" for n in range(2, 8)]
        extra += f"      - config: button_{slot}_config\n        subpage_chunks: [{', '.join(chunks)}]\n"
    hardware.write_text(text.replace(marker, extra + marker, 1))

    layout = folder / "device/lvgl.yaml"
    text = layout.read_text().replace("grid_rows: [FR(1), FR(1), FR(1)]", "grid_rows: [FR(1), FR(1), FR(1), FR(1)]")
    for slot in (7, 8):
        widget = f'        - !include {{ file: ../../../common/device/button_widget.yaml, vars: {{ num: "{slot}" }} }}\n'
        if widget not in text:
            text += widget
    layout.write_text(text)

    profiles = build.build_web_devices()
    profiles[slug].update(slots=8, cols=2, rows=4)
    profiles[slug].setdefault("features", {})["cameraMotion"] = True
    profiles[slug]["portrait"].update(cols=4, rows=2)
    output = root / ".cache/jc4880-native-web"
    request = {"outputDir": str(output), "devices": profiles,
               "embeddedMdiStyles": build.embedded_web_mdi_styles(), "testHooks": False}
    subprocess.run([node, str(root / "scripts/build_web_bundle.js")],
                   input=json.dumps(request), text=True, check=True)
    shutil.copyfile(output / "embedded.js", root / "docs/public/webserver/embedded/www.js")
    print("Prepared native eight-slot P4 deployment; use web_server.js_url: '' with the embedded editor.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True, help="Disposable repository snapshot to modify")
    parser.add_argument("--node", default="node", help="Node executable with the locked esbuild dependency")
    args = parser.parse_args()
    prepare(args.root.resolve(), args.node)
