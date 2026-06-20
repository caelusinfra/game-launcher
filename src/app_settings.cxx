#include "app_settings.hxx"
#include <fstream>
#include <stdexcept>

void Configure(const std::wstring& install_dir, const std::string& base_url)
{
    std::wstring path = install_dir + L"\\AppSettings.xml";
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f.is_open())
        throw std::runtime_error("Failed to write AppSettings.xml");

    f << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<Settings>\n"
      << "\t<ContentFolder>content</ContentFolder>\n"
      << "\t<BaseUrl>" << base_url << "</BaseUrl>\n"
      << "</Settings>\n";
}
