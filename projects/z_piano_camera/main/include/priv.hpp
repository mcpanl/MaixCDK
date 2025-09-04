#pragma once

#include "maix_basic.hpp"
#include "maix_camera.hpp"
#include "maix_video.hpp"
#include "maix_ffmpeg.hpp"

#include "z_udp_server.hpp"
#include "z_tcp_server.hpp"
#include "z_display.hpp"
#include "z_encoder.hpp"
#include "z_record_control.hpp"
#include "z_http.hpp"
#include "z_video_stream.hpp"
#include "z_network.hpp"
#include "z_task_manager.hpp"

#include "EduScheduleManager.hpp"


struct Priv {
    z::UdpServer *udp_server;
    z::TcpServer *tcp_server;
    z::Display *display;
    z::Encoder *encoder;
    z::RecordControl *recordControl;
    z::VideoStream *video_stream;
    z::Network *network;
    z::TaskManager *task_manager;

    std::shared_ptr<z::Http> http;

    std::shared_ptr<edu::EduScheduleManager> manager;

    maix::camera::Camera *cam;
    maix::camera::Camera *cam2;
    maix::video::Encoder *_encoder;
    maix::ffmpeg::FFmpegPacker *ffmpeg_packer;
    maix::audio::Recorder *audio_recorder;

    uint64_t last_read_pcm_ms;
    uint64_t last_read_cam_ms;
    uint64_t audio_pts;
    uint64_t video_pts;

    bool isRecording;
};

extern Priv priv;
