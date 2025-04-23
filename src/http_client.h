#pragma once

#include <string>
#include <functional>
#include <map>
#include <curl/curl.h>
#include <atomic>
#include <thread>
#include <nlohmann/json.hpp>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <future>

class HttpClient
{
public:
    HttpClient();
    ~HttpClient();

    // Regular HTTP requests
    bool get(const std::string &url, const std::map<std::string, std::string> &headers, std::string &response);
    bool post(const std::string &url, const std::map<std::string, std::string> &headers,
              const std::string &body, std::string &response);

    // Server-Sent Events (SSE)
    using EventCallback = std::function<void(const std::string &)>;
    bool startSSE(const std::string &url, const std::map<std::string, std::string> &headers,
                  EventCallback callback);
    bool stopSSE();

private:
    static size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata);
    static size_t sseCallback(char *ptr, size_t size, size_t nmemb, void *userdata);
    static int sseCallbackProgress(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);

    CURL *m_curl;
    std::thread m_sseThread;
    EventCallback m_eventCallback;
    std::string m_sseBuffer;

    std::atomic<bool> m_stopFlag{false};
    // Thread synchronization for SSE
    std::atomic<bool> m_sseRunning{false};
    std::mutex m_sseMutex;
    std::condition_variable m_sseCondVar;
};
