#pragma once

#include <functional>
#include <string>
#include <vector>

namespace voice_agent {

struct HttpRequest {
    std::string url;
    std::vector<std::string> headers;
    std::string body;
    long timeoutSeconds = 90L;
};

struct HttpResponse {
    long statusCode = 0;
    std::string body;
};

class HttpClient {
public:
    HttpResponse Get(const HttpRequest& request) const;
    HttpResponse Post(const HttpRequest& request) const;
    HttpResponse PostStream(const HttpRequest& request, const std::function<void(const std::string&)>& onChunk) const;
};

}  // namespace voice_agent