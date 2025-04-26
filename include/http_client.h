#pragma once

// Standard library headers
#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

// Third-party headers
#include <curl/curl.h>
#include <nlohmann/json.hpp>

// Project headers
#include "logger.h"

/**
 * @brief HTTP client with support for GET, POST, and Server-Sent Events (SSE)
 */
class HttpClient
{
public:
    HttpClient();
    ~HttpClient();

    // Callback type for SSE events
    using EventCallback = std::function<void(const std::string &)>;

    // Regular HTTP requests
    bool get(const std::string &url,
             const std::map<std::string, std::string> &headers,
             std::string &response);

    bool post(const std::string &url,
              const std::map<std::string, std::string> &headers,
              const std::string &body,
              std::string &response);

    // Server-Sent Events (SSE)
    bool startSSE(const std::string &url,
                  const std::map<std::string, std::string> &headers,
                  EventCallback callback);
    bool stopSSE();

private:
    // CURL callback functions
    static size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata);
    static size_t sseCallback(char *ptr, size_t size, size_t nmemb, void *userdata);
    static int sseCallbackProgress(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                                   curl_off_t ultotal, curl_off_t ulnow);

    // Helper methods
    bool setupCommonOptions(const std::string &url, const std::map<std::string, std::string> &headers);
    struct curl_slist *createHeaderList(const std::map<std::string, std::string> &headers);
    bool checkResponse(CURLcode res);

    // Member variables
    CURL *m_curl;
    std::thread m_sseThread;
    EventCallback m_eventCallback;
    std::string m_sseBuffer;

    // Thread synchronization for SSE
    std::atomic<bool> m_stopFlag{false};
    std::atomic<bool> m_sseRunning{false};
    std::mutex m_sseMutex;
    std::condition_variable m_sseCondVar;
};
