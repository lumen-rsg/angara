//
// Created by cv2 on 24.09.2025.
//

#include "DocumentManager.h"

namespace angara {

    // A simple implementation to convert a file URI to a path.
    // A production-ready version would need to handle Windows paths and URL decoding.
    std::string uri_to_path(const std::string& uri) {
        if (uri.rfind("file://", 0) == 0) {
            return uri.substr(7);
        }
        return uri; // Fallback for non-file URIs or already-formatted paths
    }

    void DocumentManager::on_open(const std::string& uri, const std::string& content) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_documents[uri_to_path(uri)] = content;
    }

    void DocumentManager::on_change(const std::string& uri, const std::string& new_content) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_documents[uri_to_path(uri)] = new_content;
    }

    void DocumentManager::on_close(const std::string& uri) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_documents.erase(uri_to_path(uri));
    }

    bool DocumentManager::is_open(const std::string& path) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_documents.count(path) > 0;
    }

    std::optional<std::string> DocumentManager::get_content(const std::string& path) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_documents.find(path);
        if (it != m_documents.end()) {
            return it->second;
        }
        return std::nullopt;
    }

} // namespace angara