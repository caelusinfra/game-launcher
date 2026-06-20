#pragma once
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include "registry.hxx"

using ProgressCallback = std::function<void(int, const std::wstring&)>;

struct Manifest {
    std::wstring bin;
    std::vector<std::wstring> files;
};

Manifest FetchManifest(const std::string& cdn_url, const std::string& channel, const std::string& version);

std::wstring DownloadVersionedFile(const std::string& cdn_url, const std::string& channel, const std::string& version, const std::wstring& name, RegistryManager& registry_mgr, ProgressCallback cb, const std::atomic<bool>& cancelled);

void Extract(const std::wstring& zip_path, const std::wstring& dest_dir);

void DeployComponents(const std::string& cdn_url, const std::string& channel, const std::string& version, const std::vector<std::wstring>& files, const std::wstring& install_dir, RegistryManager& registry_mgr, ProgressCallback cb, const std::atomic<bool>& cancelled);
