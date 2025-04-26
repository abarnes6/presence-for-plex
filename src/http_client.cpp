#include "http_client.h"
#include "thread_utils.h"

HttpClient::HttpClient()
{
    curl_global_init(CURL_GLOBAL_ALL);
    m_curl = curl_easy_init();
    LOG_DEBUG("HttpClient", "HttpClient initialized");
}

HttpClient::~HttpClient()
{
    if (m_sseRunning)
    {
        stopSSE();
    }

    if (m_curl)
    {
        curl_easy_cleanup(m_curl);
        m_curl = nullptr;
    }

    curl_global_cleanup();
    LOG_DEBUG("HttpClient", "HttpClient object destroyed");
}

size_t HttpClient::writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    std::string *response = static_cast<std::string *>(userdata);
    size_t totalSize = size * nmemb;
    response->append(ptr, totalSize);
    return totalSize;
}

struct curl_slist *HttpClient::createHeaderList(const std::map<std::string, std::string> &headers)
{
    struct curl_slist *curl_headers = NULL;
    for (const auto &[key, value] : headers)
    {
        std::string header = key + ": " + value;
        curl_headers = curl_slist_append(curl_headers, header.c_str());
    }
    LOG_DEBUG_STREAM("HttpClient", "Created header list with " << headers.size() << " headers");
    return curl_headers;
}

bool HttpClient::setupCommonOptions(const std::string &url, const std::map<std::string, std::string> &headers)
{
    if (!m_curl)
    {
        LOG_ERROR("HttpClient", "CURL not initialized");
        return false;
    }

    curl_easy_reset(m_curl);
    curl_easy_setopt(m_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(m_curl, CURLOPT_TIMEOUT, 10L);

    LOG_DEBUG_STREAM("HttpClient", "Set up request to URL: " << url);
    return true;
}

bool HttpClient::checkResponse(CURLcode res)
{
    if (res != CURLE_OK)
    {
        LOG_ERROR("HttpClient", "Request failed: " + std::string(curl_easy_strerror(res)));
        return false;
    }

    long response_code;
    curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &response_code);

    if (response_code < 200 || response_code >= 300)
    {
        LOG_ERROR("HttpClient", "Request failed with HTTP status code: " + std::to_string(response_code));
        return false;
    }

    LOG_DEBUG_STREAM("HttpClient", "Request successful with status code: " << response_code);
    return true;
}

bool HttpClient::get(const std::string &url, const std::map<std::string, std::string> &headers, std::string &response)
{
    LOG_INFO_STREAM("HttpClient", "Sending GET request to: " << url);

    if (!setupCommonOptions(url, headers))
    {
        return false;
    }

    curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &response);

    struct curl_slist *curl_headers = createHeaderList(headers);
    curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, curl_headers);

    LOG_DEBUG("HttpClient", "Executing GET request");
    CURLcode res = curl_easy_perform(m_curl);
    curl_slist_free_all(curl_headers);

    bool success = checkResponse(res);
    if (success)
    {
        LOG_DEBUG_STREAM("HttpClient", "GET request succeeded with response size: " << response.size() << " bytes");
    }
    return success;
}

bool HttpClient::post(const std::string &url, const std::map<std::string, std::string> &headers,
                      const std::string &body, std::string &response)
{
    LOG_INFO_STREAM("HttpClient", "Sending POST request to: " << url);
    LOG_DEBUG_STREAM("HttpClient", "POST body size: " << body.size() << " bytes");

    if (!setupCommonOptions(url, headers))
    {
        return false;
    }

    curl_easy_setopt(m_curl, CURLOPT_POST, 1L);
    curl_easy_setopt(m_curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &response);

    struct curl_slist *curl_headers = createHeaderList(headers);
    curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, curl_headers);

    LOG_DEBUG("HttpClient", "Executing POST request");
    CURLcode res = curl_easy_perform(m_curl);
    curl_slist_free_all(curl_headers);

    bool success = checkResponse(res);
    if (success)
    {
        LOG_DEBUG_STREAM("HttpClient", "POST request succeeded with response size: " << response.size() << " bytes");
    }
    return success;
}

size_t HttpClient::sseCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    HttpClient *client = static_cast<HttpClient *>(userdata);
    size_t total_size = size * nmemb;

    client->m_sseBuffer.append(ptr, total_size);
    LOG_DEBUG_STREAM("HttpClient", "SSE received " << total_size << " bytes");

    // Process events in buffer
    size_t pos;
    while ((pos = client->m_sseBuffer.find("\n\n")) != std::string::npos)
    {
        std::string event = client->m_sseBuffer.substr(0, pos);
        client->m_sseBuffer.erase(0, pos + 2); // +2 for \n\n

        size_t data_pos = event.find("data: ");
        if (data_pos != std::string::npos)
        {
            std::string data = event.substr(data_pos + 6);
            LOG_DEBUG_STREAM("HttpClient", "SSE event received, data size: " << data.size() << " bytes");
            if (client->m_eventCallback)
            {
                LOG_DEBUG("HttpClient", "Calling SSE event callback");
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
    if (httpClient->m_stopFlag)
    {
        LOG_DEBUG("HttpClient", "SSE connection termination requested");
        return 1; // Abort transfer
    }
    return 0; // Continue transfer
}

bool HttpClient::stopSSE()
{
    m_stopFlag = true;
    LOG_INFO("HttpClient", "Requesting SSE connection termination");

    if (m_sseThread.joinable())
    {
        std::unique_lock<std::mutex> lock(m_sseMutex);
        if (m_sseRunning)
        {
            LOG_INFO("HttpClient", "Waiting for SSE thread to stop");
            
            bool threadStopped = m_sseCondVar.wait_for(lock, std::chrono::seconds(5), 
                [this] { return !m_sseRunning; });
                
            if (!threadStopped) {
                LOG_WARNING("HttpClient", "SSE thread did not respond to stop request in time");
            }
        }
        
        lock.unlock();
        ThreadUtils::joinWithTimeout(m_sseThread, std::chrono::seconds(2), "SSE thread");
    }

    LOG_INFO("HttpClient", "SSE thread stopped successfully");
    return true;
}

bool HttpClient::startSSE(const std::string &url, const std::map<std::string, std::string> &headers,
                          EventCallback callback)
{
    LOG_INFO_STREAM("HttpClient", "Starting SSE connection to: " << url);

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
                LOG_ERROR("HttpClient", "Failed to initialize CURL for SSE connection");
                std::lock_guard<std::mutex> lock(m_sseMutex);
                m_sseRunning = false;
                m_sseCondVar.notify_all();
                return;
            }
            LOG_DEBUG("HttpClient", "CURL initialized for SSE connection");

            int retryCount = 0;
            while (!m_stopFlag) {
                // Setup SSE connection
                curl_easy_reset(sse_curl);
                curl_easy_setopt(sse_curl, CURLOPT_URL, url.c_str());
                curl_easy_setopt(sse_curl, CURLOPT_WRITEFUNCTION, sseCallback);
                curl_easy_setopt(sse_curl, CURLOPT_WRITEDATA, this);
                curl_easy_setopt(sse_curl, CURLOPT_TCP_NODELAY, 1L);
                
                // Setup progress monitoring for cancelation
                curl_easy_setopt(sse_curl, CURLOPT_XFERINFOFUNCTION, sseCallbackProgress);
                curl_easy_setopt(sse_curl, CURLOPT_XFERINFODATA, this);
                curl_easy_setopt(sse_curl, CURLOPT_NOPROGRESS, 0L);

                // Setup headers
                struct curl_slist *curl_headers = createHeaderList(headers);
                curl_headers = curl_slist_append(curl_headers, "Accept: text/event-stream");
                curl_easy_setopt(sse_curl, CURLOPT_HTTPHEADER, curl_headers);

                if (m_stopFlag) {
                    LOG_INFO("HttpClient", "SSE connection setup aborted due to stop request");
                    curl_slist_free_all(curl_headers);
                    break;
                }

                // Perform request
                LOG_INFO_STREAM("HttpClient", "Establishing SSE connection, attempt #" << (retryCount + 1));
                CURLcode res = curl_easy_perform(sse_curl);
                        
                if (res == CURLE_ABORTED_BY_CALLBACK) {
                    LOG_INFO("HttpClient", "SSE connection aborted by callback");
                } else if (res != CURLE_OK) {
                    retryCount++;
                    LOG_WARNING_STREAM("HttpClient", "SSE connection error: " << curl_easy_strerror(res) 
                                     << ", retry count: " << retryCount);
                    if (!m_stopFlag) {
                        int retryDelay = (std::min)(5 * retryCount, 60); // Exponential backoff with max 60 seconds
                        LOG_DEBUG_STREAM("HttpClient", "Retrying SSE connection in " << retryDelay << " seconds");
                        std::this_thread::sleep_for(std::chrono::seconds(retryDelay));
                    }
                } else {
                    // Connection ended normally, reset retry count
                    LOG_INFO("HttpClient", "SSE connection ended normally");
                    retryCount = 0;
                }

                curl_slist_free_all(curl_headers);

                if (m_stopFlag) {
                    LOG_INFO("HttpClient", "Exiting SSE connection loop due to stop request");
                    break;
                }
            }
            
            if (sse_curl) {
                curl_easy_cleanup(sse_curl);
                LOG_DEBUG("HttpClient", "Cleaned up CURL handle for SSE");
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

    LOG_DEBUG("HttpClient", "SSE thread started successfully");
    return true;
}