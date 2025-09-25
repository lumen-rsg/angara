//
// Created by cv2 on 24.09.2025.
//

#ifndef ANGARA_LS_LOGGER_H
#define ANGARA_LS_LOGGER_H

#include <string>
#include <sstream>
#include <mutex>
#include <iostream>

class Logger {
public:
    // Singleton access
    static Logger& instance();

    // Log a message
    void log(const std::string& message);

    // Dumps the entire log content to a stream (like std::cerr)
    void dump(std::ostream& os);

private:
    Logger() = default; // Private constructor
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::stringstream m_log_stream;
    std::mutex m_mutex;
};

// A convenience macro for easy logging from anywhere in the code.
#define LOG(msg) Logger::instance().log(msg)

#endif // ANGARA_LS_LOGGER_H