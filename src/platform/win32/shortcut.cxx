#include "shortcut.hxx"
#include <windows.h>
#include <shobjidl.h>

void AddShortcut(const std::wstring& target_exe, const std::wstring& arguments, const std::wstring& name, const std::wstring& dir)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool com_inited = SUCCEEDED(hr);

    IShellLinkW* link = nullptr;
    hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&link));
    if (FAILED(hr)) {
        if (com_inited) CoUninitialize();
        return;
    }

    link->SetPath(target_exe.c_str());
    if (!arguments.empty())
        link->SetArguments(arguments.c_str());
    link->SetIconLocation(target_exe.c_str(), 0);

    IPersistFile* pf = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&pf)))) {
        std::wstring lnk_path = dir + L"\\" + name + L".lnk";
        pf->Save(lnk_path.c_str(), TRUE);
        pf->Release();
    }

    link->Release();
    if (com_inited) CoUninitialize();
}
