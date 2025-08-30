#include "main.h"
#include "priv.hpp"

#include "maix_basic.hpp"
#include "maix_camera.hpp"
#include "maix_video.hpp"
#include "maix_ffmpeg.hpp"

#include "z_udp_server.hpp"
#include "z_tcp_server.hpp"
#include "z_display.hpp"
#include "z_encoder.hpp"
#include "z_record_control.hpp"
#include "../include/EduScheduleManager.hpp"
#include "mmf_vi_helper.hpp"

#include <deque>
#include <sys/stat.h>
#include <unistd.h>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <thread>
#include <chrono>
#include <random>

#include "httplib.h"
#include "json.hpp"

using namespace maix;
using namespace edu;

using json = nlohmann::json;
using namespace httplib;

Priv priv;


std::string generateRandomString(size_t length) {
    // 字符池
    const std::string chars =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789";

    // 使用 C++11 的随机数引擎和分布
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, chars.size() - 1);

    std::string result;
    result.reserve(length);

    for (size_t i = 0; i < length; ++i) {
        result += chars[distrib(gen)];
    }

    return result;
}


// 确保目录存在
static void ensure_dir(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        mkdir(path.c_str(), 0755);
    }
}

// 获取当前时间戳字符串
static std::string timestamp_str() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return oss.str();
}


void handlerTcpMessage(int fd, const std::vector<char>& data)
{
    std::string msg(data.begin(), data.end());
    std::cout << "[TCP RECV from " << fd << "] " << msg << std::endl;
}

image::Format cam_fmt = image::Format::FMT_YVU420SP;
image::Format cam2_fmt = image::Format::FMT_RGB888;
int cam_w = 1920;
int cam_h = 1080;
int cam2_w = 320;
int cam2_h = 180;
int cam_fps = 30;
int cam_buffer_num = 5;
int cam_bitrate = 9 * 1000 * 1000;

const int target_frame_interval_ms = 39 - 1;
static std::vector<uint8_t> g_sps_pps_buf;


uint64_t last_start_time = 0;       // 上次开始时间
const int record_duration_ms = 15 * 1 * 1000; // 录制 10 秒
const int start_interval_ms = 15 * 1 * 1000 + 5 * 1000; // 每 15 秒触发一次

int _main(int argc, char* argv[])
{
    mmf_deinit_v2(true);

    uint64_t t = time::time_s();

    log::info("Program start at %d", t);
    priv.manager = std::make_shared<EduScheduleManager>("/root/csv_data");

    priv.manager->loadAll();

    priv.manager->createDevice(Device{"Device_1","设备1"});

    Server svr;
    // GET /hello
    svr.Get("/getAll", [](const Request& req, Response& res) {
        json result = {
            {"errCode", 0},
            {"errMsg", ""},
            {"data", priv.manager->toHierarchyJson()}
        };
        res.set_content(result.dump(), "application/json");
    });

    // ================= 新增接口 =================
    // 自动检测或创建教师
    svr.Post("/teacher/autoCreate", [](const httplib::Request& req, httplib::Response& res) {
        json result;
        try {
            auto body = json::parse(req.body);
            std::string deviceId = body.value("deviceId", "");
            auto arr = body.value("teachers", json::array());

            for (auto &t : arr) {
                std::string id = t.value("id", "");
                std::string name = t.value("name", "");
                if (id.empty()) continue;

                auto teacher = edu::Teacher{id, name, {}};
                try {
#if 1
                    // 逻辑A：存在则更新姓名
                    priv.manager->editTeacher(teacher);
#else
                    // 逻辑B：存在则忽略
                    priv.manager->createTeacher(teacher);
#endif

                } catch (...) {
                    // 如果不存在会抛异常 -> 创建
                    priv.manager->createTeacher(teacher);
                }
            }
            priv.manager->saveAll();
            result = {{"errCode", 0}, {"errMsg", ""}, {"data", nullptr}};
        } catch (std::exception &e) {
            result = {{"errCode", 1001}, {"errMsg", e.what()}, {"data", nullptr}};
        }
        res.set_content(result.dump(), "application/json");
    });

    // 自动检测或创建学生
    svr.Post("/student/autoCreate", [](const httplib::Request& req, httplib::Response& res) {
        json result;
        try {
            auto body = json::parse(req.body);
            std::string deviceId = body.value("deviceId", "");
            std::string teacherId = body.value("teacherId", "");
            auto arr = body.value("students", json::array());

            for (auto &s : arr) {
                std::string id = s.value("id", "");
                std::string name = s.value("name", "");
                if (id.empty()) continue;

                auto stu = edu::Student{id, name, {}};
                try {
#if 1
                    priv.manager->editStudent(stu);
#else
                    priv.manager->createStudent(stu);
#endif
                } catch (...) {
                    priv.manager->createStudent(stu);
                }
                // 建立教师-学生关系
                priv.manager->assignStudentToTeacher(teacherId, id);
            }
            priv.manager->saveAll();
            result = {{"errCode", 0}, {"errMsg", ""}, {"data", nullptr}};
        } catch (std::exception &e) {
            result = {{"errCode", 1001}, {"errMsg", e.what()}, {"data", nullptr}};
        }
        res.set_content(result.dump(), "application/json");
    });

    // 获取教师下的学生列表
    svr.Get(R"(/teacher/(\w+)/device/(\w+)/students)", [](const httplib::Request& req, httplib::Response& res) {
        json result;
        try {
            std::string teacherId = req.matches[1];
            std::string deviceId = req.matches[2];

            auto studentsList = priv.manager->getStudentsByTeacher(deviceId, teacherId);

            json jstudents = json::array();
            for (const auto &st : studentsList) {
                jstudents.push_back({
                    {"id", st.id},
                    {"name", st.name},
                    {"scheduleIds", st.scheduleIds}
                });
            }

            json data;
            data["students"] = jstudents;

            result = {{"errCode", 0}, {"errMsg", ""}, {"data", data}};
        } catch (std::exception &e) {
            result = {{"errCode", 1001}, {"errMsg", e.what()}, {"data", nullptr}};
        }
        res.set_content(result.dump(), "application/json");
    });

    // 获取设备的课程列表
    svr.Get(R"(/device/(\w+)/schedules)", [](const httplib::Request& req, httplib::Response& res) {
        json result;
        try {
            std::string deviceId = req.matches[1];
            json data = priv.manager->getSchedulesByDevice(deviceId);
            result = {{"errCode", 0}, {"errMsg", ""}, {"data", data}};
        } catch (std::exception &e) {
            result = {{"errCode", 1001}, {"errMsg", e.what()}, {"data", nullptr}};
        }
        res.set_content(result.dump(), "application/json");
    });

    // 获取教师的课程列表
    svr.Get(R"(/teacher/(\w+)/device/(\w+)/schedules)", [](const httplib::Request& req, httplib::Response& res) {
        json result;
        try {
            std::string teacherId = req.matches[1];
            std::string deviceId = req.matches[2];
            json data = priv.manager->getSchedulesByTeacher(deviceId, teacherId);
            result = {{"errCode", 0}, {"errMsg", ""}, {"data", data}};
        } catch (std::exception &e) {
            result = {{"errCode", 1001}, {"errMsg", e.what()}, {"data", nullptr}};
        }
        res.set_content(result.dump(), "application/json");
    });

    // 获取学生的课程列表
    svr.Get(R"(/student/(\w+)/teacher/(\w+)/device/(\w+)/schedules)", [](const httplib::Request& req, httplib::Response& res) {
        json result;
        try {
            std::string studentId = req.matches[1];
            std::string teacherId = req.matches[2];
            std::string deviceId = req.matches[3];
            json data = priv.manager->getSchedulesByStudent(deviceId, teacherId, studentId);
            result = {{"errCode", 0}, {"errMsg", ""}, {"data", data}};
        } catch (std::exception &e) {
            result = {{"errCode", 1001}, {"errMsg", e.what()}, {"data", nullptr}};
        }
        res.set_content(result.dump(), "application/json");
    });

    // 创建/编辑课程
    svr.Post("/schedule/save", [](const httplib::Request& req, httplib::Response& res) {
        json result;
        try {
            auto body = json::parse(req.body);
            edu::Schedule s;
            s.id = body.value("courseId", "");
            s.deviceId = body.value("deviceId", "");
            s.teacherId = body.value("teacherId", "");
            s.studentId = body.value("studentId", "");
            s.startDate = edu::parseDate(body.value("startDate", "1970-01-01"));
            s.endDate   = edu::parseDate(body.value("endDate", "1970-01-01"));
            s.startTime = edu::parseTime(body.value("startTime", "00:00:00"));
            s.endTime   = edu::parseTime(body.value("endTime", "00:00:00"));
            s.recur = edu::RecurrenceType::Once;
            std::string recurType = body.value("recurType", "once");
            if (recurType=="daily") s.recur=edu::RecurrenceType::Daily;
            else if (recurType=="weekly") {s.recur=edu::RecurrenceType::Weekly; s.weekDay=body.value("weekDay",1);}
            else if (recurType=="monthly"){s.recur=edu::RecurrenceType::Monthly; s.monthDay=body.value("monthDay",1);}

            if (s.id.empty()) {
                // 创建
                s.id = generateRandomString(12);
                priv.manager->createSchedule(s);
            } else {
                priv.manager->editSchedule(s);
            }

            priv.manager->saveAll();

            result = {{"errCode",0},{"errMsg",""},{"data", {{"courseId", s.id}}}};
        } catch (std::exception &e) {
            result = {{"errCode",1001},{"errMsg",e.what()},{"data",nullptr}};
        }
        res.set_content(result.dump(), "application/json");
    });

    // 删除课程
    svr.Post("/schedule/delete", [](const httplib::Request& req, httplib::Response& res) {
        json result;
        try {
            auto body=json::parse(req.body);
            std::string courseId=body.value("courseId","");
            priv.manager->deleteSchedule(courseId);
            result={ {"errCode",0},{"errMsg",""},{"data",nullptr} };
        } catch(std::exception&e){
            result={ {"errCode",1001},{"errMsg",e.what()},{"data",nullptr} };
        }

        priv.manager->saveAll();

        res.set_content(result.dump(),"application/json");
    });

    // 获取课程例外情况
    svr.Get(R"(/schedule/(\w+)/exceptions)", [](const httplib::Request& req, httplib::Response& res) {
        json result;
        try{
            std::string courseId=req.matches[1];
            json data=json::array();
            auto hier=priv.manager->toHierarchyJson();
            for(auto &[eid,ex]: hier["exceptions"].items()){
                if(ex["scheduleId"]==courseId){
                    data.push_back(ex);
                }
            }
            result={ {"errCode",0},{"errMsg",""},{"data",data} };
        }catch(std::exception&e){
            result={ {"errCode",1001},{"errMsg",e.what()},{"data",nullptr} };
        }
        res.set_content(result.dump(),"application/json");
    });

    // 编辑课程例外情况
    svr.Post("/schedule/editExceptions", [](const httplib::Request& req, httplib::Response& res) {
        json result;
        try{
            auto body=json::parse(req.body);
            std::string courseId=body.value("courseId","");
            auto arr=body.value("dates",json::array());

            // 先删除旧的
            auto hier=priv.manager->toHierarchyJson();
            for(auto &[eid,ex]: hier["exceptions"].items()){
                if(ex["scheduleId"]==courseId){
                    priv.manager->deleteException(eid);
                }
            }
            // 新增
            for(auto &d: arr){
                edu::YMD y=edu::parseDate(d.get<std::string>());
                edu::ScheduleException se;
                se.scheduleId=courseId;
                se.id=courseId+"#"+d.get<std::string>();
                se.date=y;
                priv.manager->createException(se);
            }

            priv.manager->saveAll();

            result={ {"errCode",0},{"errMsg",""},{"data",nullptr} };
        }catch(std::exception&e){
            result={ {"errCode",1001},{"errMsg",e.what()},{"data",nullptr} };
        }
        res.set_content(result.dump(),"application/json");
    });

    std::thread server_thread([&]() {
        std::cout << "HTTP server running at http://localhost:8080\n";
        svr.listen("0.0.0.0", 8080);  // 阻塞，直到 svr.stop() 被调用
        std::cout << "HTTP server stopped.\n";
    });

    priv.display = new z::Display();
    priv.display->showLogo("assets/logo.png");

    priv.cam = new camera::Camera(cam_w, cam_h, cam_fmt, "", cam_fps, cam_buffer_num);
    priv.cam2 = priv.cam->add_channel(cam2_w, cam2_h, cam2_fmt, cam_fps, cam_buffer_num);

    priv.cam -> skip_frames(30);

    priv.udp_server = new z::UdpServer();

    priv.udp_server->start();

    priv.tcp_server = new z::TcpServer();
    priv.tcp_server->setMessageCallback([](int fd, const std::vector<char>& data) {
        handlerTcpMessage(fd, data);
    });

    priv.tcp_server->start();

    priv.encoder = new z::Encoder(priv.cam);

    uint32_t i = 0;


    priv.audio_recorder = new audio::Recorder();
    err::check_null_raise(priv.audio_recorder, "audio recorder init failed!");


    // priv._encoder = new video::Encoder("", cam_w, cam_h, image::Format::FMT_YVU420SP, video::VideoType::VIDEO_H264, 24, 50, priv.encoder->bitrate(), 1000, false, true, 1);

    priv.ffmpeg_packer = new ffmpeg::FFmpegPacker();
    err::check_null_raise(priv.ffmpeg_packer, "ffmpeg packer init failed");
    err::check_bool_raise(!priv.ffmpeg_packer->config("has_video", true), "ffmpeg packer config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("video_codec_id", AV_CODEC_ID_H264), "ffmpeg packer config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("video_width", cam_w), "ffmpeg packer config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("video_height", cam_h), "ffmpeg packer config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("video_bitrate", priv.encoder->bitrate()), "ffmpeg packer config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("video_fps", cam_fps), "ffmpeg packer config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("video_pixel_format", AV_PIX_FMT_NV21), "ffmpeg packer config failed!");

    err::check_bool_raise(!priv.ffmpeg_packer->config("has_audio", true), "ffmpeg packer config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("audio_sample_rate", 48000), "ffmpeg packer config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("audio_channels", 1), "ffmpeg packer config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("audio_bitrate", 128000), "ffmpeg packer config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("audio_format", AV_SAMPLE_FMT_S16), "ffmpeg packer config failed!");


    const int sample_rate = 48000;
    const int bytes_per_sample = 2 * 1; // 16bit * 单声道
    double sample_error_acc = 0.0;      // 累积误差（小数采样点）
    int64_t audio_pts = 0;              // 音频 PTS（单位：采样点）


    // --- FPS 统计 ---
    std::deque<long long> frame_times;
    const int fps_window_ms = 2000;  // 平滑窗口 2s

    priv.ffmpeg_packer->config2("path", "/root/20250825.mp4");

    priv.video_pts = 0;
    priv.audio_pts = 0;
    priv.last_read_cam_ms = 0;
    priv.last_read_pcm_ms = 0;

    uint64_t total_audio_samples_sent = 0;

    priv.recordControl = new z::RecordControl();

    bool a = false;

    uint64_t first_loop_time = time::ticks_ms();

    static auto start_time = std::chrono::steady_clock::now();
    uint64_t last_audio_ms = 0;

    while(!app::need_exit())
    {
        uint64_t now_ms = time::ticks_ms();
        auto loop_start = time::time_ms();  // 记录循环开始时间


        // 判断是否需要启动新录制
        if ((!priv.recordControl || priv.recordControl->state() == z::RecordControl::State::Ready) &&
            (now_ms - last_start_time >= start_interval_ms) && now_ms - first_loop_time > 2000) {

            ensure_dir("/root/record");
            std::string filename = "/root/record/" + timestamp_str() + ".mp4";

            priv.recordControl->setFileName(filename);
            priv.recordControl->start();
            last_start_time = now_ms;

            log::info("开始录制: %s", filename.c_str());
        }

        // 判断是否需要停止录制
        if (priv.recordControl && priv.recordControl->state() == z::RecordControl::State::Recording) {
            double elapsed = priv.recordControl->duration();
            if (elapsed >= record_duration_ms / 1000.0) {
                log::info("准备录制结束，持续: %.2f 秒", elapsed);
                priv.recordControl->stop();
                log::info("录制结束，持续: %.2f 秒", elapsed);
            }
        }

        bool found_venc_stream = false;
        void *frame = NULL;
        mmf_frame_info_t f;

        int ch = priv.cam->get_channel();

        int res = _mmf_vi_frame_pop(ch, &frame, &f, 10);

        auto capture_tp = std::chrono::steady_clock::now();
        uint64_t capture_ms = std::chrono::duration_cast<std::chrono::milliseconds>(capture_tp - start_time).count();

        if (res != 0 || frame == nullptr) {
            printf("Failed to get frame, skipping...\n");
            time::sleep_ms(5);
            continue;
        }

        // printf("capture_ms = %lu\n", capture_ms);

/*
        if (priv.recordControl->state() == z::RecordControl::State::Recording) {
            std::cout << "正在录制，时长: " << priv.recordControl->duration() << " 秒\n";
        } else {
            std::cout << "未在录制\n";
        }
*/

        mmf_vo_frame_push2(0, 0, 1, frame);
        // mmf_vo_frame_push2(0, 0, 2, frame);

        mmf_stream_t venc_stream = {0};

        bool isSuccess = priv.encoder->getFrame(frame, venc_stream);

        if (isSuccess) {
            const auto& sps_pps_buf = priv.encoder->getSpsPps();

            uint8_t* data = nullptr;
            int data_size = 0;
            if (venc_stream.count == 1) {
                data = venc_stream.data[0];
                data_size = venc_stream.data_size[0];
            } else if (venc_stream.count > 1) {
                data = venc_stream.data[2]; // 跳过 SPS/PPS
                data_size = venc_stream.data_size[2];
            }

            // 🔑 把关键数据交给状态机来处理
            priv.recordControl->handleVideoFrame(
                data,
                data_size,
                sps_pps_buf,
                capture_ms
            );

            priv.recordControl->handleAudioFrame(
                sample_rate,
                bytes_per_sample,
                capture_ms,
                last_audio_ms,
                sample_error_acc
            );
        }



#if 1
        if (isSuccess) {
            const auto& sps_pps_buf = priv.encoder->getSpsPps();

            uint8_t *data = NULL;
            int data_size = 0;
            if (venc_stream.count == 1) {
                data = venc_stream.data[0];
                data_size = venc_stream.data_size[0];
            } else if (venc_stream.count > 1) {
                data = venc_stream.data[2]; // 跳过 SPS/PPS，只取 IDR/P 帧
                data_size = venc_stream.data_size[2];
            }

            // TCP广播画面
            if (data_size > 0 && !sps_pps_buf.empty()) {
                // 拼接数据： [SPS+PPS] + [当前帧]
                std::vector<uint8_t> full_frame;
                full_frame.reserve(sps_pps_buf.size() + data_size);
                full_frame.insert(full_frame.end(), sps_pps_buf.begin(), sps_pps_buf.end());
                full_frame.insert(full_frame.end(), data, data + data_size);

                // 前4字节写帧长度（大端序）
                uint32_t frame_size = static_cast<uint32_t>(full_frame.size());
                std::vector<uint8_t> packet(4);
                packet[0] = (frame_size >> 24) & 0xFF;
                packet[1] = (frame_size >> 16) & 0xFF;
                packet[2] = (frame_size >> 8) & 0xFF;
                packet[3] = (frame_size) & 0xFF;

                // 拼成最终发送包
                std::vector<char> send_buf;
                send_buf.reserve(4 + frame_size);
                send_buf.insert(send_buf.end(), packet.begin(), packet.end());
                send_buf.insert(send_buf.end(), full_frame.begin(), full_frame.end());

                // 广播出去
                priv.tcp_server->broadcastBinary(send_buf);
            }
        }
#endif

        priv.encoder->freeFrame();

        i++;
        // 将 uint32_t 转换为字节数组（使用大端字节序，网络字节序）
        // std::vector<uint8_t> data(4);
        // data[0] = (i >> 24) & 0xFF;  // 最高有效字节
        // data[1] = (i >> 16) & 0xFF;
        // data[2] = (i >> 8) & 0xFF;
        // data[3] = i & 0xFF;          // 最低有效字节

        // priv.udp_server->broadcast(data);
        // priv.tcp_server->broadcastBinary(std::vector<char>(data.begin(), data.end()));
        // std::cout << "广播数字: " << (int)i << std::endl;

        // log::info("%d", time::time_s());

        // --- FPS 统计 ---
        auto now = time::time_ms();
        frame_times.push_back(now);

        // 删除窗口外的时间戳
        while (!frame_times.empty() && now - frame_times.front() > fps_window_ms) {
            frame_times.pop_front();
        }

        // 计算平滑 FPS
        if (frame_times.size() >= 2) {
            long long span = frame_times.back() - frame_times.front();
            double smooth_fps = (frame_times.size() - 1) * 1000.0 / span;

            priv.display -> setFps(smooth_fps);
            // priv.display -> runFrame();
            // printf("FPS (smoothed): %.2f\n", smooth_fps);
        }



        _mmf_vi_frame_free(ch, &frame);

        // --- 智能延时计算 ---
        auto loop_end = time::time_ms();
        int elapsed = (int)(loop_end - loop_start);  // 本轮循环耗时
        int sleep_time = target_frame_interval_ms - elapsed;
        if (sleep_time > 0) {
            // printf(": Sleep %d\n", sleep_time);
            time::sleep_ms(sleep_time);
        } else {
            // printf(": Not Sleep !\n");
        }
    }

    svr.stop();

    // 等待子线程退出
    server_thread.join();
    std::cout << "Program finished.\n";

    log::info("Program exit");
    delete priv.encoder;

    priv.udp_server->stop();
    priv.tcp_server->stop();

    delete priv.tcp_server;
    delete priv.udp_server;
    delete priv.display;
    delete priv.recordControl;

    return 0;
}

int main(int argc, char* argv[])
{
    sys::register_default_signal_handle();

    CATCH_EXCEPTION_RUN_RETURN(_main, -1, argc, argv);
}


