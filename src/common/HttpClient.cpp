#include "common/HttpClient.h"

#include <curl/curl.h>

#include <functional>
#include <stdexcept>

namespace voice_agent {

namespace {

size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    const auto bytes = size * nmemb;
    auto* output = static_cast<std::string*>(userdata);
    output->append(ptr, bytes);
    return bytes;
}

struct StreamWriteContext {
    std::string* output = nullptr;
    const std::function<void(const std::string&)>* onChunk = nullptr;
};

size_t StreamWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    const auto bytes = size * nmemb;
    auto* context = static_cast<StreamWriteContext*>(userdata);
    const std::string chunk(ptr, bytes);
    context->output->append(chunk);
    (*context->onChunk)(chunk);
    return bytes;
}

HttpResponse PerformPost(
    const HttpRequest& request,
    curl_write_callback writeCallback,
    void* writeData) {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        throw std::runtime_error("curl_easy_init failed.");
    }

    struct curl_slist* headerList = nullptr;
    for (const auto& header : request.headers) {
        headerList = curl_slist_append(headerList, header.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, writeData);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, request.timeoutSeconds);

    const CURLcode result = curl_easy_perform(curl);
    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);

    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        throw std::runtime_error(std::string("HTTP request failed: ") + curl_easy_strerror(result));
    }

    return HttpResponse{responseCode, {}};
}

HttpResponse PerformGet(
    const HttpRequest& request,
    curl_write_callback writeCallback,
    void* writeData) {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        throw std::runtime_error("curl_easy_init failed.");
    }

    struct curl_slist* headerList = nullptr;
    for (const auto& header : request.headers) {
        headerList = curl_slist_append(headerList, header.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, writeData);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, request.timeoutSeconds);

    const CURLcode result = curl_easy_perform(curl);
    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);

    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        throw std::runtime_error(std::string("HTTP request failed: ") + curl_easy_strerror(result));
    }

    return HttpResponse{responseCode, {}};
}

}  // namespace

HttpResponse HttpClient::Get(const HttpRequest& request) const {
    std::string responseBody;
    HttpResponse response = PerformGet(request, WriteCallback, &responseBody);
    response.body = std::move(responseBody);
    return response;
}

HttpResponse HttpClient::Post(const HttpRequest& request) const {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        throw std::runtime_error("curl_easy_init failed.");
    }

    std::string responseBody;
    struct curl_slist* headerList = nullptr;
    for (const auto& header : request.headers) {
        headerList = curl_slist_append(headerList, header.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, request.timeoutSeconds);

    const CURLcode result = curl_easy_perform(curl);
    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);

    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        throw std::runtime_error(std::string("HTTP request failed: ") + curl_easy_strerror(result));
    }

    return HttpResponse{responseCode, responseBody};
}

HttpResponse HttpClient::PostStream(
    const HttpRequest& request,
    const std::function<void(const std::string&)>& onChunk) const {
    std::string responseBody;
    StreamWriteContext context{&responseBody, &onChunk};
    HttpResponse response = PerformPost(request, StreamWriteCallback, &context);
    response.body = std::move(responseBody);
    return response;
}

}  // namespace voice_agent