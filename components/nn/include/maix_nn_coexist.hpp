#pragma once

namespace maix::nn
{
    /**
     * MaixCam (CVI) + NPU: default X_MMF VI VB pool uses 12 full-sensor blocks; loading a
     * cvimodel afterwards can exhaust ION (mem_alloc_raw / ion ioctl OOM). Call this once
     * before any maix::display / maix::camera / nn that triggers the first MediaRuntime
     * acquire, so SYS init uses fewer blocks. Typical values: 6–10. 0 clears the hint
     * (default 12, unless env MAIX_MMF_VB_BLK_CNT is set — env overrides the hint).
     * @maixcdk maix.nn.maixcam_mmf_set_vb_blk_cnt_hint
     */
    void maixcam_mmf_set_vb_blk_cnt_hint(unsigned blk_cnt);
}
