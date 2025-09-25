//
// Created by cv2 on 24.09.2025.
//

#include "Logger.h"

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

void Logger::log(const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_log_stream << message << std::endl;
}

void Logger::dump(std::ostream& os) {
    std::lock_guard<std::mutex> lock(m_mutex);
    os << "--- ANGARALS LOG DUMP ---\n"
       << m_log_stream.str()
       << "--- END LOG DUMP ---\n";
}