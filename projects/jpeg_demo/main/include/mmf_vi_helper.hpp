#ifndef MMF_VI_HELPER_HPP
#define MMF_VI_HELPER_HPP

#include "sophgo_middleware.hpp"

int _mmf_vi_frame_pop(int ch, void **frame_info, mmf_frame_info_t *frame_info_mmap, int block_ms);
void _mmf_vi_frame_free(int ch, void **frame_info);

#endif // MMF_VI_HELPER_HPP
