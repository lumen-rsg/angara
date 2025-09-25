#include <iostream>
#include <string>
#include <vector>
#include <mutex>
#include <optional>
#include <csignal>
#include <sstream>

#include <json.hpp>

#include "LanguageServerState.h"
#include "LspDiagnostic.h"
#include "Logger.h"

using json = nlohmann::json;

std::mutex g_stdout_mutex;

void write_json(const json& j) {
    const std::string content = j.dump();
    std::lock_guard<std::mutex> lock(g_stdout_mutex);
    std::cout << "Content-Length: " << content.length() << "\r\n\r\n" << content << std::flush;
}

json diagnostic_to_json(const angara::Diagnostic& d) {
    return json{
        {"range", {
            {"start", {{"line", d.range.start_line}, {"character", d.range.start_char}}},
            {"end", {{"line", d.range.end_line}, {"character", d.range.end_char}}}
        }},
        {"severity", static_cast<int>(d.severity)},
        {"source", "angc"},
        {"message", d.message}
    };
}

void handle_exit(int signal) {
    LOG("Server exiting with signal " + std::to_string(signal) + ". Dumping log.");
    Logger::instance().dump(std::cerr);
    exit(signal);
}

int main() {
    std::signal(SIGINT, handle_exit);
    std::signal(SIGTERM, handle_exit);
    std::signal(SIGABRT, handle_exit);

    LOG("Angara Language Server started.");
    angara::LanguageServerState state;

    while (true) {
        long content_length = -1;
        std::string header_buffer;

        LOG("Waiting for message headers...");

        while (true) {
            char c = std::cin.get();
            if (std::cin.eof()) {
                LOG("stdin reached EOF while waiting for headers. Exiting normally.");
                handle_exit(0);
            }
            header_buffer += c;

            if (header_buffer.length() >= 4 && header_buffer.substr(header_buffer.length() - 4) == "\r\n\r\n") {
                break;
            }
        }
        LOG("Full header block received:\n" + header_buffer);

        std::istringstream header_stream(header_buffer);
        std::string line;
        while(std::getline(header_stream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("Content-Length: ", 0) == 0) {
                 try {
                    content_length = std::stol(line.substr(16));
                    LOG("Found Content-Length: " + std::to_string(content_length));
                } catch (const std::exception& e) {
                    LOG("ERROR: Could not parse Content-Length. Details: " + std::string(e.what()));
                }
            }
        }

        if (content_length == -1) {
            LOG("No Content-Length found in headers. Resetting loop.");
            continue;
        }

        std::string content;
        content.resize(content_length);
        int chars_read = 0;
        LOG("Reading content body of " + std::to_string(content_length) + " bytes manually...");

        while (chars_read < content_length) {
            char c = std::cin.get();
            if (std::cin.eof()) {
                LOG("stdin reached EOF prematurely while reading content body. Exiting.");
                handle_exit(1);
            }
            content[chars_read] = c;
            chars_read++;
        }
        LOG("Content received: " + content);

        json request;
        try {
            request = json::parse(content);
            LOG("Successfully parsed JSON request.");
        } catch (const json::parse_error& e) {
            LOG("ERROR: JSON parse failed. Details: " + std::string(e.what()));
            continue;
        }

        if (!request.contains("method") || !request["method"].is_string()) {
            LOG("Received JSON is not a valid request/notification. Ignoring.");
            continue;
        }

        const std::string method = request["method"];
        LOG("Dispatching method: " + method);

        const json params = request.contains("params") ? request["params"] : json::object();
        const auto id = request.contains("id") ? std::optional(request["id"]) : std::nullopt;

        if (method == "initialize") {
            LOG("Handling 'initialize' request.");
            json result = {
                {"capabilities", {
                    {"textDocumentSync", 1},
                    {"hoverProvider", true},
                }}
            };
            json response = {{"id", id}, {"jsonrpc", "2.0"}, {"result", result}};
            LOG("Sending response: " + response.dump(-1));
            write_json(response);

        } else if (method == "initialized") {
            LOG("Handling 'initialized' notification. Client is ready.");

        } else if (method == "exit") {
            LOG("Handling 'exit' notification. Shutting down.");
            handle_exit(0);

        } else if (method == "textDocument/didOpen") {
            const std::string uri = params["textDocument"]["uri"];
            LOG("Handling 'didOpen' for URI: " + uri);
            state.on_document_open(uri, params["textDocument"]["text"]);

            auto diagnostics = state.get_diagnostics(uri);
            LOG("Found " + std::to_string(diagnostics.size()) + " diagnostics for " + uri);
            json diagnostics_json = json::array();
            for (const auto& d : diagnostics) diagnostics_json.push_back(diagnostic_to_json(d));

            json notification = {
                {"jsonrpc", "2.0"},
                {"method", "textDocument/publishDiagnostics"},
                {"params", {{"uri", uri}, {"diagnostics", diagnostics_json}}}
            };
            LOG("Sending notification: " + notification.dump(-1));
            write_json(notification);

        } else if (method == "textDocument/didChange") {
            const std::string uri = params["textDocument"]["uri"];
            LOG("Handling 'didChange' for URI: " + uri);
            state.on_document_change(uri, params["contentChanges"][0]["text"]);

            auto diagnostics = state.get_diagnostics(uri);
            LOG("Found " + std::to_string(diagnostics.size()) + " diagnostics for " + uri);
            json diagnostics_json = json::array();
            for (const auto& d : diagnostics) diagnostics_json.push_back(diagnostic_to_json(d));

            json notification = {
                {"jsonrpc", "2.0"},
                {"method", "textDocument/publishDiagnostics"},
                {"params", {{"uri", uri}, {"diagnostics", diagnostics_json}}}
            };
            LOG("Sending notification: " + notification.dump(-1));
            write_json(notification);

        } else if (method == "textDocument/didClose") {
            const std::string uri = params["textDocument"]["uri"];
            LOG("Handling 'didClose' for URI: " + uri);
            state.on_document_close(uri);

            json notification = {
                {"jsonrpc", "2.0"},
                {"method", "textDocument/publishDiagnostics"},
                {"params", {{"uri", uri}, {"diagnostics", json::array()}}}
            };
            LOG("Sending notification (clearing diagnostics): " + notification.dump(-1));
            write_json(notification);

        } else if (method == "textDocument/hover") {
            const std::string uri = params["textDocument"]["uri"];
            LOG("Handling 'hover' request for URI: " + uri);
            json result = {{"contents", {{"kind", "markdown"}, {"value", "**Angara**: Hover feature works!"}}}};
            json response = {{"id", id}, {"jsonrpc", "2.0"}, {"result", result}};
            LOG("Sending response: " + response.dump(-1));
            write_json(response);

        } else if (id) {
            LOG("ERROR: Received unhandled request with method: " + method);
            json error = {{"code", -32601}, {"message", "Method not found: " + method}};
            json response = {{"id", id}, {"jsonrpc", "2.0"}, {"error", error}};
            LOG("Sending MethodNotFound error response: " + response.dump(-1));
            write_json(response);
        } else {
            LOG("Received and ignored unhandled notification: " + method);
        }
    }

    return 0;
}