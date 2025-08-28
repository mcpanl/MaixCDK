// =========================
// File: EduScheduleManager.cpp
// =========================

#include "EduScheduleManager.hpp"
#include <filesystem>

using nlohmann::json;

namespace edu {
    // ---------------- 时间工具实现 ----------------
    static std::tm toTm(const YMD &d, const HMS &t) {
        std::tm tm{};
        tm.tm_year = d.year - 1900;
        tm.tm_mon = d.month - 1;
        tm.tm_mday = d.day;
        tm.tm_hour = t.hour;
        tm.tm_min = t.minute;
        tm.tm_sec = t.second;
        return tm;
    }

    YMD parseDate(const std::string &ymd) {
        YMD d;
        char dash1, dash2;
        std::istringstream ss(ymd);
        ss >> d.year >> dash1 >> d.month >> dash2 >> d.day;
        if (!ss || dash1 != '-' || dash2 != '-') throw std::runtime_error("Bad date: " + ymd);
        return d;
    }

    HMS parseTime(const std::string &hms) {
        HMS t;
        std::istringstream ss(hms);
        char c1 = '\0', c2 = '\0';

        // 尝试读取时分秒
        ss >> t.hour >> c1 >> t.minute >> c2 >> t.second;

        // 如果第二个分隔符不存在，说明只有时分
        if (ss && c1 == ':' && c2 != ':') {
            // 重置流状态并重新解析时分格式
            ss.clear();
            ss.seekg(0);
            ss >> t.hour >> c1 >> t.minute;
            t.second = 0; // 秒数设为默认值
        }

        // 验证解析结果
        if (!ss || c1 != ':' || (c2 != '\0' && c2 != ':') ||
            t.hour < 0 || t.hour > 23 ||
            t.minute < 0 || t.minute > 59 ||
            t.second < 0 || t.second > 59) {
            throw std::runtime_error("Bad time: " + hms);
        }

        return t;
    }

    TimePoint makeDateTime(const YMD &d, const HMS &t) {
        std::tm tm = toTm(d, t);
        std::time_t tt = std::mktime(&tm); // 本地时区
        if (tt == -1) throw std::runtime_error("Bad datetime");
        return Clock::from_time_t(tt);
    }

    TimePoint parseDateTime(const std::string &ts) {
        // 允许 "YYYY-MM-DD HH:MM" 或 "YYYY-MM-DD HH:MM:SS"
        auto pos = ts.find(' ');
        if (pos == std::string::npos) throw std::runtime_error("Bad datetime: " + ts);
        auto d = parseDate(ts.substr(0, pos));
        auto t = parseTime(ts.substr(pos + 1));
        return makeDateTime(d, t);
    }

    std::string formatDateTime(const TimePoint &tp) {
        std::time_t tt = Clock::to_time_t(tp);
        std::tm *tm = std::localtime(&tt);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
        return buf;
    }

    std::string formatDate(const YMD &d) {
        std::ostringstream ss;
        ss << std::setfill('0') << std::setw(4) << d.year << "-" << std::setw(2) << d.month << "-" << std::setw(2) << d.
                day;
        return ss.str();
    }

    std::string formatTime(const HMS &t) {
        std::ostringstream ss;
        ss << std::setfill('0') << std::setw(2) << t.hour << ":" << std::setw(2) << t.minute << ":" << std::setw(2) << t
                .second;
        return ss.str();
    }

    static bool isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }

    static int daysInMonth(int y, int m) {
        static int dm[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        int d = dm[m];
        if (m == 2 && isLeap(y)) d = 29;
        return d;
    }

    int weekdayISO(const YMD &d) {
        // 使用 std::tm + mktime 获取星期：0=Sun..6=Sat => 转为 ISO(1=Mon..7=Sun)
        std::tm tm{};
        tm.tm_year = d.year - 1900;
        tm.tm_mon = d.month - 1;
        tm.tm_mday = d.day;
        tm.tm_hour = 12; // 避免夏令时奇异
        std::mktime(&tm);
        int w = tm.tm_wday; // 0..6
        return w == 0 ? 7 : w; // 1..7
    }

    YMD addDays(const YMD &d, int delta) {
        std::tm tm{};
        tm.tm_year = d.year - 1900;
        tm.tm_mon = d.month - 1;
        tm.tm_mday = d.day;
        tm.tm_hour = 12;
        std::time_t tt = std::mktime(&tm);
        tt += static_cast<long long>(delta) * 24 * 60 * 60;
        std::tm *out = std::localtime(&tt);
        return YMD{out->tm_year + 1900, out->tm_mon + 1, out->tm_mday};
    }

    int cmpDate(const YMD &a, const YMD &b) {
        if (a.year != b.year) return a.year < b.year ? -1 : 1;
        if (a.month != b.month) return a.month < b.month ? -1 : 1;
        if (a.day != b.day) return a.day < b.day ? -1 : 1;
        return 0;
    }

    // ----------------- 工具 -----------------
    bool EduScheduleManager::overlap(const TimePoint &a1, const TimePoint &a2, const TimePoint &b1,
                                     const TimePoint &b2) {
        return (a1 < b2) && (b1 < a2);
    }

    std::string EduScheduleManager::csvEscape(const std::string &s) {
        bool need = s.find_first_of(",\n\"") != std::string::npos;
        if (!need) return s;
        std::string r = "\"";
        for (char c: s) {
            if (c == '"') r += "\"\"";
            else r.push_back(c);
        }
        r += "\"";
        return r;
    }

    std::string EduScheduleManager::csvUnescape(const std::string &s) {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
            std::string r;
            r.reserve(s.size());
            for (size_t i = 1; i + 1 < s.size(); ++i) {
                if (s[i] == '"' && i + 1 < s.size() - 1 && s[i + 1] == '"') {
                    r.push_back('"');
                    ++i;
                } else r.push_back(s[i]);
            }
            return r;
        }
        return s;
    }

    std::string EduScheduleManager::toJsonStr(bool ok, const json &data, const std::string &err) {
        json j;
        j["ok"] = ok;
        if (ok) j["data"] = data;
        else j["error"] = err;
        return j.dump();
    }

    // ----------------- 构造 & 关系维护 -----------------
    EduScheduleManager::EduScheduleManager(std::string csvRoot): root(std::move(csvRoot)) {
    }

    void EduScheduleManager::bindTeacherStudent(const std::string &teacherId, const std::string &studentId, bool add) {
        auto it = teachers.find(teacherId);
        if (it == teachers.end()) return;
        auto &vec = it->second.studentIds;
        if (add) { if (std::find(vec.begin(), vec.end(), studentId) == vec.end()) vec.push_back(studentId); } else {
            vec.erase(std::remove(vec.begin(), vec.end(), studentId), vec.end());
        }
    }

    void EduScheduleManager::bindStudentSchedule(const std::string &studentId, const std::string &scheduleId,
                                                 bool add) {
        auto it = students.find(studentId);
        if (it == students.end()) return;
        auto &vec = it->second.scheduleIds;
        if (add) { if (std::find(vec.begin(), vec.end(), scheduleId) == vec.end()) vec.push_back(scheduleId); } else {
            vec.erase(std::remove(vec.begin(), vec.end(), scheduleId), vec.end());
        }
    }

    // ----------------- 冲突检测 -----------------
    void EduScheduleManager::ensureNoConflict(const Schedule &s,
                                              const std::optional<std::string> &allowScheduleId) const {
        // 在一个相对宽的窗口（s.startDate-1年 到 s.endDate+1年）内展开所有相关 schedule，并检查同一学生/设备冲突
        auto from = makeDateTime(addDays(s.startDate, -366), HMS{0, 0, 0});
        auto to = makeDateTime(addDays(s.endDate, 366), HMS{23, 59, 59});

        // 先将欲添加/编辑的日程自身展开（供对照）
        auto mine = expandOccurrences(s, from, to);

        for (const auto &[sid, other]: schedules) {
            if (allowScheduleId && sid == *allowScheduleId) continue;
            // 只有涉及同一学生 或 同一设备 的才需要比较
            if (!(other.studentId == s.studentId || other.deviceId == s.deviceId)) continue;
            auto theirs = expandOccurrences(other, from, to);
            for (const auto &a: mine) {
                for (const auto &b: theirs) {
                    if (overlap(a.start, a.end, b.start, b.end)) {
                        throw std::runtime_error("冲突: 与现有课程重叠 (student or device)");
                    }
                }
            }
        }
    }

    // ----------------- 展开发生实例 -----------------
    std::vector<Occurrence>
    EduScheduleManager::expandOccurrences(const Schedule &s, TimePoint from, TimePoint to) const {
        std::vector<Occurrence> out;
        // 将例外取消日期制成 set 便于判断
        std::unordered_set<std::string> cancelDates;
        cancelDates.reserve(s.exceptionIds.size());
        for (const auto &eid: s.exceptionIds) {
            auto it = exceptions.find(eid);
            if (it == exceptions.end()) continue;
            cancelDates.insert(formatDate(it->second.date));
        }

        auto curDate = s.startDate;
        auto endDate = s.endDate;

        auto appendIfInRange = [&](const YMD &d) {
            // 如果当天被取消，则跳过
            if (cancelDates.count(formatDate(d))) return;
            // 生成当日的 TimePoint
            auto st = makeDateTime(d, s.startTime);
            auto ed = makeDateTime(d, s.endTime);
            if (overlap(st, ed, from, to)) {
                out.push_back(Occurrence{s.id, s.studentId, s.teacherId, s.deviceId, st, ed});
            }
        };

        // 防御：起止日期互换时调整
        if (cmpDate(curDate, endDate) > 0) std::swap(curDate, endDate);

        switch (s.recur) {
            case RecurrenceType::Once: {
                appendIfInRange(curDate);
                break;
            }
            case RecurrenceType::Daily: {
                for (YMD d = curDate; cmpDate(d, endDate) <= 0; d = addDays(d, 1)) {
                    appendIfInRange(d);
                }
                break;
            }
            case RecurrenceType::Weekly: {
                // 找到第一个满足 weekDay 的日期
                YMD d = curDate;
                int wd = weekdayISO(d);
                int delta = (s.weekDay - wd);
                if (delta < 0) delta += 7;
                d = addDays(d, delta);
                for (; cmpDate(d, endDate) <= 0; d = addDays(d, 7)) appendIfInRange(d);
                break;
            }
            case RecurrenceType::Monthly: {
                // 从起始月份开始，逐月取 monthDay（若该月天数不足，则取最后一天）
                int y = curDate.year, m = curDate.month;
                while (true) {
                    int md = std::min(s.monthDay, daysInMonth(y, m));
                    YMD d{y, m, md};
                    if (cmpDate(d, curDate) < 0) {
                        // 本月的该日早于起始日，则下月
                        if (++m > 12) {
                            m = 1;
                            ++y;
                        }
                        continue;
                    }
                    if (cmpDate(d, endDate) > 0) break;
                    appendIfInRange(d);
                    if (++m > 12) {
                        m = 1;
                        ++y;
                    }
                    if (YMD{y, m, 1}.year > endDate.year + 1) break; // 防御
                }
                break;
            }
        }

        std::sort(out.begin(), out.end(), [](const Occurrence &a, const Occurrence &b) { return a.start < b.start; });
        return out;
    }

    std::vector<Occurrence> EduScheduleManager::expandAll(TimePoint from, TimePoint to) const {
        std::vector<Occurrence> all;
        for (const auto &[id,s]: schedules) {
            auto v = expandOccurrences(s, from, to);
            all.insert(all.end(), v.begin(), v.end());
        }
        std::sort(all.begin(), all.end(), [](const Occurrence &a, const Occurrence &b) { return a.start < b.start; });
        return all;
    }

    // ----------------- CRUD（原生） -----------------
    void EduScheduleManager::createDevice(const Device &d) {
        if (devices.count(d.id)) throw std::runtime_error("设备已存在");
        devices[d.id] = d;
    }

    void EduScheduleManager::editDevice(const Device &d) {
        if (!devices.count(d.id)) throw std::runtime_error("设备不存在");
        devices[d.id] = d;
    }

    void EduScheduleManager::deleteDevice(const std::string &deviceId) {
        devices.erase(deviceId);
        // 删除引用该设备的 schedule（或留存？这里选择删除以避免悬空）
        std::vector<std::string> toDel;
        for (auto &[sid,s]: schedules) if (s.deviceId == deviceId) toDel.push_back(sid);
        for (auto &sid: toDel) deleteSchedule(sid);
    }

    void EduScheduleManager::createTeacher(const Teacher &t) {
        if (teachers.count(t.id)) throw std::runtime_error("教师已存在");
        teachers[t.id] = t;
    }

    void EduScheduleManager::editTeacher(const Teacher &t) {
        if (!teachers.count(t.id)) throw std::runtime_error("教师不存在");
        teachers[t.id] = t;
    }

    void EduScheduleManager::deleteTeacher(const std::string &teacherId) {
        // 先删除该教师名下学生的引用关系（不删学生实体，以免过度）
        teachers.erase(teacherId);
        // 关联的 schedule（teacherId）全部删除
        std::vector<std::string> toDel;
        for (auto &[sid,s]: schedules) if (s.teacherId == teacherId) toDel.push_back(sid);
        for (auto &sid: toDel) deleteSchedule(sid);
    }

    void EduScheduleManager::createStudent(const Student &s) {
        if (students.count(s.id)) throw std::runtime_error("学生已存在");
        students[s.id] = s;
    }

    void EduScheduleManager::editStudent(const Student &s) {
        if (!students.count(s.id)) throw std::runtime_error("学生不存在");
        students[s.id] = s;
    }

    void EduScheduleManager::deleteStudent(const std::string &studentId) {
        // 从所有教师的 studentIds 中解绑
        for (auto &[tid,t]: teachers) bindTeacherStudent(tid, studentId, false);
        // 删除学生的 schedule
        std::vector<std::string> toDel;
        for (auto &[sid,s]: schedules) if (s.studentId == studentId) toDel.push_back(sid);
        for (auto &sid: toDel) deleteSchedule(sid);
        students.erase(studentId);
    }

    void EduScheduleManager::createSchedule(const Schedule &s) {
        if (schedules.count(s.id)) throw std::runtime_error("课程已存在");
        // 基本校验
        if (!students.count(s.studentId)) throw std::runtime_error("学生不存在");
        if (!teachers.count(s.teacherId)) throw std::runtime_error("教师不存在");
        if (!devices.count(s.deviceId)) throw std::runtime_error("设备不存在");
        if (!(s.endTime.hour * 3600 + s.endTime.minute * 60 + s.endTime.second > s.startTime.hour * 3600 + s.startTime.
              minute * 60 + s.startTime.second))
            throw std::runtime_error("结束时间必须晚于开始时间");

        ensureNoConflict(s);
        schedules[s.id] = s;
        bindStudentSchedule(s.studentId, s.id, true);
    }

    void EduScheduleManager::editSchedule(const Schedule &s) {
        auto it = schedules.find(s.id);
        if (it == schedules.end()) throw std::runtime_error("课程不存在");
        // 先做冲突检测（允许与自己旧定义重叠）
        ensureNoConflict(s, s.id);

        // 若学生变更，更新绑定
        if (it->second.studentId != s.studentId) {
            bindStudentSchedule(it->second.studentId, s.id, false);
            bindStudentSchedule(s.studentId, s.id, true);
        }
        schedules[s.id] = s;
    }

    void EduScheduleManager::deleteSchedule(const std::string &scheduleId) {
        auto it = schedules.find(scheduleId);
        if (it == schedules.end()) return;
        // 删除其例外
        std::vector<std::string> toDel = it->second.exceptionIds;
        for (const auto &eid: toDel) exceptions.erase(eid);

        bindStudentSchedule(it->second.studentId, scheduleId, false);
        schedules.erase(it);
    }

    void EduScheduleManager::createException(const ScheduleException &e) {
        if (!schedules.count(e.scheduleId)) throw std::runtime_error("课程不存在");
        if (exceptions.count(e.id)) throw std::runtime_error("例外已存在");
        exceptions[e.id] = e;
        schedules[e.scheduleId].exceptionIds.push_back(e.id);
    }

    void EduScheduleManager::deleteException(const std::string &exceptionId) {
        auto it = exceptions.find(exceptionId);
        if (it == exceptions.end()) return;
        auto sid = it->second.scheduleId;
        exceptions.erase(it);
        auto &vec = schedules[sid].exceptionIds;
        vec.erase(std::remove(vec.begin(), vec.end(), exceptionId), vec.end());
    }

    // ----------------- 查询（原生） -----------------
    std::vector<Occurrence> EduScheduleManager::occurrencesStartingWithin1h(const TimePoint &now) const {
        auto from = now - std::chrono::hours(0); // 起点 = now（以“开始时间为准”）
        auto to = now + std::chrono::hours(1);
        auto all = expandAll(from, to);
        std::vector<Occurrence> v;
        for (const auto &oc: all) { if (oc.start >= from && oc.start <= to) v.push_back(oc); }
        return v;
    }

    EduScheduleManager::Status EduScheduleManager::statusAt(const TimePoint &now) const {
        auto from = now - std::chrono::hours(12);
        auto to = now + std::chrono::hours(12);
        auto all = expandAll(from, to);
        for (const auto &oc: all) { if (oc.start <= now && now < oc.end) { return Status{true, oc}; } }
        return Status{false, std::nullopt};
    }

    EduScheduleManager::Nearest EduScheduleManager::nearestAround(const TimePoint &now) const {
        auto from = now - std::chrono::hours(24 * 180);
        auto to = now + std::chrono::hours(24 * 365);
        auto all = expandAll(from, to);
        std::optional<Occurrence> prev, next1, next2;
        for (const auto &oc: all) {
            if (oc.end <= now) { prev = oc; } else if (oc.start >= now) {
                if (!next1) next1 = oc;
                else if (!next2) next2 = oc;
            }
            if (oc.start > now && next2) break;
        }
        return Nearest{prev, next1, next2};
    }

    // ----------------- JSON 版 API -----------------
    static json JOk() {
        json j;
        j["ok"] = true;
        return j;
    }

    std::string EduScheduleManager::jsonCreateDevice(const std::string &s) {
        try {
            auto j = json::parse(s);
            Device d{j.at("id"), j.value("name", "")};
            createDevice(d);
            return toJsonStr(true, JOk());
        } catch (const std::exception &e) { return toJsonStr(false, {}, e.what()); }
    }

    std::string EduScheduleManager::jsonEditDevice(const std::string &s) {
        try {
            auto j = json::parse(s);
            Device d{j.at("id"), j.value("name", "")};
            editDevice(d);
            return toJsonStr(true, JOk());
        } catch (const std::exception &e) { return toJsonStr(false, {}, e.what()); }
    }

    std::string EduScheduleManager::jsonDeleteDevice(const std::string &s) {
        try {
            auto j = json::parse(s);
            deleteDevice(j.at("id"));
            return toJsonStr(true, JOk());
        } catch (const std::exception &e) { return toJsonStr(false, {}, e.what()); }
    }

    std::string EduScheduleManager::jsonCreateTeacher(const std::string &s) {
        try {
            auto j = json::parse(s);
            Teacher t{j.at("id"), j.value("name", ""), j.value<std::vector<std::string> >("studentIds", {})};
            createTeacher(t);
            return toJsonStr(true, JOk());
        } catch (const std::exception &e) { return toJsonStr(false, {}, e.what()); }
    }

    std::string EduScheduleManager::jsonEditTeacher(const std::string &s) {
        try {
            auto j = json::parse(s);
            Teacher t{j.at("id"), j.value("name", ""), j.value<std::vector<std::string> >("studentIds", {})};
            editTeacher(t);
            return toJsonStr(true, JOk());
        } catch (const std::exception &e) { return toJsonStr(false, {}, e.what()); }
    }

    std::string EduScheduleManager::jsonDeleteTeacher(const std::string &s) {
        try {
            auto j = json::parse(s);
            deleteTeacher(j.at("id"));
            return toJsonStr(true, JOk());
        } catch (const std::exception &e) { return toJsonStr(false, {}, e.what()); }
    }

    std::string EduScheduleManager::jsonCreateStudent(const std::string &s) {
        try {
            auto j = json::parse(s);
            Student st{j.at("id"), j.value("name", ""), j.value<std::vector<std::string> >("scheduleIds", {})};
            createStudent(st);
            if (j.contains("teacherId")) bindTeacherStudent(j.at("teacherId"), st.id, true);
            return toJsonStr(true, JOk());
        } catch (const std::exception &e) { return toJsonStr(false, {}, e.what()); }
    }

    std::string EduScheduleManager::jsonEditStudent(const std::string &s) {
        try {
            auto j = json::parse(s);
            Student st{j.at("id"), j.value("name", ""), j.value<std::vector<std::string> >("scheduleIds", {})};
            editStudent(st);
            return toJsonStr(true, JOk());
        } catch (const std::exception &e) { return toJsonStr(false, {}, e.what()); }
    }

    std::string EduScheduleManager::jsonDeleteStudent(const std::string &s) {
        try {
            auto j = json::parse(s);
            deleteStudent(j.at("id"));
            return toJsonStr(true, JOk());
        } catch (const std::exception &e) { return toJsonStr(false, {}, e.what()); }
    }

    static RecurrenceType parseRecur(const std::string &r) {
        if (r == "once") return RecurrenceType::Once;
        if (r == "daily") return RecurrenceType::Daily;
        if (r == "weekly") return RecurrenceType::Weekly;
        if (r == "monthly") return RecurrenceType::Monthly;
        throw std::runtime_error("未知重复类型");
    }

    std::string EduScheduleManager::jsonCreateSchedule(const std::string &s) {
        try {
            auto j = json::parse(s);
            Schedule sc{};
            sc.id = j.at("id");
            sc.studentId = j.at("studentId");
            sc.teacherId = j.at("teacherId");
            sc.deviceId = j.at("deviceId");
            sc.startDate = parseDate(j.at("startDate"));
            sc.endDate = parseDate(j.at("endDate"));
            sc.recur = parseRecur(j.value("recurrence", "once"));
            sc.weekDay = j.value("weekDay", 1);
            sc.monthDay = j.value("monthDay", 1);
            sc.startTime = parseTime(j.at("startTime"));
            sc.endTime = parseTime(j.at("endTime"));
            createSchedule(sc);
            return toJsonStr(true, JOk());
        } catch (const std::exception &e) { return toJsonStr(false, {}, e.what()); }
    }

    std::string EduScheduleManager::jsonEditSchedule(const std::string &s) {
        try {
            auto j = json::parse(s);
            Schedule sc{};
            sc.id = j.at("id");
            sc.studentId = j.at("studentId");
            sc.teacherId = j.at("teacherId");
            sc.deviceId = j.at("deviceId");
            sc.startDate = parseDate(j.at("startDate"));
            sc.endDate = parseDate(j.at("endDate"));
            sc.recur = parseRecur(j.value("recurrence", "once"));
            sc.weekDay = j.value("weekDay", 1);
            sc.monthDay = j.value("monthDay", 1);
            sc.startTime = parseTime(j.at("startTime"));
            sc.endTime = parseTime(j.at("endTime"));
            // 例外若传入（覆盖式）
            if (j.contains("exceptions")) {
                // 先清空旧的
                auto it = schedules.find(sc.id);
                if (it != schedules.end()) {
                    for (const auto &eid: it->second.exceptionIds) exceptions.erase(eid);
                    it->second.exceptionIds.clear();
                }
                // 逐个创建
                for (const auto &ex: j.at("exceptions")) {
                    ScheduleException e{};
                    e.scheduleId = sc.id;
                    e.date = parseDate(ex.get<std::string>());
                    e.id = sc.id + "#" + formatDate(e.date);
                    exceptions[e.id] = e;
                    sc.exceptionIds.push_back(e.id);
                }
            } else {
                // 保留既有的
                auto it = schedules.find(sc.id);
                if (it != schedules.end()) sc.exceptionIds = it->second.exceptionIds;
            }
            editSchedule(sc);
            return toJsonStr(true, JOk());
        } catch (const std::exception &e) { return toJsonStr(false, {}, e.what()); }
    }

    std::string EduScheduleManager::jsonDeleteSchedule(const std::string &s) {
        try {
            auto j = json::parse(s);
            deleteSchedule(j.at("id"));
            return toJsonStr(true, JOk());
        } catch (const std::exception &e) { return toJsonStr(false, {}, e.what()); }
    }

    std::string EduScheduleManager::jsonCreateException(const std::string &s) {
        try {
            auto j = json::parse(s);
            ScheduleException e{};
            e.scheduleId = j.at("scheduleId");
            e.date = parseDate(j.at("date"));
            e.id = e.scheduleId + "#" + formatDate(e.date);
            createException(e);
            return toJsonStr(true, JOk());
        } catch (const std::exception &e) { return toJsonStr(false, {}, e.what()); }
    }

    std::string EduScheduleManager::jsonDeleteException(const std::string &s) {
        try {
            auto j = json::parse(s);
            deleteException(j.at("id"));
            return toJsonStr(true, JOk());
        } catch (const std::exception &e) { return toJsonStr(false, {}, e.what()); }
    }

    static json occToJson(const Occurrence &oc) {
        json j;
        j["scheduleId"] = oc.scheduleId;
        j["studentId"] = oc.studentId;
        j["teacherId"] = oc.teacherId;
        j["deviceId"] = oc.deviceId;
        j["start"] = formatDateTime(oc.start);
        j["end"] = formatDateTime(oc.end);
        return j;
    }

    std::string EduScheduleManager::jsonOccurrencesStartingWithin1h(const std::string &nowStr) {
        try {
            auto j = json::parse(nowStr);
            auto tp = parseDateTime(j.at("now"));
            json out = json::array();
            for (const auto &oc: occurrencesStartingWithin1h(tp)) out.push_back(occToJson(oc));
            return toJsonStr(true, out);
        } catch (const std::exception &e) { return toJsonStr(false, {}, e.what()); }
    }

    std::string EduScheduleManager::jsonStatusAt(const std::string &nowStr) {
        try {
            auto j = json::parse(nowStr);
            auto tp = parseDateTime(j.at("now"));
            auto st = statusAt(tp);
            json out;
            out["busy"] = st.busy;
            if (st.current) out["current"] = occToJson(*st.current);
            return toJsonStr(true, out);
        } catch (const std::exception &e) { return toJsonStr(false, {}, e.what()); }
    }

    std::string EduScheduleManager::jsonNearestAround(const std::string &nowStr) {
        try {
            auto j = json::parse(nowStr);
            auto tp = parseDateTime(j.at("now"));
            auto n = nearestAround(tp);
            json out;
            if (n.prev) out["prev"] = occToJson(*n.prev);
            if (n.next1) out["next1"] = occToJson(*n.next1);
            if (n.next2) out["next2"] = occToJson(*n.next2);
            return toJsonStr(true, out);
        } catch (const std::exception &e) { return toJsonStr(false, {}, e.what()); }
    }

    // ----------------- CSV 持久化 -----------------
    void EduScheduleManager::saveAll() const {
        namespace fs = std::filesystem;
        fs::create_directories(root);

        // devices.csv: id,name
        {
            std::ofstream f(root + "/devices.csv", std::ios::trunc);
            f << "id,name\n";
            for (const auto &[id,d]: devices) { f << csvEscape(d.id) << "," << csvEscape(d.name) << "\n"; }
        }
        // teachers.csv: id,name,studentIds(semi-colon)
        {
            std::ofstream f(root + "/teachers.csv", std::ios::trunc);
            f << "id,name,studentIds\n";
            for (const auto &[id,t]: teachers) {
                std::ostringstream ss;
                for (size_t i = 0; i < t.studentIds.size(); ++i) {
                    if (i) ss << ";";
                    ss << t.studentIds[i];
                }
                f << csvEscape(t.id) << "," << csvEscape(t.name) << "," << csvEscape(ss.str()) << "\n";
            }
        }
        // students.csv: id,name,scheduleIds(semi-colon)
        {
            std::ofstream f(root + "/students.csv", std::ios::trunc);
            f << "id,name,scheduleIds\n";
            for (const auto &[id,s]: students) {
                std::ostringstream ss;
                for (size_t i = 0; i < s.scheduleIds.size(); ++i) {
                    if (i) ss << ";";
                    ss << s.scheduleIds[i];
                }
                f << csvEscape(s.id) << "," << csvEscape(s.name) << "," << csvEscape(ss.str()) << "\n";
            }
        }
        // schedules.csv: id,studentId,teacherId,deviceId,startDate,endDate,recur,weekDay,monthDay,startTime,endTime,exceptionIds(semi-colon)
        {
            std::ofstream f(root + "/schedules.csv", std::ios::trunc);
            f <<
                    "id,studentId,teacherId,deviceId,startDate,endDate,recur,weekDay,monthDay,startTime,endTime,exceptionIds\n";
            for (const auto &[id,s]: schedules) {
                std::string recur = (s.recur == RecurrenceType::Once
                                         ? "once"
                                         : s.recur == RecurrenceType::Daily
                                               ? "daily"
                                               : s.recur == RecurrenceType::Weekly
                                                     ? "weekly"
                                                     : "monthly");
                std::ostringstream ex;
                for (size_t i = 0; i < s.exceptionIds.size(); ++i) {
                    if (i) ex << ";";
                    ex << s.exceptionIds[i];
                }
                f << csvEscape(s.id) << "," << csvEscape(s.studentId) << "," << csvEscape(s.teacherId) << "," <<
                        csvEscape(s.deviceId)
                        << "," << csvEscape(formatDate(s.startDate)) << "," << csvEscape(formatDate(s.endDate))
                        << "," << csvEscape(recur) << "," << s.weekDay << "," << s.monthDay
                        << "," << csvEscape(formatTime(s.startTime)) << "," << csvEscape(formatTime(s.endTime))
                        << "," << csvEscape(ex.str()) << "\n";
            }
        }
        // exceptions.csv: id,scheduleId,date
        {
            std::ofstream f(root + "/exceptions.csv", std::ios::trunc);
            f << "id,scheduleId,date\n";
            for (const auto &[id,e]: exceptions) {
                f << csvEscape(e.id) << "," << csvEscape(e.scheduleId) << "," << csvEscape(formatDate(e.date)) << "\n";
            }
        }
    }

    static std::vector<std::string> splitCSVLine(const std::string &line) {
        std::vector<std::string> cols;
        std::string cur;
        bool inq = false;
        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (inq) {
                if (c == '"') {
                    if (i + 1 < line.size() && line[i + 1] == '"') {
                        cur.push_back('"');
                        ++i;
                    } else inq = false;
                } else cur.push_back(c);
            } else {
                if (c == '"') inq = true;
                else if (c == ',') {
                    cols.push_back(cur);
                    cur.clear();
                } else cur.push_back(c);
            }
        }
        cols.push_back(cur);
        for (auto &s: cols) s = EduScheduleManager::csvUnescape(s);
        return cols;
    }

    void EduScheduleManager::loadAll() {
        devices.clear();
        teachers.clear();
        students.clear();
        schedules.clear();
        exceptions.clear();

        auto loadFile = [&](const std::string &path, auto handler) {
            std::ifstream f(path);
            if (!f.good()) return;
            std::string line;
            std::getline(f, line);
            while (std::getline(f, line)) {
                if (line.empty()) continue;
                handler(splitCSVLine(line));
            }
        };

        // devices
        loadFile(root + "/devices.csv", [&](const std::vector<std::string> &c) {
            if (c.size() < 2) return;
            devices[c[0]] = Device{c[0], c[1]};
        });

        // teachers
        loadFile(root + "/teachers.csv", [&](const std::vector<std::string> &c) {
            if (c.size() < 3) return;
            Teacher t{c[0], c[1], {}};
            std::stringstream ss(c[2]);
            std::string x;
            while (std::getline(ss, x, ';')) if (!x.empty()) t.studentIds.push_back(x);
            teachers[t.id] = t;
        });

        // students
        loadFile(root + "/students.csv", [&](const std::vector<std::string> &c) {
            if (c.size() < 3) return;
            Student s{c[0], c[1], {}};
            std::stringstream ss(c[2]);
            std::string x;
            while (std::getline(ss, x, ';')) if (!x.empty()) s.scheduleIds.push_back(x);
            students[s.id] = s;
        });

        // schedules
        loadFile(root + "/schedules.csv", [&](const std::vector<std::string> &c) {
            if (c.size() < 12) return;
            Schedule s{};
            s.id = c[0];
            s.studentId = c[1];
            s.teacherId = c[2];
            s.deviceId = c[3];
            s.startDate = parseDate(c[4]);
            s.endDate = parseDate(c[5]);
            std::string r = c[6];
            s.recur = (r == "once"
                           ? RecurrenceType::Once
                           : r == "daily"
                                 ? RecurrenceType::Daily
                                 : r == "weekly"
                                       ? RecurrenceType::Weekly
                                       : RecurrenceType::Monthly);
            s.weekDay = std::stoi(c[7]);
            s.monthDay = std::stoi(c[8]);
            s.startTime = parseTime(c[9]);
            s.endTime = parseTime(c[10]);
            std::stringstream ss(c[11]);
            std::string x;
            while (std::getline(ss, x, ';')) if (!x.empty()) s.exceptionIds.push_back(x);
            schedules[s.id] = s;
        });

        // exceptions
        loadFile(root + "/exceptions.csv", [&](const std::vector<std::string> &c) {
            if (c.size() < 3) return;
            ScheduleException e{c[0], c[1], parseDate(c[2])};
            exceptions[e.id] = e;
            if (schedules.count(e.scheduleId)) schedules[e.scheduleId].exceptionIds.push_back(e.id);
        });
    }

    void EduScheduleManager::printAllHierarchy(std::ostream &os) const {
        os << "===== 教师/学生/课程 层级结构 =====\n";

        for (const auto &[tid, teacher]: teachers) {
            os << "教师: " << teacher.name << " (ID=" << teacher.id << ")\n";

            for (const auto &sid: teacher.studentIds) {
                auto itStu = students.find(sid);
                if (itStu == students.end()) {
                    os << "  └─ [无效学生ID=" << sid << "]\n";
                    continue;
                }
                const auto &stu = itStu->second;
                os << "  学生: " << stu.name << " (ID=" << stu.id << ")\n";

                for (const auto &scid: stu.scheduleIds) {
                    auto itSc = schedules.find(scid);
                    if (itSc == schedules.end()) {
                        os << "    └─ [无效日程ID=" << scid << "]\n";
                        continue;
                    }
                    const auto &sc = itSc->second;

                    auto itDev = devices.find(sc.deviceId);
                    std::string deviceName = (itDev != devices.end() ? itDev->second.name : "[未知设备]");

                    os << "    课程: (ID=" << sc.id << ")\n"
                            << "      日期范围: " << formatDate(sc.startDate) << " ~ " << formatDate(sc.endDate) << "\n"
                            << "      时间: " << formatTime(sc.startTime) << " - " << formatTime(sc.endTime) << "\n"
                            << "      设备: " << deviceName << " (ID=" << sc.deviceId << ")\n"
                            << "      重复: ";
                    switch (sc.recur) {
                        case RecurrenceType::Once: os << "一次性";
                            break;
                        case RecurrenceType::Daily: os << "每天";
                            break;
                        case RecurrenceType::Weekly: os << "每周(周" << sc.weekDay << ")";
                            break;
                        case RecurrenceType::Monthly: os << "每月(" << sc.monthDay << "号)";
                            break;
                    }
                    os << "\n";

                    if (!sc.exceptionIds.empty()) {
                        os << "      例外(取消):\n";
                        for (auto &eid: sc.exceptionIds) {
                            auto itEx = exceptions.find(eid);
                            if (itEx != exceptions.end()) {
                                os << "        - " << formatDate(itEx->second.date)
                                        << " (ID=" << itEx->second.id << ")\n";
                            } else {
                                os << "        - [无效例外ID=" << eid << "]\n";
                            }
                        }
                    }
                }
            }
        }
    }

    nlohmann::json EduScheduleManager::toHierarchyJson() const {
        nlohmann::json jTeachers = nlohmann::json::array();

        for (const auto& [tid, teacher] : teachers) {
            nlohmann::json jTeacher;
            jTeacher["id"] = teacher.id;
            jTeacher["name"] = teacher.name;

            nlohmann::json jStudents = nlohmann::json::array();
            for (const auto& sid : teacher.studentIds) {
                auto itStu = students.find(sid);
                if (itStu == students.end()) continue;
                const auto& stu = itStu->second;

                nlohmann::json jStudent;
                jStudent["id"] = stu.id;
                jStudent["name"] = stu.name;

                nlohmann::json jSchedules = nlohmann::json::array();
                for (const auto& scid : stu.scheduleIds) {
                    auto itSc = schedules.find(scid);
                    if (itSc == schedules.end()) continue;
                    const auto& sc = itSc->second;

                    nlohmann::json jSchedule;
                    jSchedule["id"] = sc.id;
                    jSchedule["deviceId"] = sc.deviceId;
                    jSchedule["startDate"] = formatDate(sc.startDate);
                    jSchedule["endDate"]   = formatDate(sc.endDate);
                    jSchedule["startTime"] = formatTime(sc.startTime);
                    jSchedule["endTime"]   = formatTime(sc.endTime);
                    jSchedule["recur"]     = static_cast<int>(sc.recur);

                    jSchedules.push_back(jSchedule);
                }
                jStudent["schedules"] = jSchedules;
                jStudents.push_back(jStudent);
            }
            jTeacher["students"] = jStudents;
            jTeachers.push_back(jTeacher);
        }

        return jTeachers;
    }
} // namespace edu
