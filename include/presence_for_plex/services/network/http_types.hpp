#pragma once

#include <string>
#include <unordered_map>
#include <chrono>
#include <optional>
#include <functional>

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

} // namespace services
} // namespace presence_for_plex
