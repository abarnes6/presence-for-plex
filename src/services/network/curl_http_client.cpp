#include "presence_for_plex/services/network_service.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <curl/curl.h>
#include <fstream>
#include <future>
#include <chrono>

namespace presence_for_plex {
namespace services {

// Callback for writing HTTP response data
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}

// Callback for writing to file
static size_t WriteFileCallback(void* contents, size_t size, size_t nmemb, std::ofstream* file) {
    size_t total_size = size * nmemb;
    file->write(static_cast<char*>(contents), static_cast<std::streamsize>(total_size));
    return total_size;
}

// Callback for reading from file
static size_t ReadFileCallback(void* contents, size_t size, size_t nmemb, std::ifstream* file) {
    size_t total_size = size * nmemb;
    file->read(static_cast<char*>(contents), static_cast<std::streamsize>(total_size));
    return static_cast<size_t>(file->gcount());
}

// Callback for progress
static int CurlProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                           curl_off_t ultotal, curl_off_t ulnow) {
	(void)ultotal;
	(void)ulnow;
    auto* callback = static_cast<ProgressCallback*>(clientp);
    if (callback && dltotal > 0) {
        (*callback)(static_cast<size_t>(dlnow), static_cast<size_t>(dltotal));
    }
    return 0;
}

// Structure to pass both callbacks to curl
struct StreamingCallbackData {
    StreamingCallback* data_callback;
    bool connection_established = false;
};

// Callback for HTTP headers (to detect connection establishment)
static size_t HeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t total_size = size * nitems;
    auto* callback_data = static_cast<StreamingCallbackData*>(userdata);

    // Check if this is the HTTP status line
    std::string header_line(buffer, total_size);
    if (header_line.find("HTTP/") == 0) {
        // Extract status code
        size_t first_space = header_line.find(' ');
        if (first_space != std::string::npos) {
            size_t second_space = header_line.find(' ', first_space + 1);
            if (second_space != std::string::npos) {
                std::string status_code_str = header_line.substr(first_space + 1, second_space - first_space - 1);
                try {
                    int status_code = std::stoi(status_code_str);
                    if (status_code == 200 && !callback_data->connection_established) {
                        // Connection established! Send an empty chunk to signal this
                        callback_data->connection_established = true;
                        if (callback_data->data_callback) {
                            // Send a SSE comment to indicate connection is established
                            (*callback_data->data_callback)(": connection established\n\n");
                        }
                    }
                } catch (...) {
                    // Ignore parsing errors
                }
            }
        }
    }

    return total_size;
}

// Callback for streaming data (for SSE)
static size_t StreamingWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    auto* callback_data = static_cast<StreamingCallbackData*>(userp);

    if (callback_data && callback_data->data_callback) {
        std::string data_chunk(static_cast<char*>(contents), total_size);
        (*callback_data->data_callback)(data_chunk);
    }

    return total_size;
}

// Progress callback for stopping SSE threads
static int ProgressCallbackForStop(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                                   curl_off_t ultotal, curl_off_t ulnow) {
	(void)dltotal;
	(void)dlnow;
	(void)ultotal;
	(void)ulnow;
    auto* stop_flag = static_cast<std::atomic<bool>*>(clientp);
    // Return non-zero to abort the transfer when stop flag is false (m_running = false means stop)
    return (stop_flag && stop_flag->load() == false) ? 1 : 0;
}

class CurlHttpClient : public HttpClient {
public:
    CurlHttpClient(const HttpClientConfig& config = {}) : m_config(config) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~CurlHttpClient() override {
        curl_global_cleanup();
    }

    std::expected<HttpResponse, NetworkError> execute(const HttpRequest& request) override {
        PLEX_LOG_DEBUG("CurlHttpClient", "Starting HTTP request to: " + request.url);

        if (!request.is_valid()) {
            PLEX_LOG_ERROR("CurlHttpClient", "Invalid URL provided: " + request.url);
            return std::unexpected<NetworkError>(NetworkError::InvalidUrl);
        }

        CURL* curl = curl_easy_init();
        if (!curl) {
            PLEX_LOG_ERROR("CurlHttpClient", "Failed to initialize curl handle");
            return std::unexpected<NetworkError>(NetworkError::ConnectionFailed);
        }

        HttpResponse response;
        std::string response_body;
        auto start_time = std::chrono::steady_clock::now();

        // Setup basic options
        curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

        // Set method and body
        setup_method_and_body(curl, request);

        // Set headers
        struct curl_slist* header_list = setup_headers(request);
        if (header_list) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
        }

        // Set timeouts
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, request.timeout.count());
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

        // Set redirects
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, request.follow_redirects ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, static_cast<long>(request.max_redirects));

        // Set SSL verification
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, request.verify_ssl ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, request.verify_ssl ? 2L : 0L);

        PLEX_LOG_DEBUG("CurlHttpClient", "Performing HTTP request...");

        // Perform request
        CURLcode res = curl_easy_perform(curl);

        // Get response info
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

        char* final_url;
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &final_url);

        double content_length;
        curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &content_length);

        // Cleanup
        if (header_list) {
            curl_slist_free_all(header_list);
        }
        curl_easy_cleanup(curl);

        // Calculate response time
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        if (res != CURLE_OK) {
            PLEX_LOG_ERROR("CurlHttpClient", "HTTP request failed: " + std::string(curl_easy_strerror(res)));
            return std::unexpected<NetworkError>(curl_error_to_network_error(res));
        }

        PLEX_LOG_DEBUG("CurlHttpClient", "HTTP request completed in " + std::to_string(duration.count()) + "ms with status " + std::to_string(response_code));

        // Build response
        response.status_code = static_cast<HttpStatus>(response_code);
        response.body = std::move(response_body);
        response.response_time = duration;
        response.final_url = final_url ? std::string(final_url) : request.url;
        response.content_length = content_length > 0 ? static_cast<size_t>(content_length) : response.body.size();

        return response;
    }

    std::future<std::expected<HttpResponse, NetworkError>> execute_async(
        const HttpRequest& request) override {
        return std::async(std::launch::async, [this, request]() {
            return execute(request);
        });
    }

    std::expected<HttpResponse, NetworkError> get(
        const std::string& url,
        const HttpHeaders& headers) override {
        HttpRequest request;
        request.method = HttpMethod::GET;
        request.url = url;
        request.headers = headers;
        return execute(request);
    }

    std::expected<HttpResponse, NetworkError> post(
        const std::string& url,
        const std::string& body,
        const HttpHeaders& headers) override {
        HttpRequest request;
        request.method = HttpMethod::POST;
        request.url = url;
        request.body = body;
        request.headers = headers;
        return execute(request);
    }

    std::expected<HttpResponse, NetworkError> post_json(
        const std::string& url,
        const std::string& json_body,
        const HttpHeaders& headers) override {
        HttpHeaders json_headers = headers;
        json_headers["Content-Type"] = "application/json";
        return post(url, json_body, json_headers);
    }

    std::expected<HttpResponse, NetworkError> put(
        const std::string& url,
        const std::string& body,
        const HttpHeaders& headers) override {
        HttpRequest request;
        request.method = HttpMethod::PUT;
        request.url = url;
        request.body = body;
        request.headers = headers;
        return execute(request);
    }

    std::expected<HttpResponse, NetworkError> delete_resource(
        const std::string& url,
        const HttpHeaders& headers) override {
        HttpRequest request;
        request.method = HttpMethod::DELETE_;
        request.url = url;
        request.headers = headers;
        return execute(request);
    }

    std::expected<void, NetworkError> download_file(
        const std::string& url,
        const std::string& file_path,
        ProgressCallback progress) override {
        PLEX_LOG_DEBUG("CurlHttpClient", "Starting file download from: " + url + " to: " + file_path);

        CURL* curl = curl_easy_init();
        if (!curl) {
            PLEX_LOG_ERROR("CurlHttpClient", "Failed to initialize curl handle for file download");
            return std::unexpected<NetworkError>(NetworkError::ConnectionFailed);
        }

        std::ofstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            PLEX_LOG_ERROR("CurlHttpClient", "Failed to open file for writing: " + file_path);
            curl_easy_cleanup(curl);
            return std::unexpected<NetworkError>(NetworkError::BadResponse);
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);

        if (progress) {
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgressCallback);
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        }

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            PLEX_LOG_ERROR("CurlHttpClient", "File download failed: " + std::string(curl_easy_strerror(res)));
            return std::unexpected<NetworkError>(curl_error_to_network_error(res));
        }

        PLEX_LOG_DEBUG("CurlHttpClient", "File download completed successfully");
        return {};
    }

    std::expected<HttpResponse, NetworkError> upload_file(
        const std::string& url,
        const std::string& file_path,
        const std::string& field_name,
        const HttpHeaders& headers) override {
        (void)headers;
        PLEX_LOG_DEBUG("CurlHttpClient", "Starting file upload from: " + file_path + " to: " + url);

        CURL* curl = curl_easy_init();
        if (!curl) {
            PLEX_LOG_ERROR("CurlHttpClient", "Failed to initialize curl handle for file upload");
            return std::unexpected<NetworkError>(NetworkError::ConnectionFailed);
        }

        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            PLEX_LOG_ERROR("CurlHttpClient", "Failed to open file for reading: " + file_path);
            curl_easy_cleanup(curl);
            return std::unexpected<NetworkError>(NetworkError::BadResponse);
        }

        // Get file size
        file.seekg(0, std::ios::end);
        size_t file_size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        std::string response_body;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, ReadFileCallback);
        curl_easy_setopt(curl, CURLOPT_READDATA, &file);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(file_size));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

        // Set multipart form
        curl_mime* form = curl_mime_init(curl);
        curl_mimepart* field = curl_mime_addpart(form);
        curl_mime_name(field, field_name.c_str());
        curl_mime_filedata(field, file_path.c_str());
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);

        CURLcode res = curl_easy_perform(curl);

        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

        curl_mime_free(form);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            PLEX_LOG_ERROR("CurlHttpClient", "File upload failed: " + std::string(curl_easy_strerror(res)));
            return std::unexpected<NetworkError>(curl_error_to_network_error(res));
        }

        PLEX_LOG_DEBUG("CurlHttpClient", "File upload completed with status " + std::to_string(response_code));

        HttpResponse response;
        response.status_code = static_cast<HttpStatus>(response_code);
        response.body = std::move(response_body);
        return response;
    }

    void set_default_timeout(std::chrono::seconds timeout) override {
        m_config.default_timeout = timeout;
    }

    void set_default_headers(const HttpHeaders& headers) override {
        m_config.default_headers = headers;
    }

    void set_user_agent(const std::string& user_agent) override {
        m_config.user_agent = user_agent;
    }

    void set_follow_redirects(bool follow) override {
        m_config.follow_redirects = follow;
    }

    void set_verify_ssl(bool verify) override {
        m_config.verify_ssl = verify;
    }

    void set_connection_pool_size(size_t size) override {
        m_config.connection_pool_size = size;
    }

    void clear_connection_pool() override {
        // curl handles connection pooling internally
    }

    std::expected<void, NetworkError> execute_streaming(
        const HttpRequest& request,
        StreamingCallback callback,
        std::atomic<bool>* stop_flag = nullptr) override {

        PLEX_LOG_DEBUG("CurlHttpClient", "Starting streaming request to: " + request.url);

        if (!request.is_valid()) {
            PLEX_LOG_ERROR("CurlHttpClient", "Invalid URL for streaming request: " + request.url);
            return std::unexpected<NetworkError>(NetworkError::InvalidUrl);
        }

        CURL* curl = curl_easy_init();
        if (!curl) {
            PLEX_LOG_ERROR("CurlHttpClient", "Failed to initialize curl handle for streaming");
            return std::unexpected<NetworkError>(NetworkError::ConnectionFailed);
        }

        // Setup callback data structure
        StreamingCallbackData callback_data;
        callback_data.data_callback = &callback;
        callback_data.connection_established = false;

        // Setup basic options for streaming
        curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StreamingWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &callback_data);

        // Setup header callback to detect connection establishment
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &callback_data);

        // Setup progress callback for clean shutdown
        if (stop_flag) {
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallbackForStop);
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, stop_flag);
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        }

        // Set method and body
        setup_method_and_body(curl, request);

        // Set headers
        struct curl_slist* header_list = setup_headers(request);
        if (header_list) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
        }

        // SSE-specific timeouts: no total timeout, just connection timeout
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);  // No timeout for persistent connection
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);  // Connection timeout only

        // Set low-speed limits to detect dead connections
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);  // 1 byte/sec minimum
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);  // for 60 seconds

        // Don't follow redirects for streaming connections
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);

        // Set SSL verification
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, request.verify_ssl ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, request.verify_ssl ? 2L : 0L);

        PLEX_LOG_DEBUG("CurlHttpClient", "Performing streaming request...");

        // Perform the streaming request - this will block and call the callback for each data chunk
        CURLcode res = curl_easy_perform(curl);

        // Clean up
        if (header_list) {
            curl_slist_free_all(header_list);
        }
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            PLEX_LOG_ERROR("CurlHttpClient", "Streaming request failed: " + std::string(curl_easy_strerror(res)));
            return std::unexpected<NetworkError>(curl_error_to_network_error(res));
        }

        PLEX_LOG_DEBUG("CurlHttpClient", "Streaming request completed");
        return {};
    }

private:
    HttpClientConfig m_config;

    void setup_method_and_body(CURL* curl, const HttpRequest& request) {
        switch (request.method) {
            case HttpMethod::GET:
                PLEX_LOG_DEBUG("CurlHttpClient", "Setting up GET request");
                // Default
                break;
            case HttpMethod::POST:
                PLEX_LOG_DEBUG("CurlHttpClient", "Setting up POST request with body size: " + std::to_string(request.body.length()));
                curl_easy_setopt(curl, CURLOPT_POST, 1L);
                if (!request.body.empty()) {
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, request.body.length());
                }
                break;
            case HttpMethod::PUT:
                PLEX_LOG_DEBUG("CurlHttpClient", "Setting up PUT request with body size: " + std::to_string(request.body.length()));
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
                if (!request.body.empty()) {
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, request.body.length());
                }
                break;
            case HttpMethod::DELETE_:
                PLEX_LOG_DEBUG("CurlHttpClient", "Setting up DELETE request");
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
                break;
            case HttpMethod::PATCH:
                PLEX_LOG_DEBUG("CurlHttpClient", "Setting up PATCH request with body size: " + std::to_string(request.body.length()));
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
                if (!request.body.empty()) {
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, request.body.length());
                }
                break;
            case HttpMethod::HEAD:
                PLEX_LOG_DEBUG("CurlHttpClient", "Setting up HEAD request");
                curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
                break;
            case HttpMethod::OPTIONS:
                PLEX_LOG_DEBUG("CurlHttpClient", "Setting up OPTIONS request");
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "OPTIONS");
                break;
        }
    }

    struct curl_slist* setup_headers(const HttpRequest& request) {
        struct curl_slist* header_list = nullptr;

        // Add default headers
        for (const auto& [key, value] : m_config.default_headers) {
            std::string header = key + ": " + value;
            PLEX_LOG_DEBUG("CurlHttpClient", "Adding default header: " + header);
            header_list = curl_slist_append(header_list, header.c_str());
        }

        // Add request headers (override defaults)
        for (const auto& [key, value] : request.headers) {
            std::string header = key + ": " + value;
            PLEX_LOG_DEBUG("CurlHttpClient", "Adding request header: " + header);
            header_list = curl_slist_append(header_list, header.c_str());
        }

        // Add authentication headers
        if (request.bearer_token) {
            std::string auth_header = "Authorization: Bearer " + *request.bearer_token;
            PLEX_LOG_DEBUG("CurlHttpClient", "Adding bearer token authentication header");
            header_list = curl_slist_append(header_list, auth_header.c_str());
        }

        // Set User-Agent
        if (!m_config.user_agent.empty()) {
            std::string ua_header = "User-Agent: " + m_config.user_agent;
            PLEX_LOG_DEBUG("CurlHttpClient", "Adding User-Agent header: " + ua_header);
            header_list = curl_slist_append(header_list, ua_header.c_str());
        }

        return header_list;
    }

    NetworkError curl_error_to_network_error(CURLcode code) {
        switch (code) {
            case CURLE_COULDNT_RESOLVE_HOST:
            case CURLE_COULDNT_RESOLVE_PROXY:
                return NetworkError::DNSResolutionFailed;
            case CURLE_COULDNT_CONNECT:
                return NetworkError::ConnectionFailed;
            case CURLE_OPERATION_TIMEDOUT:
                return NetworkError::Timeout;
            case CURLE_SSL_CONNECT_ERROR:
            case CURLE_SSL_CERTPROBLEM:
            case CURLE_SSL_CIPHER:
                return NetworkError::SSLError;
            case CURLE_TOO_MANY_REDIRECTS:
                return NetworkError::TooManyRedirects;
            case CURLE_URL_MALFORMAT:
                return NetworkError::InvalidUrl;
            case CURLE_ABORTED_BY_CALLBACK:
                return NetworkError::Cancelled;
            default:
                return NetworkError::BadResponse;
        }
    }
};

// Factory implementation
class DefaultHttpClientFactory : public HttpClientFactory {
public:
    std::unique_ptr<HttpClient> create_client(
        ClientType type,
        const HttpClientConfig& config) override {
        switch (type) {
            case ClientType::Curl:
                return std::make_unique<CurlHttpClient>(config);
            case ClientType::Native:
                // For now, fallback to curl
                return std::make_unique<CurlHttpClient>(config);
            case ClientType::Mock:
                // TODO: Implement mock client for testing
                return std::make_unique<CurlHttpClient>(config);
            default:
                return nullptr;
        }
    }
};

std::unique_ptr<HttpClientFactory> HttpClientFactory::create_default_factory() {
    return std::make_unique<DefaultHttpClientFactory>();
}

} // namespace services
}
