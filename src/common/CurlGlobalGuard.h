#pragma once

#include <curl/curl.h>

#include <stdexcept>
#include <string>

namespace voice_agent {

class CurlGlobalGuard {
public:
    CurlGlobalGuard() {
        const CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (result != CURLE_OK) {
            throw std::runtime_error(std::string("curl_global_init failed: ") + curl_easy_strerror(result));
        }
    }

    ~CurlGlobalGuard() {
        curl_global_cleanup();
    }
};

}  // namespace voice_agent