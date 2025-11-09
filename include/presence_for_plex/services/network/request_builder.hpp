#pragma once

#include "presence_for_plex/services/network/http_types.hpp"
#include <string>
#include <chrono>

namespace presence_for_plex {
namespace services {

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

} // namespace services
} // namespace presence_for_plex
