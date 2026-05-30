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
    const CancellationToken* cancellationToken = nullptr;
};

size_t StreamWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    const auto bytes = size * nmemb;
    auto* context = static_cast<StreamWriteContext*>(userdata);
    if (context->cancellationToken != nullptr && context->cancellationToken->IsCancelled()) {
        return 0;  // signals curl to abort
    }
    const std::string chunk(ptr, bytes);
    context->output->append(chunk);
    (*context->onChunk)(chunk);
    return bytes;
}

int XferProgressCallback(void* clientp, curl_off_t /*dltotal*/, curl_off_t /*dlnow*/,
                         curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    const auto* token = static_cast<const CancellationToken*>(clientp);
    if (token != nullptr && token->IsCancelled()) {
        return 1;  // non-zero aborts
    }
    return 0;
}

void ApplyCancellation(CURL* curl, const CancellationToken* token) {
    if (token == nullptr) {
        return;
    }
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, XferProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, const_cast<CancellationToken*>(token));
}

bool IsCancelResult(CURLcode code, const CancellationToken* token) {
    if (token != nullptr && token->IsCancelled()) {
        return true;
    }
    return code == CURLE_ABORTED_BY_CALLBACK || code == CURLE_WRITE_ERROR;
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
    ApplyCancellation(curl, request.cancellationToken);

    const CURLcode result = curl_easy_perform(curl);
    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);

    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        if (IsCancelResult(result, request.cancellationToken)) {
            return HttpResponse{responseCode, {}, true};
        }
        throw std::runtime_error(std::string("HTTP request failed: ") + curl_easy_strerror(result));
    }

    return HttpResponse{responseCode, {}, false};
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
    ApplyCancellation(curl, request.cancellationToken);

    const CURLcode result = curl_easy_perform(curl);
    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);

    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        if (IsCancelResult(result, request.cancellationToken)) {
            return HttpResponse{responseCode, {}, true};
        }
        throw std::runtime_error(std::string("HTTP request failed: ") + curl_easy_strerror(result));
    }

    return HttpResponse{responseCode, {}, false};
}

}  // namespace

HttpResponse HttpClient::Get(const HttpRequest& request) const {
    std::string responseBody;
    HttpResponse response = PerformGet(request, WriteCallback, &responseBody);
    response.body = std::move(responseBody);
    return response;
}

HttpResponse HttpClient::Post(const HttpRequest& request) const {
    std::string responseBody;
    HttpResponse response = PerformPost(request, WriteCallback, &responseBody);
    response.body = std::move(responseBody);
    return response;
}

HttpResponse HttpClient::PostStream(
    const HttpRequest& request,
    const std::function<void(const std::string&)>& onChunk) const {
    std::string responseBody;
    StreamWriteContext context{&responseBody, &onChunk, request.cancellationToken};
    HttpResponse response = PerformPost(request, StreamWriteCallback, &context);
    response.body = std::move(responseBody);
    return response;
}

HttpResponse HttpClient::PostMultipart(
    const std::string& url,
    const std::vector<std::string>& headers,
    const std::vector<MultipartField>& fields,
    const MultipartFile& file,
    long timeoutSeconds,
    const CancellationToken* cancellationToken) const {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        throw std::runtime_error("curl_easy_init failed.");
    }

    std::string responseBody;
    struct curl_slist* headerList = nullptr;
    for (const auto& header : headers) {
        headerList = curl_slist_append(headerList, header.c_str());
    }

    curl_mime* mime = curl_mime_init(curl);
    if (mime == nullptr) {
        curl_slist_free_all(headerList);
        curl_easy_cleanup(curl);
        throw std::runtime_error("curl_mime_init failed.");
    }

    for (const auto& field : fields) {
        curl_mimepart* part = curl_mime_addpart(mime);
        curl_mime_name(part, field.name.c_str());
        curl_mime_data(part, field.value.c_str(), CURL_ZERO_TERMINATED);
    }

    curl_mimepart* filePart = curl_mime_addpart(mime);
    curl_mime_name(filePart, file.fieldName.c_str());
    curl_mime_filedata(filePart, file.filePath.c_str());
    if (!file.contentType.empty()) {
        curl_mime_type(filePart, file.contentType.c_str());
    }
    if (!file.fileName.empty()) {
        curl_mime_filename(filePart, file.fileName.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
    ApplyCancellation(curl, cancellationToken);

    const CURLcode result = curl_easy_perform(curl);
    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);

    curl_mime_free(mime);
    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        if (IsCancelResult(result, cancellationToken)) {
            return HttpResponse{responseCode, {}, true};
        }
        throw std::runtime_error(std::string("HTTP request failed: ") + curl_easy_strerror(result));
    }

    return HttpResponse{responseCode, responseBody, false};
}

}  // namespace voice_agent