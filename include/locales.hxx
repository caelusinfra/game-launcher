#pragma once
#include <string>
#include <unordered_map>
#include <map>

class LocaleManager {
public:
    static LocaleManager& getInstance();

    std::wstring getLocalizedString(const std::string& key) const;

private:
    LocaleManager();
    ~LocaleManager() = default;

    LocaleManager(const LocaleManager&) = delete;
    LocaleManager& operator=(const LocaleManager&) = delete;

    std::string detectSystemLang() const;
    void initTranslations();
    std::string getCurrentLang() const;

    std::string currentLang;
    std::map<std::string, std::unordered_map<std::string, std::string>> allTranslationsUtf8;
};
