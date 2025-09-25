//
// Created by cv2 on 24.09.2025.
//

#ifndef ANGARA_LS_DOCUMENTMANAGER_H
#define ANGARA_LS_DOCUMENTMANAGER_H

#include <string>
#include <map>
#include <optional>
#include <mutex>

namespace angara {

    // Converts an LSP URI (e.g., "file:///path/to/file.an") to a
    // standard file system path that the compiler driver can use.
    std::string uri_to_path(const std::string& uri);

    // Manages the in-memory state of all documents open in the editor.
    // This class is designed to be thread-safe.
    class DocumentManager {
    public:
        // Called when the editor sends a `textDocument/didOpen` notification.
        void on_open(const std::string& uri, const std::string& content);

        // Called when the editor sends a `textDocument/didChange` notification.
        void on_change(const std::string& uri, const std::string& new_content);

        // Called when the editor sends a `textDocument/didClose` notification.
        void on_close(const std::string& uri);

        // Checks if a given file path corresponds to a document that is currently open.
        bool is_open(const std::string& path) const;

        // Retrieves the latest content of an open document.
        // Returns std::nullopt if the document is not currently open.
        std::optional<std::string> get_content(const std::string& path) const;

    private:
        // A map from file system path to the document's content.
        // We use path as the key for easier lookup from the CompilerDriver.
        std::map<std::string, std::string> m_documents;

        // A mutex to protect the map from concurrent access from different
        // LSP request threads.
        mutable std::mutex m_mutex;
    };

} // namespace angara

#endif // ANGARA_LS_DOCUMENTMANAGER_H
