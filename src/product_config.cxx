#include "product_config.hxx"

ProductConfig GetProductConfig(const wchar_t* lpCmdLine)
{
    bool studio = (lpCmdLine && wcsstr(lpCmdLine, L"--studio") != nullptr);

    if (studio) {
        return ProductConfig{
            .product = "aisaka-studio",
            .version = "versionStudio",
            .type = "WindowsStudio",
            .reg_root = "SOFTWARE\\Aisaka Unincorporation\\Studio",
            .protocol = L"aisaka-studio",
            .name = L"Aisaka Studio"
        };
    }

    return ProductConfig{
        .product = "aisaka-player",
        .version = "versionPlayer",
        .type = "WindowsPlayer",
        .reg_root = "SOFTWARE\\Aisaka Unincorporation\\Player",
        .protocol = L"caelus-launcher",
        .name = L"Aisaka Player"
    };
}

ProductConfig GetBootstrapperConfig()
{
    return ProductConfig{
        .product = "aisaka-launcher",
        .version = "versionBootstrapper",
        .type = "WindowsBootstrapper",
        .reg_root = "SOFTWARE\\Aisaka Unincorporation\\Bootstrapper",
        .protocol = L"",
        .name = L"Aisaka Launcher"
    };
}
