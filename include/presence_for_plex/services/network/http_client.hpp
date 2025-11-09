#pragma once

#include "presence_for_plex/services/network/http_types.hpp"
#include <memory>
#include <future>
#include <expected>
#include <atomic>

namespace presence_for_plex {
namespace services {

// HTTP client interface
class HttpClient {
public:
    virtual ~HttpClient() = default;

    // Synchronous requests
    virtual std::expected<HttpResponse, NetworkError> execute(const HttpRequest& request) = 0;

    // Asynchronous requests
    virtual std::future<std::expected<HttpResponse, NetworkError>> execute_async(
        const HttpRequest& request) = 0;

    // Convenience methods
    virtual std::expected<HttpResponse, NetworkError> get(
        const std::string& url,
        const HttpHeaders& headers = {}) = 0;

    virtual std::expected<HttpResponse, NetworkError> post(
        const std::string& url,
        const std::string& body,
        const HttpHeaders& headers = {}) = 0;

    virtual std::expected<HttpResponse, NetworkError> post_json(
        const std::string& url,
        const std::string& json_body,
        const HttpHeaders& headers = {}) = 0;

    virtual std::expected<HttpResponse, NetworkError> put(
        const std::string& url,
        const std::string& body,
        const HttpHeaders& headers = {}) = 0;

    virtual std::expected<HttpResponse, NetworkError> delete_resource(
        const std::string& url,
        const HttpHeaders& headers = {}) = 0;

    // File operations
    virtual std::expected<void, NetworkError> download_file(
        const std::string& url,
        const std::string& file_path,
        ProgressCallback progress = nullptr) = 0;

    virtual std::expected<HttpResponse, NetworkError> upload_file(
        const std::string& url,
        const std::string& file_path,
        const std::string& field_name = "file",
        const HttpHeaders& headers = {}) = 0;

    // Streaming operations for SSE
    virtual std::expected<void, NetworkError> execute_streaming(
        const HttpRequest& request,
        StreamingCallback callback,
        std::atomic<bool>* stop_flag = nullptr) = 0;

    // Configuration
    virtual void set_default_timeout(std::chrono::seconds timeout) = 0;
    virtual void set_default_headers(const HttpHeaders& headers) = 0;
    virtual void set_user_agent(const std::string& user_agent) = 0;
    virtual void set_follow_redirects(bool follow) = 0;
    virtual void set_verify_ssl(bool verify) = 0;

    // Connection management
    virtual void set_connection_pool_size(size_t size) = 0;
    virtual void clear_connection_pool() = 0;
};

// HTTP client configuration
struct HttpClientConfig {
    std::chrono::seconds default_timeout{30};
    HttpHeaders default_headers;
    std::string user_agent = "PresenceForPlex/1.0";
    bool follow_redirects = true;
    int max_redirects = 5;
    bool verify_ssl = true;
    size_t connection_pool_size = 10;
    std::chrono::seconds keep_alive_timeout{60};

    // Proxy settings
    std::optional<std::string> proxy_url;
    std::optional<std::string> proxy_auth; // "username:password"

    // SSL settings
    std::optional<std::string> ca_cert_path;
    std::optional<std::string> client_cert_path;
    std::optional<std::string> client_key_path;

    bool is_valid() const;
};

// Factory function for creating HTTP clients
std::unique_ptr<HttpClient> create_http_client(const HttpClientConfig& config = {});

} // namespace services
} // namespace presence_for_plex
