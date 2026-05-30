#pragma once

#include "common/CancellationToken.h"

#include <functional>
#include <string>
#include <vector>

namespace voice_agent {

struct HttpRequest {
    std::string url;
    std::vector<std::string> headers;
    std::string body;
    long timeoutSeconds = 90L;
    const CancellationToken* cancellationToken = nullptr;
};

struct MultipartField {
    std::string name;
    std::string value;
};

struct MultipartFile {
    std::string fieldName;
    std::string filePath;
    std::string contentType;
    std::string fileName;
};

struct HttpResponse {
    long statusCode = 0;
    std::string body;
    bool cancelled = false;
};

class HttpClient {
public:
    HttpResponse Get(const HttpRequest& request) const;
    HttpResponse Post(const HttpRequest& request) const;
    HttpResponse PostStream(const HttpRequest& request, const std::function<void(const std::string&)>& onChunk) const;
    HttpResponse PostMultipart(
        const std::string& url,
        const std::vector<std::string>& headers,
        const std::vector<MultipartField>& fields,
        const MultipartFile& file,
        long timeoutSeconds = 90L,
        const CancellationToken* cancellationToken = nullptr) const;
};

}  // namespace voice_agent