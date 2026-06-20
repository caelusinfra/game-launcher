#include "http_client.hxx"
#include <curl/curl.h>
#include <fstream>

void HttpGlobalInit() { curl_global_init(CURL_GLOBAL_DEFAULT); }
void HttpGlobalCleanup() { curl_global_cleanup(); }

static size_t WriteToString(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* s = static_cast<std::string*>(userdata);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

static size_t WriteToFile(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* f = static_cast<std::ofstream*>(userdata);
    f->write(ptr, static_cast<std::streamsize>(size * nmemb));
    return size * nmemb;
}

struct DownloadState {
    std::function<void(int64_t, int64_t)> progress;
    const std::atomic<bool>* cancelled;
};

static int XferCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t)
{
    auto* state = static_cast<DownloadState*>(clientp);
    if (state->cancelled && state->cancelled->load())
        return 1;
    if (state->progress)
        state->progress(static_cast<int64_t>(dlnow), static_cast<int64_t>(dltotal));
    return 0;
}

static size_t HeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata)
{
    auto* etag_out = static_cast<std::string*>(userdata);
    std::string line(buffer, size * nitems);
    const std::string prefix = "etag: ";
    if (line.size() > prefix.size()) {
        std::string low = line.substr(0, prefix.size());
        for (auto& c : low) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        if (low == prefix) {
            std::string val = line.substr(prefix.size());
            while (!val.empty() && (val.back() == '\r' || val.back() == '\n' || val.back() == '"'))
                val.pop_back();
            if (!val.empty() && val.front() == '"')
                val.erase(val.begin());
            *etag_out = val;
        }
    }
    return size * nitems;
}

static void SetOpts(CURL* curl)
{
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Aisaka/Bootstrapper");
}


int HttpGet(const std::string& url, const std::string& etag_in, std::string& body_out, std::string& etag_out)
{
    CURL* curl = curl_easy_init();
    if (!curl) return -1;

    struct curl_slist* headers = nullptr;
    if (!etag_in.empty())
        headers = curl_slist_append(headers, ("If-None-Match: \"" + etag_in + "\"").c_str());

    SetOpts(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body_out);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &etag_out);

    CURLcode res = curl_easy_perform(curl);
    long http_code = -1;
    if (res == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return static_cast<int>(http_code);
}

int HttpDownload(const std::string& url, const std::string& etag_in, const std::wstring& local_path, std::string& etag_out, std::function<void(int64_t, int64_t)> progress, const std::atomic<bool>& cancelled)
{
    CURL* curl = curl_easy_init();
    if (!curl) return -1;

    struct curl_slist* headers = nullptr;
    if (!etag_in.empty())
        headers = curl_slist_append(headers, ("If-None-Match: \"" + etag_in + "\"").c_str());

    std::ofstream file(local_path, std::ios::binary);
    if (!file.is_open()) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return -1;
    }

    DownloadState state{ std::move(progress), &cancelled };

    SetOpts(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &etag_out);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, XferCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = -1;
    if (res == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    else if (res == CURLE_ABORTED_BY_CALLBACK)
        http_code = 0;

    file.close();
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return static_cast<int>(http_code);
}