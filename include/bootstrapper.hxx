#pragma once
#include <string>
#include <functional>
#include <atomic>
#include "product_config.hxx"

struct PlayArgs {
    std::string place_launcher_url;
    std::string auth_url;
    std::string auth_ticket;
};

struct BootstrapStatus {
    std::wstring message;
    int percent;
    bool success;
    std::wstring error;
    bool close_dialog;
    bool show_cancel;
    bool tray_mode;
};

using StatusCallback = std::function<void(BootstrapStatus)>;

std::string FetchVersion(const std::string& cdn_url, const std::string& product);

void RunBootstrap(const ProductConfig& cfg, const std::string& cdn_url, const std::string& base_url, const std::wstring& install_dir, const std::wstring& bootstrapper, const PlayArgs* play_args, StatusCallback status_cb, const std::atomic<bool>& cancelled);
