// =========================
// File: EduScheduleManager.hpp
// C++20  / requires: nlohmann/json single-header (https://github.com/nlohmann/json)
// 功能概述：
// - 设备/教师/学生/课程/例外 的统一管理
// - 支持两套 API：原生 C++ 类型 + JSON 字符串（输入输出均为 JSON）
// - 支持 CSV 文件落盘与加载（devices.csv, teachers.csv, students.csv, schedules.csv, exceptions.csv）
// - 支持重复规则：一次性、每天、每周(星期几)、每月(几号)
// - 支持冲突检测：学生冲突 + 设备冲突（同一时段只能服务一名学生）
// - 查询：
//    1) 给定当前时间，返回最近1小时内开始的课程清单
//    2) 给定当前时间，返回明确状态（空闲/正在上某课）及关联信息
//    3) 给定当前时间，返回最近的前一个、下一个、下第二个课程
//
// 说明：为简洁起见，将全部类型与实现集中在两个文件（hpp/cpp）中。
// =========================
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <json.hpp>

namespace edu {
    // =============== 基础时间工具（C++20 <chrono>） ===============
    using Clock = std::chrono::system_clock;
    using TimePoint = std::chrono::time_point<Clock>;

    struct YMD {
        int year{1970}, month{1}, day{1};
    };

    struct HMS {
        int hour{0}, minute{0}, second{0};
    };

    // 解析 "YYYY-MM-DD" -> YMD
    YMD parseDate(const std::string &ymd);

    // 解析 "HH:MM" 或 "HH:MM:SS" -> HMS
    HMS parseTime(const std::string &hms);

    // 解析 "YYYY-MM-DD HH:MM:SS" -> TimePoint（本地时区按系统解释）
    TimePoint parseDateTime(const std::string &ts);

    // 组合 YMD + HMS -> TimePoint
    TimePoint makeDateTime(const YMD &d, const HMS &t);

    // 格式化 TimePoint -> "YYYY-MM-DD HH:MM:SS"
    std::string formatDateTime(const TimePoint &tp);

    // 格式化 YMD -> "YYYY-MM-DD"
    std::string formatDate(const YMD &d);

    // 格式化 HMS -> "HH:MM:SS"
    std::string formatTime(const HMS &t);

    // 计算星期几（1=Mon ... 7=Sun）
    int weekdayISO(const YMD &d);

    // YMD 加天数（正负）
    YMD addDays(const YMD &d, int delta);

    // 比较 YMD (按日期先后)
    int cmpDate(const YMD &a, const YMD &b);

    // =============== 实体定义 ===============
    struct Device {
        std::string id;
        std::string name;
    };

    struct Teacher {
        std::string id;
        std::string name;
        std::vector<std::string> studentIds; // 关系：教师 -> 学生
    };

    struct Student {
        std::string id;
        std::string name;
        std::vector<std::string> scheduleIds; // 关系：学生 -> 日程
    };

    enum class RecurrenceType { Once, Daily, Weekly, Monthly };

    struct ScheduleException {
        // 目前仅支持“取消”
        std::string id; // 唯一ID：ScheduleID + "#" + YYYY-MM-DD
        std::string scheduleId; // 所属日程
        YMD date; // 取消日期（仅日期维度）
    };

    struct Schedule {
        std::string id;
        std::string name;
        std::string remarks;
        std::string studentId;
        std::string teacherId;
        std::string deviceId;

        YMD startDate; // 生效开始日期（含）
        YMD endDate; // 生效结束日期（含）

        RecurrenceType recur{RecurrenceType::Once};
        // Weekly：用 weekDay (1=Mon..7=Sun)
        int weekDay{1};
        // Monthly：用 monthDay (1..31)
        int monthDay{1};

        HMS startTime; // 当天开始时间
        HMS endTime; // 当天结束时间（必须晚于开始时间）

        // 例外（取消）
        std::vector<std::string> exceptionIds;
    };

    // 单次实际发生的“课程实例”
    struct Occurrence {
        std::string scheduleId;
        std::string studentId;
        std::string teacherId;
        std::string deviceId;
        TimePoint start;
        TimePoint end;
    };

    // =============== 主管理器 ===============
    class EduScheduleManager {
    public:
        // CSV 根目录（默认当前工作目录）
        explicit EduScheduleManager(std::string csvRoot = ".");

        // ----------- CRUD（原生类型） -----------
        // Device
        void createDevice(const Device &d);

        void editDevice(const Device &d);

        void deleteDevice(const std::string &deviceId);

        // Teacher
        void createTeacher(const Teacher &t);

        void editTeacher(const Teacher &t);

        void deleteTeacher(const std::string &teacherId);

        // Student
        void createStudent(const Student &s);

        void editStudent(const Student &s);

        void deleteStudent(const std::string &studentId);

        std::vector<Student> getStudentsByTeacher(const std::string &deviceId, const std::string &teacherId);

        std::string jsonGetStudentsByTeacher(const std::string &s);

        // Schedule（创建/修改时会进行冲突检测）
        void createSchedule(const Schedule &s);

        void editSchedule(const Schedule &s);

        void deleteSchedule(const std::string &scheduleId);

        // Exception（取消）
        void createException(const ScheduleException &e);

        void deleteException(const std::string &exceptionId);

        // ----------- 查询（原生类型） -----------
        // 1) 给定当前时间，返回最近1小时内“开始”的课程 Occurrence 列表
        std::vector<Occurrence> occurrencesStartingWithin1h(const TimePoint &now) const;

        // 2) 给定当前时间，返回状态：空闲 / 正在上某课（含关联信息）
        struct Status {
            bool busy{false};
            std::optional<Occurrence> current; // 若 busy 为 true
        };

        Status statusAt(const TimePoint &now) const;

        // 3) 最近的前一个、下一个、下第二个课程
        struct Nearest {
            std::optional<Occurrence> prev;
            std::optional<Occurrence> next1;
            std::optional<Occurrence> next2;
        };

        Nearest nearestAround(const TimePoint &now) const;

        Nearest nearestByConditions(const TimePoint &now, const std::string &deviceId, const std::string &teacherId, const std::string &studentId) const;

        // ----------- JSON 版 API（输入/输出均为 JSON 字符串） -----------
        // 说明：统一返回 {"ok":true/false, "data":..., "error":"..."}
        std::string jsonCreateDevice(const std::string &jsonStr);

        std::string jsonEditDevice(const std::string &jsonStr);

        std::string jsonDeleteDevice(const std::string &jsonStr);

        std::string jsonCreateTeacher(const std::string &jsonStr);

        std::string jsonEditTeacher(const std::string &jsonStr);

        std::string jsonDeleteTeacher(const std::string &jsonStr);

        std::string jsonCreateStudent(const std::string &jsonStr);

        std::string jsonEditStudent(const std::string &jsonStr);

        std::string jsonDeleteStudent(const std::string &jsonStr);

        std::string jsonCreateSchedule(const std::string &jsonStr);

        std::string jsonEditSchedule(const std::string &jsonStr);

        std::string jsonDeleteSchedule(const std::string &jsonStr);

        std::string jsonCreateException(const std::string &jsonStr);

        std::string jsonDeleteException(const std::string &jsonStr);

        std::string jsonOccurrencesStartingWithin1h(const std::string &jsonNow);

        std::string jsonStatusAt(const std::string &jsonNow);

        std::string jsonNearestAround(const std::string &jsonNow);

        // ----------- CSV 持久化 -----------
        void saveAll() const; // 保存到 csvRoot
        void loadAll(); // 从 csvRoot 加载
        // CSV 工具
        static std::string csvEscape(const std::string &s);

        static std::string csvUnescape(const std::string &s);

        void printAllHierarchy(std::ostream &os = std::cout) const;

        // 获取设备下所有课程
        nlohmann::json getSchedulesByDevice(const std::string &deviceId) const;

        // 获取教师下所有课程
        nlohmann::json getSchedulesByTeacher(const std::string &deviceId, const std::string &teacherId) const;

        // 获取学生下所有课程
        nlohmann::json getSchedulesByStudent(const std::string &deviceId,
                                                                 const std::string &teacherId,
                                                                 const std::string &studentId) const;

        void assignStudentToTeacher(const std::string &teacherId, const std::string &studentId);

        // 获取课程例外情况
        nlohmann::json getExceptionsBySchedule(const std::string &scheduleId) const;

        nlohmann::json toHierarchyJson() const;

    private:
        std::string root;
        std::unordered_map<std::string, Device> devices;
        std::unordered_map<std::string, Teacher> teachers;
        std::unordered_map<std::string, Student> students;
        std::unordered_map<std::string, Schedule> schedules;
        std::unordered_map<std::string, ScheduleException> exceptions;

        // 关系维护辅助
        void bindTeacherStudent(const std::string &teacherId, const std::string &studentId, bool add);

        void bindStudentSchedule(const std::string &studentId, const std::string &scheduleId, bool add);

        // 冲突检测：给定一个日程，判断其定义是否会与现有课程发生冲突
        // 规则：同一学生，同一时间不可重叠；同一设备，同一时间不可重叠
        // 注意：编辑时需要允许自身 ID 的旧定义被覆盖，因此传入 allowScheduleId
        void ensureNoConflict(const Schedule &s,
                              const std::optional<std::string> &allowScheduleId = std::nullopt) const;

        // 生成某个日程在一定时间窗口内的 Occurrence（考虑取消）
        std::vector<Occurrence> expandOccurrences(const Schedule &s, TimePoint from, TimePoint to) const;

        // 时间窗口辅助（把所有课程在窗口内展开）
        std::vector<Occurrence> expandAll(TimePoint from, TimePoint to) const;

        // 工具：判断两个时间段是否重叠
        static bool overlap(const TimePoint &a1, const TimePoint &a2, const TimePoint &b1, const TimePoint &b2);

        // JSON 转换辅助
        static std::string toJsonStr(bool ok, const nlohmann::json &data, const std::string &err = "");
    };
} // namespace edu
