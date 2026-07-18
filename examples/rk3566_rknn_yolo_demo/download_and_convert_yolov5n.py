#!/usr/bin/env python3
"""
Download YOLOv5n weights, export ONNX at a chosen square size (default 224), convert to RKNN for RK3566.

Requires:
  python3 -m pip install rknn-toolkit2==2.3.2 ultralytics

Usage:
  python3 download_and_convert_yolov5n.py
  python3 download_and_convert_yolov5n.py --imgsz 200
  python3 download_and_convert_yolov5n.py --legacy640
"""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path
from urllib.request import urlretrieve

from rknn.api import RKNN


ROOT = Path(__file__).resolve().parent
MODEL_DIR = ROOT / "models"
LABELS_PATH = MODEL_DIR / "coco80.txt"

# YOLOv5 default anchors for 640x640 — scale by (imgsz/640) for other export sizes without re-running autoanchor.
ANCHORS_REF_SIZE = 640
ANCHORS_640 = (
    "10,13, 16,30, 33,23, 30,61, 62,45, 59,119, 116,90, 156,198, 373,326"
)

# From Rockchip rknn_model_zoo yolov5n table (640x640 only).
ONNX_URL_640 = (
    "https://ftrg.zbox.filez.com/v2/delivery/data/"
    "95f00b0fc900458ba134f8b180b3f7a1/examples/yolov5/yolov5n.onnx"
)

COCO80 = [
    "person",
    "bicycle",
    "car",
    "motorcycle",
    "airplane",
    "bus",
    "train",
    "truck",
    "boat",
    "traffic light",
    "fire hydrant",
    "stop sign",
    "parking meter",
    "bench",
    "bird",
    "cat",
    "dog",
    "horse",
    "sheep",
    "cow",
    "elephant",
    "bear",
    "zebra",
    "giraffe",
    "backpack",
    "umbrella",
    "handbag",
    "tie",
    "suitcase",
    "frisbee",
    "skis",
    "snowboard",
    "sports ball",
    "kite",
    "baseball bat",
    "baseball glove",
    "skateboard",
    "surfboard",
    "tennis racket",
    "bottle",
    "wine glass",
    "cup",
    "fork",
    "knife",
    "spoon",
    "bowl",
    "banana",
    "apple",
    "sandwich",
    "orange",
    "broccoli",
    "carrot",
    "hot dog",
    "pizza",
    "donut",
    "cake",
    "chair",
    "couch",
    "potted plant",
    "bed",
    "dining table",
    "toilet",
    "tv",
    "laptop",
    "mouse",
    "remote",
    "keyboard",
    "cell phone",
    "microwave",
    "oven",
    "toaster",
    "sink",
    "refrigerator",
    "book",
    "clock",
    "vase",
    "scissors",
    "teddy bear",
    "hair drier",
    "toothbrush",
]


def paths_for(imgsz: int | None, legacy640: bool) -> tuple[Path, Path, Path]:
    if legacy640:
        return (ROOT / "yolov5n.onnx", MODEL_DIR / "yolov5n.rknn", MODEL_DIR / "yolov5n.mud")
    s = int(imgsz)
    return (
        ROOT / f"yolov5n_{s}.onnx",
        MODEL_DIR / f"yolov5n_{s}.rknn",
        MODEL_DIR / f"yolov5n_{s}.mud",
    )


def scaled_anchors(anchor_csv: str, ref_sz: int, out_sz: int) -> str:
    k = float(out_sz) / float(ref_sz)
    parts: list[str] = []
    for t in anchor_csv.replace(",", " ").split():
        t = t.strip()
        if not t:
            continue
        parts.append(f"{float(t) * k:.6g}")
    return ", ".join(parts)


def ensure_labels():
    MODEL_DIR.mkdir(parents=True, exist_ok=True)
    LABELS_PATH.write_text("\n".join(COCO80) + "\n", encoding="utf-8")


def ensure_onnx_ultralytics(onnx_path: Path, imgsz: int) -> None:
    if onnx_path.exists():
        print(f"[skip] onnx exists: {onnx_path}")
        return
    try:
        from ultralytics import YOLO
    except ImportError as e:
        raise SystemExit(
            "Missing ultralytics. Install: python3 -m pip install ultralytics"
        ) from e

    print(f"[ultralytics] load yolov5n.pt (auto-download) -> ONNX {imgsz}x{imgsz}")
    model = YOLO("yolov5n.pt")
    exported = model.export(
        format="onnx",
        imgsz=imgsz,
        simplify=True,
        opset=12,
    )
    out = Path(str(exported))
    onnx_path.parent.mkdir(parents=True, exist_ok=True)
    if out.resolve() != onnx_path.resolve():
        shutil.copy2(out, onnx_path)
    print(f"[ok] saved to {onnx_path}")


def ensure_onnx_rockchip(onnx_path: Path) -> None:
    if onnx_path.exists():
        print(f"[skip] onnx exists: {onnx_path}")
        return
    print(f"[download] {ONNX_URL_640}")
    urlretrieve(ONNX_URL_640, onnx_path)
    print(f"[ok] saved to {onnx_path}")


def convert_rknn(onnx_path: Path, rknn_path: Path) -> None:
    MODEL_DIR.mkdir(parents=True, exist_ok=True)
    print("[rknn] init")
    rknn = RKNN(verbose=True)
    ret = rknn.config(target_platform="rk3566")
    if ret != 0:
        raise RuntimeError(f"rknn.config failed: {ret}")

    print("[rknn] load onnx")
    ret = rknn.load_onnx(model=str(onnx_path))
    if ret != 0:
        raise RuntimeError(f"rknn.load_onnx failed: {ret}")

    print("[rknn] build")
    ret = rknn.build(do_quantization=False)
    if ret != 0:
        raise RuntimeError(f"rknn.build failed: {ret}")

    print("[rknn] export")
    ret = rknn.export_rknn(str(rknn_path))
    if ret != 0:
        raise RuntimeError(f"rknn.export_rknn failed: {ret}")
    rknn.release()
    print(f"[ok] exported: {rknn_path}")


def write_mud(rknn_path: Path, mud_path: Path, anchors_line: str) -> None:
    mud = f"""[basic]
type = rknn
model = {rknn_path.name}

[extra]
model_type = yolov5
input_type = rgb
mean = 0, 0, 0
scale = 0.00392156862745098, 0.00392156862745098, 0.00392156862745098
input_channel = chw
labels = {LABELS_PATH.name}
anchors = {anchors_line}
"""
    mud_path.write_text(mud, encoding="utf-8")
    print(f"[ok] wrote mud: {mud_path}")


def main() -> None:
    parser = argparse.ArgumentParser(description="YOLOv5n ONNX -> RKNN + mud for RK3566")
    parser.add_argument(
        "--imgsz",
        type=int,
        default=224,
        help="Square input size for Ultralytics export (default: 224)",
    )
    parser.add_argument(
        "--legacy640",
        action="store_true",
        help="Download Rockchip 640x640 ONNX instead of Ultralytics export",
    )
    args = parser.parse_args()

    if args.legacy640:
        imgsz = 640
        onnx_path, rknn_path, mud_path = paths_for(None, True)
        anchors_line = ANCHORS_640
        ensure_labels()
        ensure_onnx_rockchip(onnx_path)
    else:
        imgsz = args.imgsz
        if imgsz <= 0:
            raise SystemExit("--imgsz must be positive")
        onnx_path, rknn_path, mud_path = paths_for(imgsz, False)
        anchors_line = scaled_anchors(ANCHORS_640, ANCHORS_REF_SIZE, imgsz)
        ensure_labels()
        ensure_onnx_ultralytics(onnx_path, imgsz)

    convert_rknn(onnx_path, rknn_path)
    write_mud(rknn_path, mud_path, anchors_line)

    print("\nDone. Run demo with:")
    print(f"  ./build/main/rk3566_rknn_yolo_demo {mud_path} <image.jpg>")


if __name__ == "__main__":
    main()
