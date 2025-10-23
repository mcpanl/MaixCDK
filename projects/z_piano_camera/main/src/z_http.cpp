#include "z_http.hpp"
#include <cstdio>
#include "priv.hpp"

using json = nlohmann::json;
using namespace httplib;

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

namespace z {
    Http::Http() {
        printf("==== HTTP ====\n");

        // 启动网络检测线程
        network_monitor_thread = std::thread([this]() { network_monitor(); });

        // server = new Server();
        // start();
        // register_route();
    }

    Http::~Http() {
        printf("~~~~ HTTP ~~~~\n");
        stop_flag = true;
        if (network_monitor_thread.joinable())
            network_monitor_thread.join();

        stop();
    }

    void Http::start() {
        printf("after lock\n");
        std::lock_guard<std::mutex> lock(server_mutex);
        printf("before lock\n");
        if (server_running) return;

        // 确保每次重新创建 server 实例
        printf("check server\n");

        if (server) {
            // delete server;
        }

        printf("will new server\n");

        server = new Server();

        printf("register_router\n");
        register_route();

        printf("start thread\n");

        server_thread = std::thread([this]() {
            std::cout << "HTTP server running at http://0.0.0.0:8080\n";
            server_running = true;

            bool ok = server->listen("0.0.0.0", 8080);

            if (!ok) {
                std::cerr << "[HTTP] listen failed — will retry later\n";
            }

            std::cout << "HTTP server stopped.\n";
            server_running = false;
        });
    }


    void Http::stop() {
        std::lock_guard<std::mutex> lock(server_mutex);
        if (!server_running) return;

        if (server) {
            server->stop();   // 通知 listen() 退出
        }

        if (server_thread.joinable()) {
            server_thread.join();
        }

        server_running = false;
        delete server;
        server = nullptr;
    }

    void Http::network_monitor() {
        LanState last_state = LanState::DISCONNECTED;
        int connected_stable_count = 0; // 连续检测到 CONNECTED 的次数

        while (!stop_flag) {
            if (priv.network) {
                LanState state = priv.network->get_lan_state();

                if (state == LanState::CONNECTED) {
                    connected_stable_count++;
                    if (!server_running && connected_stable_count >= 1) {
                        // 连续1次连接成功（约 0.5 秒）才启动
                        std::cout << "[NET] Connected and stable, starting server...\n";
                        start();
                    }
                }
                else {
                    if (last_state == LanState::CONNECTED && server_running) {
                        std::cout << "[NET] Disconnected, stopping server...\n";
                        stop();
                    }
                    connected_stable_count = 0; // 连接断开就清零
                }

                last_state = state;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));  // 每0.5秒检测一次
        }
    }

    void Http::register_route() {

        server->set_pre_routing_handler([](const httplib::Request &req, httplib::Response &res) {
            std::cout << "[HTTP] Incoming request: "
                      << req.method << " " << req.path << std::endl;
            return httplib::Server::HandlerResponse::Unhandled;
            // 不处理，让它继续走后面的路由匹配
        });


        server->Get("/getAll", [](const Request& req, Response& res) {
            json result = {
                {"errCode", 0},
                {"errMsg", ""},
                {"data", priv.manager->toHierarchyJson()}
            };
            res.set_content(result.dump(), "application/json");
        });

        // ================= 新增接口 =================
        // 自动检测或创建教师
        server->Post("/teacher/autoCreate", [](const httplib::Request& req, httplib::Response& res) {
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
    #if 0
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
        server->Post("/student/autoCreate", [](const httplib::Request& req, httplib::Response& res) {
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
        server->Get(R"(/teacher/(\w+)/device/(\w+)/students)", [](const httplib::Request& req, httplib::Response& res) {
            json result;
            try {
                // 打印基本的请求信息
                std::cout << "[LOG] 收到请求: " << req.method << " " << req.path << std::endl;

                // 打印 URL 路径参数
                std::string teacherId = req.matches[1];
                std::string deviceId = req.matches[2];
                std::cout << "[LOG] teacherId: " << teacherId << ", deviceId: " << deviceId << std::endl;

                // 打印 query 参数（如果有）
                if (!req.params.empty()) {
                    std::cout << "[LOG] Query 参数: " << std::endl;
                    for (const auto &p : req.params) {
                        std::cout << "    " << p.first << " = " << p.second << std::endl;
                    }
                } else {
                    std::cout << "[LOG] 无 Query 参数" << std::endl;
                }

                // 业务逻辑
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
                // 捕获异常并打印
                std::cerr << "[ERROR] 异常: " << e.what() << std::endl;
                result = {{"errCode", 1001}, {"errMsg", e.what()}, {"data", nullptr}};
            }

            // 打印响应 JSON
            std::cout << "[LOG] 响应内容: " << result.dump(4) << std::endl;

            res.set_content(result.dump(), "application/json");
        });

        // 获取设备的课程列表
        server->Get(R"(/device/(\w+)/schedules)", [](const httplib::Request& req, httplib::Response& res) {
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
        server->Get(R"(/teacher/(\w+)/device/(\w+)/schedules)", [](const httplib::Request& req, httplib::Response& res) {
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
        server->Get(R"(/student/(\w+)/teacher/(\w+)/device/(\w+)/schedules)", [](const httplib::Request& req, httplib::Response& res) {
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
        server->Post("/schedule/save", [](const httplib::Request& req, httplib::Response& res) {
            json result;
            try {
                auto body = json::parse(req.body);
                edu::Schedule s;
                s.id = body.value("id", "");
                s.name = body.value("name", "");
                s.remarks = body.value("remarks", "");
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
        server->Post("/schedule/delete", [](const httplib::Request& req, httplib::Response& res) {
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
        server->Get(R"(/schedule/(\w+)/exceptions)", [](const httplib::Request& req, httplib::Response& res) {
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
        server->Post("/schedule/editExceptions", [](const httplib::Request& req, httplib::Response& res) {
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

        server->Get("/upload/all", [](const httplib::Request& req, httplib::Response& res) {
            json result;
            try {
                std::ifstream ifs("/root/all.json");
                if (!ifs.is_open()) {
                    throw std::runtime_error("Failed to open /root/all.json");
                }

                json file_data;
                ifs >> file_data;

                result = {
                    {"errCode", 0},
                    {"errMsg", ""},
                    {"data", file_data}
                };
            } catch (std::exception &e) {
                result = {
                    {"errCode", 1001},
                    {"errMsg", e.what()},
                    {"data", nullptr}
                };
            }

            res.set_content(result.dump(), "application/json");
        });

        server->Get("/upload/success", [](const httplib::Request& req, httplib::Response& res) {
            json result;
            try {
                std::ifstream ifs("/root/success.json");
                if (!ifs.is_open()) {
                    throw std::runtime_error("Failed to open /root/success.json");
                }

                json file_data;
                ifs >> file_data;

                result = {
                    {"errCode", 0},
                    {"errMsg", ""},
                    {"data", file_data}
                };
            } catch (std::exception &e) {
                result = {
                    {"errCode", 1001},
                    {"errMsg", e.what()},
                    {"data", nullptr}
                };
            }

            res.set_content(result.dump(), "application/json");
        });

        server->Post("/file/delete", [](const httplib::Request& req, httplib::Response& res) {
            json result;
            try {
                auto body = json::parse(req.body);
                std::string filename = body.value("filename", "");
                if (filename.empty()) {
                    result = { {"errCode",1002}, {"errMsg","filename is empty"}, {"data",nullptr} };
                    res.set_content(result.dump(), "application/json");
                    return;
                }

                std::vector<std::string> candidates = {
                    "/root/record_task/@" + filename,
                    "/root/record_task/#" + filename,
                    "/root/record_task/." + filename,
                    "/root/record_task/@" + filename + ".sha256",
                    "/root/record_task/#" + filename + ".sha256",
                    "/root/record_task/." + filename + ".sha256"
                };

                bool deleted = false;
                for (const auto& path : candidates) {
                    if (std::remove(path.c_str()) == 0) {
                        deleted = true;
                    }
                }

                if (deleted) {
                    result = { {"errCode",0}, {"errMsg",""}, {"data",nullptr} };
                } else {
                    result = { {"errCode",1003}, {"errMsg","no file deleted"}, {"data",nullptr} };
                }
            } catch (std::exception& e) {
                result = { {"errCode",1001}, {"errMsg",e.what()}, {"data",nullptr} };
            }
            res.set_content(result.dump(), "application/json");
        });
    }
}