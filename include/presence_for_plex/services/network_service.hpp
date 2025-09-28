#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <chrono>
#include <span>
#include <future>
#include <expected>
#include <optional>
#include <atomic>

namespace presence_for_plex {
namespace services {

// HTTP method enumeration
enum class HttpMethod {
    GET,
    POST,
    PUT,
    DELETE_,
    PATCH,
    HEAD,
    OPTIONS
};

// HTTP status codes
enum class HttpStatus {
    OK = 200,
    Created = 201,
    NoContent = 204,
    BadRequest = 400,
    Unauthorized = 401,
    Forbidden = 403,
    NotFound = 404,
    MethodNotAllowed = 405,
    InternalServerError = 500,
    BadGateway = 502,
    ServiceUnavailable = 503,
    GatewayTimeout = 504
};

// Network error types
enum class NetworkError {
    ConnectionFailed,
    Timeout,
    DNSResolutionFailed,
    SSLError,
    InvalidUrl,
    TooManyRedirects,
    BadResponse,
    Cancelled
};

// HTTP headers type
using HttpHeaders = std::unordered_map<std::string, std::string>;

// HTTP request structure
struct HttpRequest {
    HttpMethod method = HttpMethod::GET;
    std::string url;
    HttpHeaders headers;
    std::string body;
    std::chrono::seconds timeout{30};
    bool follow_redirects = true;
    int max_redirects = 5;

    // Authentication
    std::optional<std::string> basic_auth; // "username:password"
    std::optional<std::string> bearer_token;

    // SSL options
    bool verify_ssl = true;
    std::optional<std::string> client_cert_path;
    std::optional<std::string> client_key_path;

    bool is_valid() const;
};

// HTTP response structure
struct HttpResponse {
    HttpStatus status_code;
    HttpHeaders headers;
    std::string body;
    std::chrono::milliseconds response_time;
    std::string final_url; // After redirects
    size_t content_length = 0;

    // Convenience methods
    bool is_success() const;
    bool is_client_error() const;
    bool is_server_error() const;
    std::optional<std::string> get_header(const std::string& name) const;
};

// Progress callback for long operations
using ProgressCallback = std::function<void(size_t downloaded, size_t total)>;

// Streaming data callback for SSE and similar use cases
using StreamingCallback = std::function<void(const std::string& data_chunk)>;

// Abstract HTTP client interface
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

// Factory interface for creating HTTP clients
class HttpClientFactory {
public:
    virtual ~HttpClientFactory() = default;

    enum class ClientType {
        Curl,      // libcurl-based implementation
        Native,    // Platform-native implementation
        Mock       // For testing
    };

    virtual std::unique_ptr<HttpClient> create_client(
        ClientType type,
        const HttpClientConfig& config = {}
    ) = 0;

    static std::unique_ptr<HttpClientFactory> create_default_factory();
};

// Request builder for fluent API
class RequestBuilder {
public:
    RequestBuilder() = default;
    explicit RequestBuilder(const std::string& url);

    RequestBuilder& method(HttpMethod method);
    RequestBuilder& url(const std::string& url);
    RequestBuilder& header(const std::string& name, const std::string& value);
    RequestBuilder& headers(const HttpHeaders& headers);
    RequestBuilder& body(const std::string& body);
    RequestBuilder& json_body(const std::string& json);
    RequestBuilder& timeout(std::chrono::seconds timeout);
    RequestBuilder& basic_auth(const std::string& username, const std::string& password);
    RequestBuilder& bearer_token(const std::string& token);
    RequestBuilder& follow_redirects(bool follow = true);
    RequestBuilder& verify_ssl(bool verify = true);

    HttpRequest build() const;

private:
    HttpRequest m_request;
};

// URL utilities
class UrlUtils {
public:
    static std::string encode(const std::string& str);
    static std::string decode(const std::string& str);
    static std::string join_path(const std::string& base, const std::string& path);
    static std::unordered_map<std::string, std::string> parse_query_string(const std::string& query);
    static std::string build_query_string(const std::unordered_map<std::string, std::string>& params);
    static bool is_valid_url(const std::string& url);
    static std::optional<std::string> get_host(const std::string& url);
    static std::optional<int> get_port(const std::string& url);
    static std::optional<std::string> get_scheme(const std::string& url);
};

// Server-Sent Events (SSE) client interface
class SSEClient {
public:
    SSEClient();
    ~SSEClient();

    // Move-only semantics
    SSEClient(const SSEClient&) = delete;
    SSEClient& operator=(const SSEClient&) = delete;
    SSEClient(SSEClient&&) = delete;
    SSEClient& operator=(SSEClient&&) = delete;

    bool connect(const std::string& url, const HttpHeaders& headers);
    void disconnect();
    bool is_connected() const;
    void process_sse_data(const std::string& data);

    void set_message_callback(std::function<void(const std::string&)> callback);
    void set_error_callback(std::function<void(const std::string&)> callback);
    void set_connection_callback(std::function<void(bool)> callback);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace services
} // namespace presence_for_plex
