#include "rpc.hxx"
#include "http_client.hxx"

#include <windows.h>
#include <string>
#include <cstdio>

static constexpr const char* kAppId = "1509100097936429116";

namespace {

static std::string JsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

static std::string JsonGetString(const std::string& json, const std::string& key)
{
    std::string k = "\"" + key + "\"";
    auto pos = json.find(k);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + k.size());
    if (pos == std::string::npos) return {};
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return {};
    std::string result;
    for (++pos; pos < json.size() && json[pos] != '"'; ++pos) {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            ++pos;
            switch (json[pos]) {
            case '"': result += '"'; break;
            case '\\': result += '\\'; break;
            case 'n': result += '\n'; break;
            case 'r': result += '\r'; break;
            case 't': result += '\t'; break;
            default: result += json[pos]; break;
            }
        } else {
            result += json[pos];
        }
    }
    return result;
}

static long long JsonGetInt(const std::string& json, const std::string& key)
{
    std::string k = "\"" + key + "\":";
    auto pos = json.find(k);
    if (pos == std::string::npos) return 0;
    pos += k.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    try { return std::stoll(json.substr(pos)); } catch (...) { return 0; }
}

struct GameInfo {
    std::string name;
    std::string builder;
    std::string icon;
};

static GameInfo FetchGameInfo(long long place_id)
{
    GameInfo info;
    std::string etag, body;

    char url[256];
    snprintf(url, sizeof(url), "https://games.caelus.lol/v1/games/multiget-place-details?placeIds=%lld", place_id);
    if (HttpGet(url, {}, body, etag) != 200) return info;

    info.name = JsonGetString(body, "name");
    info.builder = JsonGetString(body, "builder");
    long long universe_id = JsonGetInt(body, "universeId");

    if (universe_id <= 0) return info;

    etag.clear(); body.clear();
    snprintf(url, sizeof(url), "https://thumbnails.caelus.lol/v1/games/icons?size=150x150&format=png&universeIds=%lld", universe_id);
    if (HttpGet(url, {}, body, etag) != 200) return info;

    info.icon = JsonGetString(body, "imageUrl");
    return info;
}

struct RpcFrame {
    uint32_t op;
    uint32_t len;
};

static bool PipeWrite(HANDLE pipe, uint32_t op, const std::string& json)
{
    RpcFrame hdr{ op, (uint32_t)json.size() };
    DWORD written;
    if (!WriteFile(pipe, &hdr, sizeof(hdr), &written, nullptr)) return false;
    if (!WriteFile(pipe, json.c_str(), (DWORD)json.size(), &written, nullptr)) return false;
    return true;
}

static bool PipeRead(HANDLE pipe)
{
    RpcFrame hdr{};
    DWORD read;
    if (!ReadFile(pipe, &hdr, sizeof(hdr), &read, nullptr) || read < sizeof(hdr)) return false;
    if (hdr.len > 0) {
        std::string buf(hdr.len, '\0');
        if (!ReadFile(pipe, buf.data(), hdr.len, &read, nullptr)) return false;
    }
    return true;
}

static HANDLE ConnectPipe()
{
    for (int i = 0; i < 10; ++i) {
        wchar_t path[64];
        swprintf_s(path, L"\\\\.\\pipe\\discord-ipc-%d", i);
        HANDLE h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) return h;
    }
    return INVALID_HANDLE_VALUE;
}

}

static HANDLE g_stop_event = nullptr;
static HANDLE g_thread = nullptr;

static DWORD WINAPI RpcThreadProc(LPVOID arg)
{
    long long place_id = (long long)(uintptr_t)arg;
    bool is_studio = (place_id == 0);

    GameInfo info;
    if (!is_studio)
        info = FetchGameInfo(place_id);

    HANDLE pipe = ConnectPipe();
    if (pipe == INVALID_HANDLE_VALUE) return 0;

    char handshake[128];
    snprintf(handshake, sizeof(handshake), "{\"v\":1,\"client_id\":\"%s\"}", kAppId);
    if (!PipeWrite(pipe, 0, handshake) || !PipeRead(pipe)) {
        CloseHandle(pipe);
        return 0;
    }

    std::string act = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":";
    act += std::to_string(GetCurrentProcessId());
    act += ",\"activity\":{";

    if (is_studio) {
        act += "\"details\":\"In Studio\"";
    } else {
        char game[128];
        snprintf(game, sizeof(game), "https://www.caelus.lol/games/%lld/--", place_id);
        if (!info.name.empty())
            act += "\"details\":\"" + JsonEscape(info.name) + "\",";
        if (!info.builder.empty())
            act += "\"state\":\"By " + JsonEscape(info.builder) + "\",";
        if (!info.icon.empty()) {
            act += "\"assets\":{\"large_image\":\"" + JsonEscape(info.icon) + "\"";
            if (!info.name.empty())
                act += ",\"large_text\":\"" + JsonEscape(info.name) + "\"";
            act += "},";
        }
        act += "\"buttons\":[{\"label\":\"Game Page\",\"url\":\"";
        act += game;
        act += "\"}]";
    }

    act += "}},\"nonce\":\"1\"}";

    PipeWrite(pipe, 1, act);
    PipeRead(pipe);
    WaitForSingleObject(g_stop_event, INFINITE);

    CloseHandle(pipe);
    return 0;
}

namespace Rpc {

    void Start(long long place_id)
    {
        if (g_thread) return;
        g_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        g_thread = CreateThread(nullptr, 0, RpcThreadProc, (LPVOID)(uintptr_t)place_id, 0, nullptr);
    }

    void Stop()
    {
        if (g_stop_event) SetEvent(g_stop_event);
        if (g_thread) {
            if (WaitForSingleObject(g_thread, 5000) == WAIT_TIMEOUT)
                TerminateThread(g_thread, 0);
            CloseHandle(g_thread);
            g_thread = nullptr;
        }
        if (g_stop_event) {
            CloseHandle(g_stop_event);
            g_stop_event = nullptr;
        }
    }
}