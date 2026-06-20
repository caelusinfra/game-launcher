#include "locales.hxx"
#include "str_convert.hxx"

// TODO: tray item translations, error messages and other dialogs

static const std::map<std::string, std::map<std::string, std::string>> TRANSLATIONS = {
    {
        "en-US", {
            {"configuring", "Configuring Aisaka..."},
            {"file_check", "Performing file check..."},
            {"please_wait", "Please wait..."},
            {"connecting", "Connecting to Aisaka..."},
            {"button_ok", "OK"},
            {"button_cancel", "Cancel"},
            {"getting_latest", "Getting the latest Aisaka..."},
            {"starting", "Aisaka is up-to-date"},
            {"install_success", "Aisaka has successfully been installed"},
            {"upgrading", "Upgrading Aisaka..."},
            {"installing", "Installing Aisaka..."},
        }
    },
    {
        "ru-RU", {
            {"configuring", "Настройка Aisaka..."},
            {"file_check", "Проверка файлов..."},
            {"please_wait", "Пожалуйста, подождите..."},
            {"connecting", "Подключение к Aisaka..."},
            {"button_ok", "ОК"},
            {"button_cancel", "Отмена"},
            {"getting_latest", "Загрузка последней версии Aisaka..."},
            {"starting", "Aisaka обновлен"},
            {"install_success", "Aisaka успешно установлена"},
            {"upgrading", "Обновление Aisaka..."},
            {"installing", "Установка Aisaka..."},
        }
    },
    {
        "ro-RO", {
            {"configuring", "Se configurează Aisaka..."},
            {"file_check", "Se verifică fișierele..."},
            {"please_wait", "Vă rugăm să așteptați..."},
            {"connecting", "Se conectează la Aisaka..."},
            {"button_ok", "OK"},
            {"button_cancel", "Anulare"},
            {"getting_latest", "Se descarcă cea mai recentă versiune Aisaka..."},
            {"starting", "Aisaka este actualizat"},
            {"install_success", "Aisaka a fost instalat cu succes"},
            {"upgrading", "Se actualizează Aisaka..."},
            {"installing", "Se instalează Aisaka..."},
        }
    },
    {
        "de-DE", {
            {"configuring", "Aisaka wird konfiguriert..."},
            {"file_check", "Dateien werden überprüft..."},
            {"please_wait", "Bitte warten..."},
            {"connecting", "Verbindung zu Aisaka wird hergestellt..."},
            {"button_ok", "OK"},
            {"button_cancel", "Abbrechen"},
            {"getting_latest", "Neueste Version von Aisaka wird abgerufen..."},
            {"starting", "Aisaka ist auf dem neuesten Stand"},
            {"install_success", "Aisaka wurde erfolgreich installiert"},
            {"upgrading", "Aisaka wird aktualisiert..."},
            {"installing", "Aisaka wird installiert..."},
        }
    },
     {
        "pl-PL", {
            {"configuring", "Konfigurowanie Aisaka..."},
            {"file_check", "Sprawdzanie Plików..."},
            {"please_wait", "Prosze poczekać..."},
            {"connecting", "Łączenie z Aisaka..."},
            {"button_ok", "OK"},
            {"button_cancel", "Anuluj"},
            {"getting_latest", "Zdobywanie najnowszej wersji Aisaka..."},
            {"starting", "Aisaka jest w najnowszej wersji"},
            {"install_success", "Aisaka została pomyślnie zainstalowana"},
            {"upgrading", "Aktualizacja Aisaka..."},
            {"installing", "Instalowanie Aisaka..."},
        }
    }
};

LocaleManager& LocaleManager::getInstance() {
    static LocaleManager instance;
    return instance;
}

LocaleManager::LocaleManager() {
    currentLang = detectSystemLang();
    initTranslations();
}

std::wstring LocaleManager::getLocalizedString(const std::string& key) const {
    auto langIt = allTranslationsUtf8.find(currentLang);
    if (langIt != allTranslationsUtf8.end()) {
        auto keyIt = langIt->second.find(key);
        if (keyIt != langIt->second.end())
            return Utf8ToWide(keyIt->second);
    }

    auto enIt = allTranslationsUtf8.find("en-US");
    if (enIt != allTranslationsUtf8.end()) {
        auto keyIt = enIt->second.find(key);
        if (keyIt != enIt->second.end())
            return Utf8ToWide(keyIt->second);
    }

    return Utf8ToWide(key);
}

std::string LocaleManager::detectSystemLang() const {
    wchar_t localeName[LOCALE_NAME_MAX_LENGTH];
    if (GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH)) {
        std::wstring wstr(localeName);
        return std::string(wstr.begin(), wstr.end());
    }
    return "en-US";
}

void LocaleManager::initTranslations() {
    for (const auto& [language, translations] : TRANSLATIONS) {
        std::unordered_map<std::string, std::string> transMap;
        for (const auto& [key, value] : translations) {
            transMap[key] = value;
        }
        allTranslationsUtf8[language] = transMap;
    }
}

std::string LocaleManager::getCurrentLang() const {
    return currentLang;
}
