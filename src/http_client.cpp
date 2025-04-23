#include "http_client.h"
#include "logger.h"
#include <sstream>
#include <future>

HttpClient::HttpClient()
{
    curl_global_init(CURL_GLOBAL_ALL);
    m_curl = curl_easy_init();
}

HttpClient::~HttpClient()
{
    stopSSE();

    if (m_sseThread.joinable())
    {
        m_sseThread.join();
    }

    if (m_curl)
    {
        curl_easy_cleanup(m_curl);
        m_curl = nullptr;
    }

    curl_global_cleanup();
    LOG_INFO("HttpClient", "HttpClient object destroyed");
}

size_t HttpClient::writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    std::string *response = static_cast<std::string *>(userdata);
    response->append(ptr, size * nmemb);
    return size * nmemb;
}

bool HttpClient::get(const std::string &url, const std::map<std::string, std::string> &headers, std::string &response)
{
    if (!m_curl)
    {
        LOG_ERROR("HttpClient", "CURL not initialized");
        return false;
    }

    curl_easy_reset(m_curl);
    curl_easy_setopt(m_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(m_curl, CURLOPT_TIMEOUT, 10L);

    struct curl_slist *curl_headers = NULL;
    for (const auto &[key, value] : headers)
    {
        std::string header = key + ": " + value;
        curl_headers = curl_slist_append(curl_headers, header.c_str());
    }
    curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, curl_headers);

    CURLcode res = curl_easy_perform(m_curl);
    curl_slist_free_all(curl_headers);

    if (res != CURLE_OK)
    {
        LOG_ERROR("HttpClient", "GET request failed: " + std::string(curl_easy_strerror(res)));
        return false;
    }

    long response_code;
    curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &response_code);
    if (response_code < 200 || response_code >= 300)
    {
        LOG_ERROR("HttpClient", "GET request failed with code: " + std::to_string(response_code));
        return false;
    }

    return true;
}

bool HttpClient::post(const std::string &url, const std::map<std::string, std::string> &headers,
                      const std::string &body, std::string &response)
{
    if (!m_curl)
    {
        LOG_ERROR("HttpClient", "CURL not initialized");
        return false;
    }

    curl_easy_reset(m_curl);
    curl_easy_setopt(m_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(m_curl, CURLOPT_POST, 1L);
    curl_easy_setopt(m_curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(m_curl, CURLOPT_TIMEOUT, 10L);

    struct curl_slist *curl_headers = NULL;
    for (const auto &[key, value] : headers)
    {
        std::string header = key + ": " + value;
        curl_headers = curl_slist_append(curl_headers, header.c_str());
    }
    curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, curl_headers);

    CURLcode res = curl_easy_perform(m_curl);
    curl_slist_free_all(curl_headers);

    if (res != CURLE_OK)
    {
        LOG_ERROR("HttpClient", "POST request failed: " + std::string(curl_easy_strerror(res)));
        return false;
    }

    long response_code;
    curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &response_code);
    if (response_code < 200 || response_code >= 300)
    {
        LOG_ERROR("HttpClient", "POST request failed with code: " + std::to_string(response_code));
        return false;
    }

    return true;
}

size_t HttpClient::sseCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    HttpClient *client = static_cast<HttpClient *>(userdata);
    size_t total_size = size * nmemb;

    client->m_sseBuffer.append(ptr, total_size);

    size_t pos;
    while ((pos = client->m_sseBuffer.find("\n\n")) != std::string::npos)
    {
        std::string event = client->m_sseBuffer.substr(0, pos);
        client->m_sseBuffer.erase(0, pos + 2); // +2 for \n\n

        size_t data_pos = event.find("data: ");
        if (data_pos != std::string::npos)
        {
            std::string data = event.substr(data_pos + 6);
            if (client->m_eventCallback)
            {
                client->m_eventCallback(data);
            }
        }
    }

    return total_size;
}

int HttpClient::sseCallbackProgress(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                                    curl_off_t ultotal, curl_off_t ulnow)
{
    HttpClient *httpClient = static_cast<HttpClient *>(clientp);
    LOG_DEBUG_STREAM("HttpClient", "SSE progress called, stop flag: " << httpClient->m_stopFlag.load());
    if (httpClient->m_stopFlag)
    {
        return 1;
    }
    return 0;
}

bool HttpClient::stopSSE()
{
    // Set the stop flag
    m_stopFlag = true;

    // Wait for the SSE thread to finish
    std::unique_lock<std::mutex> lock(m_sseMutex);
    if (m_sseRunning)
    {
        LOG_INFO("HttpClient", "Waiting for SSE thread to stop");
        m_sseCondVar.wait(lock, [this]
                          { return !m_sseRunning; });
    }

    LOG_INFO("HttpClient", "SSE thread stopped");
    return true;
}

bool HttpClient::startSSE(const std::string &url, const std::map<std::string, std::string> &headers,
                          EventCallback callback)
{
    {
        std::lock_guard<std::mutex> lock(m_sseMutex);
        m_stopFlag = false;
        m_sseRunning = true;
        m_eventCallback = callback;
        m_sseBuffer.clear();
    }

    m_sseThread = std::thread([this, url, headers]()
                              {
        LOG_INFO("HttpClient", "SSE thread starting");
        
        try {
            
            CURL* sse_curl = curl_easy_init();
            if (!sse_curl) {
                LOG_ERROR("HttpClient", "Failed to initialize CURL for SSE");
                std::lock_guard<std::mutex> lock(m_sseMutex);
                m_sseRunning = false;
                m_sseCondVar.notify_all();
                return;
            }

            while (!m_stopFlag) {
                
                curl_easy_reset(sse_curl);
                curl_easy_setopt(sse_curl, CURLOPT_URL, url.c_str());
                curl_easy_setopt(sse_curl, CURLOPT_WRITEFUNCTION, sseCallback);
                curl_easy_setopt(sse_curl, CURLOPT_WRITEDATA, this);
                curl_easy_setopt(sse_curl, CURLOPT_TCP_NODELAY, 1L);
                
                curl_easy_setopt(sse_curl, CURLOPT_XFERINFOFUNCTION, sseCallbackProgress);
                curl_easy_setopt(sse_curl, CURLOPT_XFERINFODATA, this);
                curl_easy_setopt(sse_curl, CURLOPT_NOPROGRESS, 0L);

                struct curl_slist *curl_headers = NULL;
                for (const auto& [key, value] : headers) {
                    std::string header = key + ": " + value;
                    curl_headers = curl_slist_append(curl_headers, header.c_str());
                }
                curl_headers = curl_slist_append(curl_headers, "Accept: text/event-stream");
                curl_easy_setopt(sse_curl, CURLOPT_HTTPHEADER, curl_headers);

                if (m_stopFlag) {
                    curl_slist_free_all(curl_headers);
                    break;
                }

                CURLcode res = curl_easy_perform(sse_curl);
                        
                if (res == CURLE_ABORTED_BY_CALLBACK) {
                    LOG_INFO("HttpClient", "SSE connection aborted");
                } else if (res != CURLE_OK) {
                    LOG_ERROR_STREAM("HttpClient", "SSE connection error: " << curl_easy_strerror(res));
                    if (!m_stopFlag) {
                        std::this_thread::sleep_for(std::chrono::seconds(5));
                    }
                }

                curl_slist_free_all(curl_headers);

                if (m_stopFlag) {
                    break;
                }
            }
            
            if (sse_curl) {
                curl_easy_cleanup(sse_curl);
            }
        }
        catch (const std::exception& e) {
            LOG_ERROR("HttpClient", "Exception in SSE thread: " + std::string(e.what()));
        }
        catch (...) {
            LOG_ERROR("HttpClient", "Unknown exception in SSE thread");
        }
        
        // Mark as not running and notify waiting threads
        {
            std::lock_guard<std::mutex> lock(m_sseMutex);
            m_sseRunning = false;
            m_sseCondVar.notify_all();
        }
        
        LOG_INFO("HttpClient", "SSE thread exiting"); });

    return true;
}