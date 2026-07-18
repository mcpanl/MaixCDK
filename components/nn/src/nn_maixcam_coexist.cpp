#include "maix_nn_coexist.hpp"

#if defined(PLATFORM_MAIXCAM)
extern "C" void X_MMF_SetVbBlkCntHint(unsigned int blk_cnt);
#endif

namespace maix::nn
{

void maixcam_mmf_set_vb_blk_cnt_hint(unsigned blk_cnt)
{
#if defined(PLATFORM_MAIXCAM)
    X_MMF_SetVbBlkCntHint((unsigned int)blk_cnt);
#else
    (void)blk_cnt;
#endif
}

}
