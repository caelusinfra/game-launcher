#pragma once
#include <string>

struct ProductConfig {
    std::string product;
    std::string version;
    std::string type;
    std::string reg_root;
    std::wstring protocol;
    std::wstring name;
};

ProductConfig GetProductConfig(const wchar_t* lpCmdLine);
ProductConfig GetBootstrapperConfig();
