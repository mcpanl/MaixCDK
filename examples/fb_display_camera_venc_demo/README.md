# FB Display + Camera + VENC demo

分步计划见仓库根目录  
[`000_zonhor_fb_display_camera_venc_demo_plan.md`](../../../000_zonhor_fb_display_camera_venc_demo_plan.md)。

当前默认跑到 **Step 4**（VPSS G3→VENC Bind，无用户态 Send）。

| Step | 行为 |
|------|------|
| 0 | FB 预览 + Enable G3-Ch0 + `GetChnFrame` 探针 |
| 1 | + Create/Start VENC（USER VB 池） |
| 2 | + 用户态 `SendFrame` |
| 3 | + `GetStream` pack dump（手动 Send 路径） |
| 4 | + `depth=0` + Bind G3→VENC（Stop→Bind→Start）+ GetStream |
| ≥5 | 预留（落盘） |

## Build / deploy

```bash
export MAIXCDK_PATH=/path/to/MaixCDK
cd examples/fb_display_camera_venc_demo
maixcdk build --build-type=Debug -p zonhor --arch arm64
scp -r dist/ root@<board>:/root/
```

```bash
cd /root/dist/fb_display_camera_venc_demo_debug_arm64
export LD_LIBRARY_PATH=./dl_lib:$LD_LIBRARY_PATH
./fb_display_camera_venc_demo --step 4
```

## Expected success (Step 4)

```text
G3-Ch0 depth set to 0
StopRecvFrame → Bind G3-Ch0→VENC → StartRecvFrame
GetStream SPS/PPS/IDR + P…
Step4 GetStream stats: ok=30 … SPS=1 PPS=1 IDR=1 P=29
EncodedFrame: 30  EncFramePerSec≈16
Step4 SUCCESS
UnBind + Destroy ok
```

## Notes

- Bind **before** StartRecv (or Stop→Bind→Start). Start-then-Bind alone yields EncodedFrame=0.
- `GetStream` needs `QueryStatus` + preallocated `pstPack[]`.
- USER VB pool required for H264 ref frames (not COMMON).
