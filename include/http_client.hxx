#pragma once
#include <string>
#include <functional>
#include <atomic>

void HttpGlobalInit();
void HttpGlobalCleanup();

int HttpGet(const std::string& url, const std::string& etag_in, std::string& body_out, std::string& etag_out);
int HttpDownload(const std::string& url, const std::string& etag_in, const std::wstring& local_path, std::string& etag_out, std::function<void(int64_t, int64_t)> progress, const std::atomic<bool>& cancelled);
