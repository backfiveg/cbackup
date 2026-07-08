#pragma once
#include <string>
#include <cstdio>

namespace cbackup {

enum class LogLevel { INFO, WARN, ERROR };

class Logger {
 public:
    static void set_verbose(bool v);
    static void info(const std::string& msg);
    static void warn(const std::string& msg);
    static void error(const std::string& msg);
    static void progress(uint64_t files, uint64_t bytes);

 private:
    static bool verbose_;
    static void log(LogLevel level, const std::string& msg);
};

}  // namespace cbackup
