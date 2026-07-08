#include "utils/logger.h"
#include <ctime>
#include <iostream>
#include <cstdio>

namespace cbackup {

bool Logger::verbose_ = false;

void Logger::set_verbose(bool v) { verbose_ = v; }

void Logger::log(LogLevel level, const std::string& msg) {
    time_t t = time(nullptr);
    char tbuf[20];
    strftime(tbuf, sizeof(tbuf), "%H:%M:%S", localtime(&t));
    const char* prefix = nullptr;
    FILE* out = stdout;
    switch (level) {
        case LogLevel::INFO:  prefix = "INFO";  break;
        case LogLevel::WARN:  prefix = "WARN";  out = stderr; break;
        case LogLevel::ERROR: prefix = "ERROR"; out = stderr; break;
    }
    fprintf(out, "[%s][%s] %s\n", tbuf, prefix, msg.c_str());
}

void Logger::info(const std::string& msg) {
    if (verbose_) log(LogLevel::INFO, msg);
}

void Logger::warn(const std::string& msg) { log(LogLevel::WARN, msg); }

void Logger::error(const std::string& msg) { log(LogLevel::ERROR, msg); }

void Logger::progress(uint64_t files, uint64_t bytes) {
    double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    fprintf(stdout, "\r  Progress: %lu files, %.2f MB", files, mb);
    fflush(stdout);
}

}  // namespace cbackup
