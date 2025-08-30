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
        server = new Server();
        start();
        register_route();
    }

    Http::~Http() {
        printf("~~~~ HTTP ~~~~\n");
        stop();
        delete server;
    }

    void Http::start() {
        server_thread = std::thread([this]() {
            std::cout << "HTTP server running at http://localhost:8080\n";
            server->listen("0.0.0.0", 8080);  // 阻塞
            std::cout << "HTTP server stopped.\n";
        });
    }

    void Http::stop() {
        server->stop();
        if (server_thread.joinable()) {
            server_thread.join();
        }
    }

    void Http::register_route() {
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
    }
}