# RK3566 RKNN YOLO Demo

This demo uses `components/nn_rk` to run YOLOv5n (`.rknn` or `.mud`) and prints each detection box in `xywh`.

## 1) Convert ONNX -> RKNN

In this directory:

```bash
python3 -m pip install rknn-toolkit2==2.3.2 ultralytics
python3 download_and_convert_yolov5n.py
```

By default it will:
- Download `yolov5n.pt` via Ultralytics and export **`yolov5n_224.onnx`** (224×224)
- Convert to `models/yolov5n_224.rknn`
- Write `models/yolov5n_224.mud` (anchors scaled for 224) and `models/coco80.txt`

Other sizes:

```bash
python3 download_and_convert_yolov5n.py --imgsz 200
```

Rockchip-hosted **640×640** ONNX (previous behavior):

```bash
python3 download_and_convert_yolov5n.py --legacy640
```

## 2) Build

```bash
maixcdk build -p rk3566
```

## 3) Run

```bash
./build/main/rk3566_rknn_yolo_demo models/yolov5n_224.mud cat1.jpg
```

Output includes:
- `class id/name`
- `score`
- `xywh` for each detection

And the visualization result is saved to `result.jpg`.

## Notes (Chinese)

For a detailed write-up of the 224 / Ultralytics export work, RKNN output layout differences, and debugging notes (including wrong-box causes), see **`DEVELOPMENT_SUMMARY.md`** §7–§8 in this directory.
