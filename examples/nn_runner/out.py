import numpy as np

ANCHORS = [
    [(10,13),(16,30),(33,23)],      # stride 8,  head 0
    [(30,61),(62,45),(59,119)],     # stride 16, head 1
    [(116,90),(156,198),(373,326)], # stride 32, head 2
]
SHAPES  = [(1,255,28,40),(1,255,14,20),(1,255,7,10)]
STRIDES = [8, 16, 32]
MODEL_W, MODEL_H = 320, 224
NC = 80
COCO = ["person","bicycle","car","motorcycle","airplane","bus","train","truck","boat",
    "traffic light","fire hydrant","stop sign","parking meter","bench","bird","cat",
    "dog","horse","sheep","cow","elephant","bear","zebra","giraffe","backpack",
    "umbrella","handbag","tie","suitcase","frisbee","skis","snowboard","sports ball",
    "kite","baseball bat","baseball glove","skateboard","surfboard","tennis racket",
    "bottle","wine glass","cup","fork","knife","spoon","bowl","banana","apple",
    "sandwich","orange","broccoli","carrot","hot dog","pizza","donut","cake","chair",
    "couch","potted plant","bed","dining table","toilet","tv","laptop","mouse",
    "remote","keyboard","cell phone","microwave","oven","toaster","sink",
    "refrigerator","book","clock","vase","scissors","teddy bear","hair drier","toothbrush"]

def sigmoid(x): return 1.0 / (1.0 + np.exp(-x.astype(np.float64)))

outputs = [np.fromfile(f'yolo_out{i}.bin', dtype=np.float32).reshape(SHAPES[i])
           for i in range(3)]

# ─── 1. per-anchor objectness stats ──────────────────────────────────────────
print("="*70)
print("1. Per-anchor objectness statistics")
print("="*70)
for hi, (out, ancs, st) in enumerate(zip(outputs, ANCHORS, STRIDES)):
    H, W = out.shape[2], out.shape[3]
    print(f"\nHead {hi}  stride={st}  grid={H}×{W}")
    for ai, (aw, ah) in enumerate(ancs):
        obj = sigmoid(out[0, ai*85+4])
        print(f"  anchor{ai} ({aw:3d},{ah:3d}): "
              f"max={obj.max():.4f}  mean={obj.mean():.5f}  "
              f">0.1:{(obj>0.1).sum():3d}  >0.3:{(obj>0.3).sum():3d}  >0.5:{(obj>0.5).sum():3d}")

# ─── 2. full decode at multiple thresholds ───────────────────────────────────
print()
print("="*70)
print("2. Full decode  (all anchors, conf_thresh sweep)")
print("="*70)
all_dets = []
for hi, (out, ancs, st) in enumerate(zip(outputs, ANCHORS, STRIDES)):
    H, W = out.shape[2], out.shape[3]
    for ai, (aw, ah) in enumerate(ancs):
        base = ai * 85
        obj = sigmoid(out[0, base+4])              # [H,W]
        positions = np.argwhere(obj >= 0.05)       # only above floor
        for h, w in positions:
            obj_c = float(obj[h, w])
            cls_p = sigmoid(out[0, base+5:base+85, h, w])
            mc = float(cls_p.max()); mid = int(cls_p.argmax())
            conf = obj_c * mc
            cx = (sigmoid(float(out[0, base+0, h, w])) * 2 - 0.5 + w) * st
            cy = (sigmoid(float(out[0, base+1, h, w])) * 2 - 0.5 + h) * st
            bw = (sigmoid(float(out[0, base+2, h, w])) * 2)**2 * aw
            bh = (sigmoid(float(out[0, base+3, h, w])) * 2)**2 * ah
            all_dets.append(dict(conf=conf, obj=obj_c, cls=mc, cls_id=mid,
                                 label=COCO[mid], cx=cx, cy=cy, bw=bw, bh=bh,
                                 h=h, w=w, hi=hi, ai=ai))
all_dets.sort(key=lambda x: -x['conf'])

for thresh in [0.50, 0.30, 0.20, 0.10, 0.05]:
    n = sum(1 for d in all_dets if d['conf'] >= thresh)
    print(f"  conf>={thresh:.2f} -> {n:4d} dets")

print("\nTop-20:")
for i, d in enumerate(all_dets[:20]):
    print(f"  #{i+1:2d} [{d['label']:18s}] conf={d['conf']:.4f} "
          f"obj={d['obj']:.4f} cls={d['cls']:.4f}  "
          f"head={d['hi']} anc={d['ai']} grid=({d['h']},{d['w']})  "
          f"cx={d['cx']:.1f} cy={d['cy']:.1f} bw={d['bw']:.1f} bh={d['bh']:.1f}")

# ─── 3. class scores for best detection ──────────────────────────────────────
print()
print("="*70)
print("3. Class probability top-10 for best detection")
print("="*70)
if all_dets:
    d = all_dets[0]
    base = d['ai'] * 85
    raw_cls = outputs[d['hi']][0, base+5:base+85, d['h'], d['w']]
    cls_p   = sigmoid(raw_cls)
    topk    = np.argsort(cls_p)[::-1][:10]
    print(f"Best det: head={d['hi']} anchor={d['ai']} grid=({d['h']},{d['w']})")
    for k in topk:
        bar = "█" * int(cls_p[k] * 40)
        print(f"  [{COCO[k]:20s}] raw={raw_cls[k]:7.4f}  sig={cls_p[k]:.4f}  {bar}")

# ─── 4. raw output distribution ──────────────────────────────────────────────
print()
print("="*70)
print("4. Raw output value percentiles")
print("="*70)
for hi, out in enumerate(outputs):
    flat = out.flatten()
    pct  = np.percentile(flat, [1,5,10,25,50,75,90,95,99])
    print(f"Head{hi}: "
          f"p1={pct[0]:.3f} p5={pct[1]:.3f} p10={pct[2]:.3f} "
          f"p25={pct[3]:.3f} med={pct[4]:.3f} p75={pct[5]:.3f} "
          f"p90={pct[6]:.3f} p95={pct[7]:.3f} p99={pct[8]:.3f}  "
          f"max={flat.max():.3f}")

