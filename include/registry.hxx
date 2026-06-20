#pragma once
#include <string>
#include "product_config.hxx"

class RegistryManager {
public:
    explicit RegistryManager(const ProductConfig& cfg);

    std::string ReadVersion() const;
    void WriteVersion(const std::string& version);

    std::wstring ReadBin() const;
    void WriteBin(const std::wstring& bin);

    std::string ReadETag(const std::wstring& filename) const;
    void WriteETag(const std::wstring& filename, const std::string& etag);

    void RegisterProtocol(const std::wstring& bootstrapper);

    void RegisterUninstall(const std::wstring& bootstrapper);

    void DeleteKeys();

private:
    std::wstring reg_root_;
    std::wstring etag_root_;
    std::wstring scheme_;
};
