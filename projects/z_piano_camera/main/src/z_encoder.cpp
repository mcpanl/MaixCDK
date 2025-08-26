#include "z_encoder.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>

using namespace maix;



namespace z {
    uint64_t calculateRecommendedBitrate(int width, int height) {
        // 计算总像素数
        int totalPixels = width * height;

        // 基于常见分辨率到码率的映射，使用像素数作为基准
        // 参考标准（1080p约8Mbps）
        const double baseBitrate1080p = 8000000; // 8 Mbps for 1920x1080
        const int pixels1080p = 1920 * 1080;

        // 计算像素比例因子（使用平方根缩放，因为码率增长通常低于分辨率线性增长）
        double scaleFactor = std::sqrt(static_cast<double>(totalPixels) / pixels1080p);

        // 基础码率计算
        uint64_t recommendedBitrate = static_cast<uint64_t>(baseBitrate1080p * scaleFactor);

        // 根据常见分辨率应用经验调整系数
        if (totalPixels <= 640 * 480) { // SD及以下
            recommendedBitrate = std::max<uint64_t>(recommendedBitrate, 500000); // 最低500kbps
        } else if (totalPixels <= 1280 * 720) { // 720p
            recommendedBitrate = std::max<uint64_t>(recommendedBitrate, 1500000); // 最低1.5Mbps
        } else if (totalPixels <= 1920 * 1080) { // 1080p
            recommendedBitrate = std::max<uint64_t>(recommendedBitrate, 3000000); // 最低3Mbps
        } else if (totalPixels <= 3840 * 2160) { // 4K
            recommendedBitrate = std::max<uint64_t>(recommendedBitrate, 12000000); // 最低12Mbps
        } else { // 8K及以上
            recommendedBitrate = std::max<uint64_t>(recommendedBitrate, 40000000); // 最低40Mbps
        }

        // 确保不超过合理上限（约100Mbps）
        recommendedBitrate = std::min<uint64_t>(recommendedBitrate, 100000000);

        return recommendedBitrate;
    }

    Encoder::Encoder(camera::Camera* cam) {
        printf("==== Encoder ====\n");
        _bitrate = calculateRecommendedBitrate(cam->width(), cam->height());
        printf("Got bit = %d\n", _bitrate);

        encoder = new video::Encoder("", cam->width(), cam->height(), image::Format::FMT_YVU420SP, video::VideoType::VIDEO_H264, 24, 50, _bitrate, 1000, false, true, 1);

    }

    Encoder::~Encoder() {
        printf("~~~~ Encoder ~~~~\n");
        delete encoder;
    }
}