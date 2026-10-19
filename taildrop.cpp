#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define _UNICODE
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <shellapi.h>
#include <windowsx.h>
#include <winsvc.h>
#include <tlhelp32.h>
#include <dwmapi.h>
#include <commdlg.h>
#include <objbase.h>
#include <wininet.h>
#include <atomic>
#include <deque>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <stdexcept>
#include <vector>
#include <condition_variable>
#include <functional>
#include <fstream>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <cmath>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "wininet.lib")

constexpr int kPort = 45779;
constexpr size_t kChunk = 256 * 1024;
constexpr int kStreams = 4;
constexpr long long kSplitMin = 8LL * 1024 * 1024;
constexpr UINT kWmState = WM_APP + 1;
constexpr UINT kTrayMsg = WM_APP + 2;
constexpr UINT kTimerRefresh = 1, kTimerRevert = 2, kTimerAnimate = 3;
constexpr UINT kCmdOpen = 1001, kCmdQuit = 1002;
constexpr int kIconRes = 1;
constexpr double kPi = 3.14159265358979323846;
constexpr const char* kAppVersion = "1";
constexpr const wchar_t* kUpdateApi =
    L"https://api.github.com/repos/k7m2q9x4b1z8p6nd/tailscaleptp/releases/latest";

constexpr COLORREF kBg = RGB(0x0A, 0x0A, 0x0A);
constexpr COLORREF kCard = RGB(0x10, 0x10, 0x10);
constexpr COLORREF kCardHi = RGB(0x14, 0x14, 0x14);
constexpr COLORREF kTrack = RGB(0x22, 0x22, 0x22);
constexpr COLORREF kLine = RGB(0x26, 0x26, 0x26);
constexpr COLORREF kLineHi = RGB(0x4A, 0x4A, 0x4A);
constexpr COLORREF kFg = RGB(0xED, 0xED, 0xED);
constexpr COLORREF kDim = RGB(0x8A, 0x8A, 0x8A);
constexpr COLORREF kMuted = RGB(0x55, 0x55, 0x55);
constexpr wchar_t kFontName[] = L"Segoe UI";

static double g_S = 1.0;
static int sc(double v) { return (int)std::lround(v * g_S); }

struct SHandleDeleter {
    void operator()(HANDLE h) const {
        if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
};
using UniqueHandle = std::unique_ptr<void, SHandleDeleter>;

struct SHandle {
    HANDLE v = nullptr;
    ~SHandle() { if (v) CloseHandle(v); }
    explicit operator bool() const { return v != nullptr; }
};

struct SBrush {
    HBRUSH v;
    explicit SBrush(COLORREF c) : v(CreateSolidBrush(c)) {}
    ~SBrush() { DeleteObject(v); }
    operator HBRUSH() const { return v; }
};

struct SPen {
    HPEN v;
    SPen(int style, int width, COLORREF c) : v(CreatePen(style, width, c)) {}
    ~SPen() { DeleteObject(v); }
    operator HPEN() const { return v; }
};

struct SSocket {
    SOCKET v;
    explicit SSocket(SOCKET s) : v(s) {}
    ~SSocket() { if (v != INVALID_SOCKET) closesocket(v); }
};

static std::wstring wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring r(n, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n);
    return r;
}

static std::string narrow(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string r(n, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n, nullptr, nullptr);
    return r;
}

static std::wstring baseNameW(const std::wstring& p) {
    size_t i = p.find_last_of(L"\\/");
    std::wstring n = (i == std::wstring::npos) ? p : p.substr(i + 1);
    if (n.empty() || n == L"." || n == L"..") n = L"file";
    return n;
}

static std::wstring fmtBytes(long long n) {
    double v = (double)n;
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB"};
    for (int i = 0; i < 4; ++i) {
        if (v < 1000.0 || i == 3) {
            wchar_t buf[64];
            if (i == 0) swprintf(buf, 64, L"%lld B", n);
            else swprintf(buf, 64, L"%.1f %s", v, units[i]);
            return buf;
        }
        v /= 1000.0;
    }
    return L"";
}

static std::wstring clip(const std::wstring& s, size_t lim) {
    if (s.size() <= lim) return s;
    return s.substr(0, lim - 1) + L"\x2026";
}

struct Json {
    enum Kind { Null, Bool, Num, Str, Arr, Obj } kind = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Json> arr;
    std::vector<std::string> keys;
    const Json* find(const char* k) const {
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == k) return &arr[i];
        return nullptr;
    }
};

struct JsonReader {
    const char* p;
    bool read(Json& out) {
        try { v(out); ws(); return *p == '\0'; } catch (...) { return false; }
    }
    void ws() { while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p; }
    void v(Json& o) {
        ws();
        char c = *p;
        if (c == '{') {
            ++p; o.kind = Json::Obj; ws();
            if (*p == '}') { ++p; return; }
            for (;;) {
                Json key;
                v(key);
                if (key.kind != Json::Str) throw 1;
                ws();
                if (*p != ':') throw 1;
                ++p;
                Json val;
                v(val);
                o.keys.push_back(std::move(key.str));
                o.arr.push_back(std::move(val));
                ws();
                if (*p == ',') { ++p; continue; }
                if (*p == '}') { ++p; return; }
                throw 1;
            }
        }
        if (c == '[') {
            ++p; o.kind = Json::Arr; ws();
            if (*p == ']') { ++p; return; }
            for (;;) {
                Json e;
                v(e);
                o.arr.push_back(std::move(e));
                ws();
                if (*p == ',') { ++p; continue; }
                if (*p == ']') { ++p; return; }
                throw 1;
            }
        }
        if (c == '"') { str(o); return; }
        if (c == 't' || c == 'f') { lit(o); return; }
        if (c == 'n') { if (std::strncmp(p, "null", 4)) throw 1; p += 4; o.kind = Json::Null; return; }
        o.kind = Json::Num;
        char* end = nullptr;
        o.num = strtod(p, &end);
        if (!end || end == p) throw 1;
        p = end;
    }
    static unsigned hex4(const char*& q) {
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = *q++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else throw 1;
        }
        return v;
    }
    static void utf8(std::string& s, unsigned cp) {
        if (cp < 0x80) s += (char)cp;
        else if (cp < 0x800) { s += (char)(0xC0 | cp >> 6); s += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { s += (char)(0xE0 | cp >> 12); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
        else { s += (char)(0xF0 | cp >> 18); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    }
    void str(Json& o) {
        ++p;
        o.kind = Json::Str;
        for (;;) {
            char c = *p++;
            if (c == '"') return;
            if (c == '\0') throw 1;
            if (c != '\\') { o.str += c; continue; }
            c = *p++;
            switch (c) {
                case '"': case '\\': case '/': o.str += c; break;
                case 'b': o.str += '\b'; break;
                case 'f': o.str += '\f'; break;
                case 'n': o.str += '\n'; break;
                case 'r': o.str += '\r'; break;
                case 't': o.str += '\t'; break;
                case 'u': {
                    unsigned a = hex4(p);
                    if (a >= 0xD800 && a <= 0xDBFF && p[0] == '\\' && p[1] == 'u') {
                        p += 2;
                        unsigned bcp = hex4(p);
                        a = 0x10000 + ((a - 0xD800) << 10) + (bcp - 0xDC00);
                    }
                    utf8(o.str, a);
                    break;
                }
                default: throw 1;
            }
        }
    }
    void lit(Json& o) {
        if (*p == 't') {
            if (std::strncmp(p, "true", 4)) throw 1;
            p += 4; o.kind = Json::Bool; o.b = true;
        } else {
            if (std::strncmp(p, "false", 5)) throw 1;
            p += 5; o.kind = Json::Bool;
        }
    }
};

static const Json* field(const Json& j, const char* k) { return j.kind == Json::Obj ? j.find(k) : nullptr; }

static std::string stringField(const Json& j, const char* k) {
    const Json* f = field(j, k);
    return (f && f->kind == Json::Str) ? f->str : "";
}

static long long numField(const Json& j, const char* k, long long def) {
    const Json* f = field(j, k);
    return (f && f->kind == Json::Num) ? (long long)f->num : def;
}

struct Peer {
    std::wstring name;
    std::string ip;
    bool operator==(const Peer& o) const { return name == o.name && ip == o.ip; }
};

enum Mode { Idle, Active, Done, Error };

struct Ui {
    Mode mode = Idle;
    double frac = 0, target = 0;
    std::wstring title, sub, detail, message;
    std::vector<Peer> peers;
    std::string selected;
    int holdMs = 0;
    unsigned resultSerial = 0;
    int batchTotal = 0, batchDone = 0;
    std::wstring currentKey;
};

static std::mutex g_uiMtx;
static Ui g_ui;
static std::atomic<HWND> g_window{nullptr};
static std::atomic<bool> g_repaint{false};

static void notify() {
    if (g_repaint.exchange(true)) return;
    HWND h = g_window.load();
    if (h) PostMessageW(h, kWmState, 0, 0);
}

static std::wstring downloadDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    std::wstring dir = n ? std::wstring(buf) : L".";
    return dir + L"\\Downloads";
}

static std::string captureOutput(const wchar_t* cmdLine) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) throw std::runtime_error("pipe");
    UniqueHandle rdp(rd);
    UniqueHandle wrp(wr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    PROCESS_INFORMATION pi{};
    std::wstring mut(cmdLine);
    if (!CreateProcessW(nullptr, mut.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        throw std::runtime_error("spawn");
    CloseHandle(pi.hThread);
    UniqueHandle proc(pi.hProcess);
    wrp.reset();
    std::string out;
    char buf[8192];
    DWORD n;
    while (ReadFile(rd, buf, sizeof(buf), &n, nullptr) && n) out.append(buf, n);
    WaitForSingleObject(pi.hProcess, 30000);
    return out;
}

static Json tailscaleStatus() {
    Json d;
    try {
        std::string out = captureOutput(L"tailscale.exe status --json");
        JsonReader r{out.c_str()};
        r.read(d);
    } catch (...) {}
    return d;
}

static bool tailscaleRunning(const Json& d) {
    return d.kind == Json::Obj && stringField(d, "BackendState") == "Running";
}

static std::wstring statusMessage(const Json& d) {
    if (d.kind != Json::Obj) return L"Tailscale is off";
    if (!tailscaleRunning(d)) return L"Tailscale not connected";
    return L"No devices online";
}

static std::string tailnetIp(const Json* node) {
    if (node && node->kind == Json::Obj) {
        const Json* ips = field(*node, "TailscaleIPs");
        if (ips && ips->kind == Json::Arr) {
            for (const Json& s : ips->arr)
                if (s.kind == Json::Str && s.str.rfind("100.", 0) == 0) return s.str;
        }
    }
    return "";
}

static std::vector<Peer> parsePeers(const Json& d) {
    std::vector<Peer> out;
    if (d.kind != Json::Obj) return out;
    std::string self = tailnetIp(field(d, "Self"));
    const Json* peers = field(d, "Peer");
    if (!peers || peers->kind != Json::Obj) return out;
    for (size_t i = 0; i < peers->arr.size(); ++i) {
        const Json& p = peers->arr[i];
        const Json* on = field(p, "Online");
        if (!(on && on->kind == Json::Bool && on->b)) continue;
        std::string name = stringField(p, "HostName");
        if (name.empty()) {
            std::string dn = stringField(p, "DNSName");
            name = dn.substr(0, dn.find('.'));
        }
        std::string ip = tailnetIp(&p);
        if (ip.empty() || ip == self) continue;
        std::wstring wname = wide(name);
        if (wname.empty()) wname = wide(ip);
        if (!_wcsicmp(wname.c_str(), L"localhost")) continue;
        out.push_back({wname, ip});
    }
    return out;
}

static bool tailscaleAppRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    UniqueHandle s(snap);
    PROCESSENTRY32W e{sizeof(e)};
    for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e))
        if (!_wcsicmp(e.szExeFile, L"tailscale-ipn.exe")) return true;
    return false;
}

static void openTailscaleApp() {
    if (tailscaleAppRunning()) return;
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"ProgramFiles", buf, MAX_PATH);
    std::wstring exe = (n ? std::wstring(buf) : L"C:\\Program Files") + L"\\Tailscale\\tailscale-ipn.exe";
    if (GetFileAttributesW(exe.c_str()) != INVALID_FILE_ATTRIBUTES)
        ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, nullptr, SW_SHOW);
}

static bool tailscaleServiceRunning() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, L"Tailscale", SERVICE_QUERY_STATUS);
    if (svc) {
        SERVICE_STATUS s{};
        QueryServiceStatus(svc, &s);
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        return s.dwCurrentState == SERVICE_RUNNING;
    }
    CloseServiceHandle(scm);
    return false;
}

static void startTailscaleService() {
    if (tailscaleServiceRunning()) return;
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring sc = std::wstring(sys) + L"\\sc.exe";
    SHELLEXECUTEINFOW sh{};
    sh.cbSize = sizeof(sh);
    sh.fMask = SEE_MASK_NOCLOSEPROCESS;
    sh.lpVerb = L"runas";
    sh.lpFile = sc.c_str();
    sh.lpParameters = L"start Tailscale";
    sh.nShow = SW_HIDE;
    if (ShellExecuteExW(&sh) && sh.hProcess) {
        WaitForSingleObject(sh.hProcess, 60000);
        CloseHandle(sh.hProcess);
    }
}

static void refreshPeers() {
    Json d = tailscaleStatus();
    auto peers = parsePeers(d);
    std::wstring msg = statusMessage(d);
    {
        std::lock_guard<std::mutex> lk(g_uiMtx);
        if (peers == g_ui.peers && msg == g_ui.message) return;
        g_ui.peers = std::move(peers);
        g_ui.message = std::move(msg);
        bool found = false;
        for (auto& p : g_ui.peers)
            if (p.ip == g_ui.selected) found = true;
        if (!found) g_ui.selected = g_ui.peers.empty() ? "" : g_ui.peers[0].ip;
    }
    notify();
}

static void refreshPeersAsync() { std::thread(refreshPeers).detach(); }

struct SInet {
    HINTERNET v;
    ~SInet() { if (v) InternetCloseHandle(v); }
};

static HINTERNET httpOpen(const wchar_t* url) {
    HINTERNET inet = InternetOpenW(L"Taildrop", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!inet) throw std::runtime_error("inet");
    HINTERNET h = InternetOpenUrlW(inet, url, nullptr, 0,
                                   INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                                       INTERNET_FLAG_NO_COOKIES | INTERNET_FLAG_SECURE,
                                   0);
    InternetCloseHandle(inet);
    if (!h) throw std::runtime_error("openurl");
    return h;
}

static std::string httpGet(const wchar_t* url) {
    SInet h{httpOpen(url)};
    std::string out;
    char buf[65536];
    DWORD n;
    while (InternetReadFile(h.v, buf, sizeof(buf), &n) && n) out.append(buf, n);
    return out;
}

static void httpDownload(const wchar_t* url, const std::wstring& path) {
    SInet h{httpOpen(url)};
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!f || f == INVALID_HANDLE_VALUE) throw std::runtime_error("create");
    UniqueHandle fh(f);
    char buf[65536];
    DWORD n;
    while (InternetReadFile(h.v, buf, sizeof(buf), &n) && n) {
        DWORD w;
        if (!WriteFile(f, buf, n, &w, nullptr) || w != n) throw std::runtime_error("write");
    }
    fh.reset();
    if (GetFileSize(f, nullptr) == 0 && GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        throw std::runtime_error("empty");
}

static std::vector<int> verParts(std::string s) {
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
    std::vector<int> v;
    size_t i = 0;
    while (i < s.size()) {
        int n = 0;
        bool any = false;
        while (i < s.size() && isdigit((unsigned char)s[i])) { n = n * 10 + s[i] - '0'; ++i; any = true; }
        if (any) v.push_back(n);
        ++i;
    }
    return v;
}

static bool versionNewer(const std::string& tag) {
    auto a = verParts(tag), b = verParts(kAppVersion);
    a.resize(std::max(a.size(), b.size()));
    b.resize(std::max(a.size(), b.size()));
    return a > b;
}

static void checkUpdate() {
    try {
        std::string body = httpGet(kUpdateApi);
        Json j;
        JsonReader r{body.c_str()};
        if (!r.read(j)) return;
        std::string tag = stringField(j, "tag_name");
        if (!versionNewer(tag)) return;
        const Json* assets = field(j, "assets");
        if (!assets || assets->kind != Json::Arr || assets->arr.empty()) return;
        std::string url = stringField(assets->arr[0], "browser_download_url");
        if (url.empty()) return;
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        std::wstring dl = std::wstring(tmp) + L"taildrop_update.exe";
        httpDownload(wide(url).c_str(), dl);
        wchar_t exeBuf[MAX_PATH];
        GetModuleFileNameW(nullptr, exeBuf, MAX_PATH);
        std::wstring exe = exeBuf;
        std::wstring bat = std::wstring(tmp) + L"taildrop_upd.bat";
        {
            std::ofstream f(bat);
            f << "@echo off\r\n"
              << "taskkill /PID " << GetCurrentProcessId() << " /F >nul 2>&1\r\n"
              << "ping -n 2 127.0.0.1 >nul\r\n"
              << "del /f \"" << narrow(exe) << "\" >nul 2>&1\r\n"
              << ":mv\r\n"
              << "move /y \"" << narrow(dl) << "\" \"" << narrow(exe) << "\" >nul 2>&1\r\n"
              << "if exist \"" << narrow(dl) << "\" (ping -n 2 127.0.0.1 >nul & goto mv)\r\n"
              << "start \"\" \"" << narrow(exe) << "\"\r\n"
              << "del \"%~f0\"\r\n";
        }
        std::wstring args = L"/c \"" + bat + L"\"";
        ShellExecuteW(nullptr, L"open", L"cmd.exe", args.c_str(), nullptr, SW_HIDE);
        ExitProcess(0);
    } catch (...) {}
}

static void ensureTailscale() {
    openTailscaleApp();
    startTailscaleService();
    refreshPeers();
}

static void sendAll(SOCKET s, const char* data, size_t n) {
    while (n) {
        int r = send(s, data, (int)n, 0);
        if (r <= 0) throw std::runtime_error("send failed");
        data += r; n -= (size_t)r;
    }
}

static void readAll(SOCKET s, char* data, size_t n) {
    while (n) {
        int r = recv(s, data, (int)n, 0);
        if (r <= 0) throw std::runtime_error("Connection closed");
        data += r; n -= (size_t)r;
    }
}

static std::string jsonEscape(const std::string& s) {
    std::string r;
    r.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"': r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\b': r += "\\b"; break;
            case '\f': r += "\\f"; break;
            case '\n': r += "\\n"; break;
            case '\r': r += "\\r"; break;
            case '\t': r += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    sprintf_s(buf, "\\u%04x", (unsigned char)c);
                    r += buf;
                } else r += c;
        }
    }
    return r;
}

static void writeLenPrefixed(SOCKET s, const std::string& payload) {
    char hdr[4] = {(char)(payload.size() >> 24), (char)(payload.size() >> 16),
                   (char)(payload.size() >> 8), (char)payload.size()};
    sendAll(s, hdr, 4);
    sendAll(s, payload.data(), payload.size());
}

static void readAt(HANDLE f, char* buf, DWORD n, long long pos) {
    OVERLAPPED o{};
    o.Offset = (DWORD)pos;
    o.OffsetHigh = (DWORD)(pos >> 32);
    DWORD got = 0;
    if (!ReadFile(f, buf, n, &got, &o) && GetLastError() != ERROR_HANDLE_EOF)
        throw std::runtime_error("read failed");
    if (got != n) throw std::runtime_error("read failed");
}

static void writeAt(HANDLE f, const char* buf, DWORD n, long long pos) {
    OVERLAPPED o{};
    o.Offset = (DWORD)pos;
    o.OffsetHigh = (DWORD)(pos >> 32);
    DWORD done = 0;
    if (!WriteFile(f, buf, n, &done, &o) || done != n) throw std::runtime_error("write failed");
}

static std::wstring createDestination(const std::wstring& name, long long size) {
    std::wstring dir = downloadDir();
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring path = dir + L"\\" + name;
    size_t dot = name.find_last_of(L'.');
    std::wstring base = (dot == std::wstring::npos) ? name : name.substr(0, dot);
    std::wstring ext = (dot == std::wstring::npos) ? L"" : name.substr(dot);
    for (int i = 1;; ++i) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER li;
            li.QuadPart = size;
            SetFilePointerEx(h, li, nullptr, FILE_BEGIN);
            SetEndOfFile(h);
            CloseHandle(h);
            return path;
        }
        if (GetLastError() != ERROR_FILE_EXISTS) throw std::runtime_error("create failed");
        path = dir + L"\\" + base + L" (" + std::to_wstring(i) + L")" + ext;
    }
}

struct Transfer {
    std::wstring path, name;
    long long size;
    long long received = 0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

static std::mutex g_txMtx;
static std::map<std::string, std::shared_ptr<Transfer>> g_transfers;
static std::atomic<long long> g_legacyKey{0};

static std::shared_ptr<Transfer> acquireTransfer(const std::string& key, const std::wstring& name,
                                               long long size) {
    std::lock_guard<std::mutex> lk(g_txMtx);
    auto it = g_transfers.find(key);
    if (it != g_transfers.end()) return it->second;
    auto t = std::make_shared<Transfer>();
    t->path = createDestination(name, size);
    t->name = name;
    t->size = size;
    g_transfers.emplace(key, t);
    return t;
}

static void showResultLocked(Mode m, const std::wstring& title, const std::wstring& sub,
                             const std::wstring& detail, int hold) {
    g_ui.mode = m;
    g_ui.title = title;
    g_ui.sub = sub;
    g_ui.detail = detail;
    g_ui.holdMs = hold;
    ++g_ui.resultSerial;
    if (m == Error) g_ui.batchTotal = g_ui.batchDone = 0;
    g_ui.currentKey.clear();
    notify();
}

static void setResult(Mode m, const std::wstring& t, const std::wstring& s,
                    const std::wstring& d, int hold) {
    std::lock_guard<std::mutex> lk(g_uiMtx);
    showResultLocked(m, t, s, d, hold);
}

static void reportProgress(wchar_t dir, const std::wstring& name, long long done, long long size,
                           double speed) {
    std::lock_guard<std::mutex> lk(g_uiMtx);
    std::wstring key = std::wstring(1, dir) + name;
    if (key != g_ui.currentKey) {
        g_ui.currentKey = key;
        g_ui.frac = g_ui.target = 0;
    }
    g_ui.target = size ? (double)done / size : 0;
    std::wstring detail = fmtBytes(done) + L" of " + fmtBytes(size);
    if (speed) detail += L"  \xB7  " + fmtBytes((long long)speed) + L"/s";
    if (dir == L's' && g_ui.batchTotal > 1)
        detail += L"  \xB7  file " + std::to_wstring(std::min(g_ui.batchDone + 1, g_ui.batchTotal)) +
                  L" of " + std::to_wstring(g_ui.batchTotal);
    g_ui.mode = Active;
    g_ui.title = (dir == L's') ? L"Sending" : L"Receiving";
    g_ui.sub = clip(name, 36);
    g_ui.detail = detail;
    notify();
}

static void receiveRange(SOCKET conn, const std::shared_ptr<Transfer>& t, long long offset,
                         long long length) {
    HANDLE h = CreateFileW(t->path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!h || h == INVALID_HANDLE_VALUE) throw std::runtime_error("open failed");
    UniqueHandle uh(h);
    std::vector<char> buf(kChunk);
    long long got = 0, pos = offset;
    while (got < length) {
        int n = recv(conn, buf.data(), (int)std::min<long long>(kChunk, length - got), 0);
        if (n <= 0) break;
        writeAt(h, buf.data(), (DWORD)n, pos);
        pos += n; got += n;
        long long done;
        {
            std::lock_guard<std::mutex> lk(g_txMtx);
            t->received += n;
            done = t->received;
        }
        double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t->start).count();
        reportProgress(L'r', t->name, done, t->size, el > 0.05 ? done / el : 0);
    }
    if (got != length) throw std::runtime_error("Transfer ended before the full file arrived");
}

static void receiveConnection(SOCKET conn) {
    SSocket cs(conn);
    try {
        char raw[4];
        readAll(conn, raw, 4);
        unsigned hlen = ((unsigned char)raw[0] << 24) | ((unsigned char)raw[1] << 16) |
                        ((unsigned char)raw[2] << 8) | (unsigned char)raw[3];
        if (hlen > 65536) throw std::runtime_error("bad header");
        std::string hb(hlen, '\0');
        readAll(conn, hb.data(), hlen);
        Json meta;
        {
            JsonReader r{hb.c_str()};
            if (!r.read(meta)) throw std::runtime_error("bad header");
        }
        std::wstring name = baseNameW(wide(stringField(meta, "name")));
        long long size = numField(meta, "size", 0);
        std::string key = stringField(meta, "id");
        if (key.empty()) key = "legacy:" + std::to_string(++g_legacyKey);
        long long offset = numField(meta, "offset", 0);
        long long length = numField(meta, "length", size);
        auto t = acquireTransfer(key, name, size);
        receiveRange(conn, t, offset, length);
        bool complete = false;
        {
            std::lock_guard<std::mutex> lk(g_txMtx);
            if (t->received == t->size) complete = g_transfers.erase(key) > 0;
        }
        if (complete) {
            std::lock_guard<std::mutex> lk(g_uiMtx);
            showResultLocked(Done, L"Received", clip(t->name, 36), L"Saved to Downloads", 3200);
        }
    } catch (const std::exception& e) {
        setResult(Error, L"Receive failed", L"", wide(e.what()), 5000);
    }
}

static void serverLoop() {
    SOCKET ls = socket(AF_INET, SOCK_STREAM, 0);
    SSocket lg(ls);
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons(kPort);
    if (bind(ls, (sockaddr*)&a, sizeof(a)) || listen(ls, 4)) {
        setResult(Error, L"Can't receive files", L"",
                  L"Port " + std::to_wstring(kPort) + L" unavailable", 5000);
        return;
    }
    for (;;) {
        SOCKET c = accept(ls, nullptr, nullptr);
        if (c == INVALID_SOCKET) break;
        std::thread(receiveConnection, c).detach();
    }
}

using ProgressFn = std::function<void(int)>;

static void sendRange(const std::string& ip, const std::wstring& path, const std::wstring& name,
                      long long size, const std::string& id, long long offset, long long length,
                      const ProgressFn& progress) {
    std::string meta = "{\"name\":\"" + jsonEscape(narrow(name)) + "\",\"size\":" +
                       std::to_string(size);
    if (!id.empty())
        meta += ",\"id\":\"" + id + "\",\"offset\":" + std::to_string(offset) +
                ",\"length\":" + std::to_string(length);
    meta += "}";
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(kPort);
    if (inet_pton(AF_INET, ip.c_str(), &a.sin_addr) != 1)
        throw std::runtime_error("bad address");
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    SSocket sg(s);
    DWORD ms = 10000;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&ms, sizeof(ms));
    if (connect(s, (sockaddr*)&a, sizeof(a))) {
        int e = WSAGetLastError();
        if (e == WSAETIMEDOUT || e == WSAECONNREFUSED || e == WSAEHOSTUNREACH)
            throw std::runtime_error("Make sure Taildrop is open on that device");
        throw std::runtime_error("connect failed");
    }
    ms = 30000;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&ms, sizeof(ms));
    writeLenPrefixed(s, meta);
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!f || f == INVALID_HANDLE_VALUE) throw std::runtime_error("open failed");
    UniqueHandle fh(f);
    std::vector<char> buf(kChunk);
    long long pos = offset, remain = length;
    while (remain > 0) {
        DWORD n = (DWORD)std::min<long long>(kChunk, remain);
        readAt(f, buf.data(), n, pos);
        sendAll(s, buf.data(), n);
        pos += n; remain -= n;
        progress((int)n);
    }
}

static void sendFile(const std::string& ip, const std::wstring& path) {
    std::wstring name = baseNameW(path);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!h || h == INVALID_HANDLE_VALUE) throw std::runtime_error("open failed");
    LARGE_INTEGER li;
    GetFileSizeEx(h, &li);
    CloseHandle(h);
    long long size = li.QuadPart;
    std::vector<std::pair<long long, long long>> ranges;
    if (size >= kSplitMin) {
        long long step = (size + kStreams - 1) / kStreams;
        for (long long o = 0; o < size; o += step)
            ranges.push_back({o, std::min(step, size - o)});
    } else {
        ranges.push_back({0, size});
    }
    std::string transferId;
    if (ranges.size() > 1) {
        char buf[37];
        GUID g;
        CoCreateGuid(&g);
        sprintf_s(buf, "%08x%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x", g.Data1, g.Data2, g.Data3,
                  g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5],
                  g.Data4[6], g.Data4[7]);
        transferId = buf;
    }
    auto started = std::chrono::steady_clock::now();
    std::mutex pm;
    long long sent = 0;
    std::string err;
    auto progress = [&](int n) {
        std::lock_guard<std::mutex> lk(pm);
        sent += n;
        long long d = sent;
        double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        reportProgress(L's', name, d, size, el > 0.05 ? d / el : 0);
    };
    std::vector<std::thread> ths;
    ths.reserve(ranges.size());
    for (auto& rg : ranges)
        ths.emplace_back([&, rg] {
            try {
                sendRange(ip, path, name, size, transferId, rg.first, rg.second, progress);
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lk(pm);
                if (err.empty()) err = e.what();
            }
        });
    for (auto& t : ths) t.join();
    if (!err.empty()) throw std::runtime_error(err);
}

struct SendJob { std::string ip; std::wstring path; };
static std::mutex g_jobMtx;
static std::condition_variable g_jobCv;
static std::deque<SendJob> g_jobs;

static void queueSend(std::vector<std::wstring>&& paths) {
    if (paths.empty()) return;
    std::string sel;
    {
        std::lock_guard<std::mutex> lk(g_uiMtx);
        sel = g_ui.selected;
    }
    if (sel.empty()) {
        setResult(Error, L"Choose a device first", L"", L"Pick one above, then try again", 3000);
        return;
    }
    {
        std::lock_guard<std::mutex> lk(g_uiMtx);
        if (g_ui.batchDone >= g_ui.batchTotal) g_ui.batchTotal = g_ui.batchDone = 0;
        g_ui.batchTotal += (int)paths.size();
        if (g_ui.mode != Active) {
            g_ui.currentKey.clear();
            g_ui.target = 0;
            g_ui.mode = Active;
            g_ui.title = L"Connecting\x2026";
            g_ui.sub.clear();
            g_ui.detail.clear();
        }
    }
    notify();
    {
        std::lock_guard<std::mutex> lk(g_jobMtx);
        for (auto& p : paths) g_jobs.push_back({sel, p});
    }
    g_jobCv.notify_all();
}

static void markSent(const std::wstring& name) {
    std::lock_guard<std::mutex> lk(g_uiMtx);
    ++g_ui.batchDone;
    if (g_ui.batchDone < g_ui.batchTotal) return;
    int count = g_ui.batchTotal;
    g_ui.batchTotal = g_ui.batchDone = 0;
    std::wstring label = count == 1 ? clip(name, 36) : std::to_wstring(count) + L" files";
    showResultLocked(Done, L"Sent", label, L"", 3200);
}

static void senderLoop() {
    for (;;) {
        SendJob job;
        {
            std::unique_lock<std::mutex> lk(g_jobMtx);
            g_jobCv.wait(lk, [] { return !g_jobs.empty(); });
            job = g_jobs.front();
            g_jobs.pop_front();
        }
        try {
            sendFile(job.ip, job.path);
            markSent(baseNameW(job.path));
        } catch (const std::exception& e) {
            {
                std::lock_guard<std::mutex> lk(g_jobMtx);
                g_jobs.clear();
            }
            setResult(Error, L"Couldn't send", clip(baseNameW(job.path), 36), wide(e.what()), 5000);
        }
    }
}

struct Chip { RECT r; std::string ip; };
struct Gui {
    HFONT fonts[8] = {};
    std::vector<Chip> chips;
    RECT zone{};
    RECT refresh{};
    int hoverChip = -1;
    bool hoverZone = false, hoverRefresh = false, tracking = false;
};
static Gui g_gui;

enum FontId { FLabel, FRefresh, FChip, FTitle, FPct, FSub, FDetail };

static void makeFonts() {
    HDC sdc = GetDC(nullptr);
    int dpi = GetDeviceCaps(sdc, LOGPIXELSY);
    ReleaseDC(nullptr, sdc);
    static const int specs[7][2] = {{8, FW_BOLD}, {12, FW_NORMAL}, {10, FW_NORMAL},
                                    {14, FW_BOLD}, {17, FW_BOLD}, {10, FW_NORMAL}, {9, FW_NORMAL}};
    for (int i = 0; i < 7; ++i)
        g_gui.fonts[i] = CreateFontW(-MulDiv(specs[i][0], dpi, 72), 0, 0, 0, specs[i][1], FALSE, FALSE,
                                     FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, kFontName);
}

static void freeFonts() {
    for (auto& f : g_gui.fonts) {
        if (f) DeleteObject(f);
        f = nullptr;
    }
}

static void pill(HDC dc, RECT r, COLORREF fill, COLORREF edge, bool dotted) {
    int corner = sc(24);
    SPen pen(dotted ? PS_DOT : PS_SOLID, std::max(1, sc(1)), edge);
    SelectObject(dc, pen);
    if (fill) {
        SBrush b(fill);
        SelectObject(dc, b);
        RoundRect(dc, r.left, r.top, r.right, r.bottom, corner, corner);
    } else {
        HGDIOBJ nb = GetStockObject(NULL_BRUSH);
        SelectObject(dc, nb);
        RoundRect(dc, r.left, r.top, r.right, r.bottom, corner, corner);
    }
}

static void textCenter(HDC dc, const RECT& r, const std::wstring& s, HFONT f, COLORREF c, UINT fmt) {
    HGDIOBJ prev = SelectObject(dc, f);
    SetTextColor(dc, c);
    RECT tr = r;
    DrawTextW(dc, s.c_str(), (int)s.size(), &tr, fmt);
    SelectObject(dc, prev);
}

static int textWidth(HDC dc, const std::wstring& s, HFONT f) {
    HGDIOBJ prev = SelectObject(dc, f);
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    SelectObject(dc, prev);
    return sz.cx;
}

static void fillRect(HDC dc, const RECT& r, COLORREF c) {
    SBrush b(c);
    FillRect(dc, &r, b);
}

static void drawBadge(HDC dc, int cx, int cy, int size, Mode m, double frac) {
    double u = size / 112.0;
    int R = (int)(44 * u);
    if (m == Active) {
        {
            SPen pen(PS_SOLID, std::max(1, (int)(4 * u)), kTrack);
            HGDIOBJ op = SelectObject(dc, pen);
            HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Ellipse(dc, cx - R, cy - R, cx + R, cy + R);
            SelectObject(dc, op); SelectObject(dc, ob);
        }
        if (frac > 0.004) {
            SPen pen(PS_SOLID, std::max(1, (int)(4 * u)), kFg);
            HGDIOBJ op = SelectObject(dc, pen);
            HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
            if (frac >= 0.998) {
                Ellipse(dc, cx - R, cy - R, cx + R, cy + R);
            } else {
                double a = 360.0 * frac;
                Arc(dc, cx - R, cy - R, cx + R, cy + R, cx, cy - R,
                    cx + (int)(R * sin(a * kPi / 180)), cy - (int)(R * cos(a * kPi / 180)));
            }
            SelectObject(dc, op); SelectObject(dc, ob);
        }
    } else if (m == Done) {
        SBrush b(kFg);
        HGDIOBJ ob = SelectObject(dc, b);
        HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, cx - R, cy - R, cx + R, cy + R);
        SelectObject(dc, ob); SelectObject(dc, op);
        SPen pen(PS_SOLID, std::max(1, (int)(3.4 * u)), kBg);
        HGDIOBJ op2 = SelectObject(dc, pen);
        POINT pts[3] = {{(int)(cx - 14 * u), (int)(cy + 1 * u)},
                        {(int)(cx - 4 * u), (int)(cy + 11 * u)},
                        {(int)(cx + 15 * u), (int)(cy - 10 * u)}};
        Polyline(dc, pts, 3);
        SelectObject(dc, op2);
    } else {
        SPen pen(PS_SOLID, std::max(1, (int)(1.6 * u)), kLineHi);
        HGDIOBJ op = SelectObject(dc, pen);
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Ellipse(dc, cx - R, cy - R, cx + R, cy + R);
        SelectObject(dc, op); SelectObject(dc, ob);
        SPen p2(PS_SOLID, std::max(1, (int)(3.2 * u)), kFg);
        HGDIOBJ op2 = SelectObject(dc, p2);
        MoveToEx(dc, cx, (int)(cy - 16 * u), nullptr);
        LineTo(dc, cx, (int)(cy + 4 * u));
        SelectObject(dc, op2);
        SBrush b(kFg);
        HGDIOBJ ob2 = SelectObject(dc, b);
        int dd = std::max(1, (int)(2.3 * u));
        Ellipse(dc, cx - dd, (int)(cy + 11.5 * u) - dd + dd, cx + dd, (int)(cy + 16.1 * u));
        SelectObject(dc, ob2);
    }
}

static void drawWindow(HWND w) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(w, &ps);
    RECT cr;
    GetClientRect(w, &cr);
    int W = cr.right, H = cr.bottom;
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, W, H);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    SetBkMode(mem, TRANSPARENT);
    fillRect(mem, cr, kBg);
    Ui u;
    {
        std::lock_guard<std::mutex> lk(g_uiMtx);
        u = g_ui;
    }
    int pad = sc(32);
    int padTop = sc(30);
    int padBot = sc(30);
    RECT label{pad, padTop, W - pad - sc(24), padTop + sc(16)};
    textCenter(mem, label, L"SEND TO", g_gui.fonts[FLabel], kMuted,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT ref{W - pad - sc(20), padTop - 2, W - pad, padTop + sc(18)};
    g_gui.refresh = ref;
    textCenter(mem, ref, L"\x21BB", g_gui.fonts[FRefresh],
               g_gui.hoverRefresh ? kFg : kMuted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    g_gui.chips.clear();
    int chH = sc(32), gap = sc(8);
    int x = pad, y = padTop + sc(28), maxY = y;
    if (u.peers.empty()) {
        RECT mr{pad, y, W - pad, y + chH};
        textCenter(mem, mr, u.message, g_gui.fonts[FChip], kMuted,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        for (size_t i = 0; i < u.peers.size(); ++i) {
            const Peer& p = u.peers[i];
            std::wstring label = clip(p.name, 20);
            int cw = textWidth(mem, label, g_gui.fonts[FChip]) + sc(38);
            if (x > pad && x + cw > W - pad) { x = pad; y += chH + gap; }
            RECT r{x, y, x + cw, y + chH};
            bool sel = p.ip == u.selected;
            bool hov = (int)i == g_gui.hoverChip;
            COLORREF fill = sel ? kFg : 0;
            COLORREF edge = hov ? (sel ? kFg : kLineHi) : (sel ? kFg : kLine);
            pill(mem, r, fill, edge, false);
            textCenter(mem, r, label, g_gui.fonts[FChip], sel ? kBg : kDim,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            g_gui.chips.push_back({r, p.ip});
            x += cw + gap;
        }
        maxY = y + chH + 2;
    }
    int zTop = maxY + sc(24);
    if (u.peers.empty()) zTop = y + chH + sc(24);
    RECT zone{pad, zTop, W - pad, H - padBot};
    g_gui.zone = zone;
    if (zone.bottom - zone.top >= sc(180) && zone.right - zone.left >= sc(120)) {
        bool idle = u.mode == Idle;
        bool hot = idle && g_gui.hoverZone;
        COLORREF fill, edge;
        bool dotted = false;
        if (idle) {
            fill = hot ? kCardHi : kCard;
            edge = hot ? kLineHi : RGB(0x3A, 0x3A, 0x3A);
            dotted = !hot;
        } else {
            fill = kCard; edge = kLine;
        }
        pill(mem, zone, fill, edge, dotted);
        int cx = (zone.left + zone.right) / 2;
        int cy = (zone.top + zone.bottom) / 2 - sc(30);
        int size = sc(112);
        if (!idle) drawBadge(mem, cx, cy, size, u.mode, u.frac);
        std::wstring title = idle ? L"Drag and drop" : u.title;
        std::wstring sub = idle ? L"Click to browse" : u.sub;
        std::wstring detail = idle ? L"" : u.detail;
        if (u.mode == Active) {
            wchar_t pc[16];
            swprintf(pc, 16, L"%d%%", (int)(u.frac * 100));
            textCenter(mem, {cx - size / 2, cy - size / 2, cx + size / 2, cy + size / 2}, pc,
                       g_gui.fonts[FPct], kFg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        int ty = idle ? cy - sc(8) : cy + sc(70);
        textCenter(mem, {zone.left, ty, zone.right, ty + sc(20)}, title,
                   g_gui.fonts[FTitle], kFg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (!sub.empty())
            textCenter(mem, {zone.left, ty + sc(26), zone.right, ty + sc(44)}, sub,
                       g_gui.fonts[FSub], idle ? kDim : kFg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (!detail.empty())
            textCenter(mem, {zone.left + sc(36), cy + sc(118), zone.right - sc(36), cy + sc(140)},
                       detail, g_gui.fonts[FDetail], kMuted,
                       DT_CENTER | DT_VCENTER | DT_WORDBREAK);
    }
    BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(w, &ps);
}

static std::vector<std::wstring> browseFiles(HWND w) {
    std::vector<wchar_t> buf(65536, 0);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = w;
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = (DWORD)buf.size();
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    std::vector<std::wstring> out;
    if (GetOpenFileNameW(&ofn)) {
        const wchar_t* p = buf.data();
        std::wstring first = p;
        p += first.size() + 1;
        if (!*p) out.push_back(first);
        else {
            while (*p) {
                out.push_back(first + L"\\" + p);
                p += wcslen(p) + 1;
            }
        }
    }
    return out;
}

static void startAnimate(HWND w) { SetTimer(w, kTimerAnimate, 33, nullptr); }
static void stopAnimate(HWND w) { KillTimer(w, kTimerAnimate); }
static void startRevert(HWND w, int ms) {
    KillTimer(w, kTimerRevert);
    SetTimer(w, kTimerRevert, ms, nullptr);
}
static void stopRevert(HWND w) { KillTimer(w, kTimerRevert); }

static unsigned g_seenResult = 0;

static void onState(HWND w) {
    g_repaint = false;
    Mode m;
    int hold;
    bool fresh;
    {
        std::lock_guard<std::mutex> lk(g_uiMtx);
        m = g_ui.mode;
        hold = g_ui.holdMs;
        fresh = g_ui.resultSerial != g_seenResult;
        g_seenResult = g_ui.resultSerial;
    }
    if (m == Active) {
        startAnimate(w);
        stopRevert(w);
    } else {
        stopAnimate(w);
        if (fresh && (m == Done || m == Error)) startRevert(w, hold ? hold : 3000);
    }
    InvalidateRect(w, nullptr, FALSE);
}

static void onTimer(HWND w, UINT id) {
    if (id == kTimerRefresh) {
        refreshPeersAsync();
    } else if (id == kTimerAnimate) {
        {
            std::lock_guard<std::mutex> lk(g_uiMtx);
            if (g_ui.mode != Active) { stopAnimate(w); return; }
            g_ui.frac += (g_ui.target - g_ui.frac) * 0.35;
            if (std::fabs(g_ui.target - g_ui.frac) < 0.002) g_ui.frac = g_ui.target;
        }
        InvalidateRect(w, nullptr, FALSE);
    } else if (id == kTimerRevert) {
        KillTimer(w, kTimerRevert);
        std::lock_guard<std::mutex> lk(g_uiMtx);
        g_ui.mode = Idle;
        InvalidateRect(w, nullptr, FALSE);
    }
}

static void onMouseMove(HWND w, int x, int y) {
    POINT pt{x, y};
    int hc = -1;
    for (size_t i = 0; i < g_gui.chips.size(); ++i)
        if (PtInRect(&g_gui.chips[i].r, pt)) { hc = (int)i; break; }
    bool hz = PtInRect(&g_gui.zone, pt) != 0;
    bool hr = PtInRect(&g_gui.refresh, pt) != 0;
    bool changed = hc != g_gui.hoverChip || hz != g_gui.hoverZone || hr != g_gui.hoverRefresh;
    g_gui.hoverChip = hc; g_gui.hoverZone = hz; g_gui.hoverRefresh = hr;
    if (!g_gui.tracking) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, w, 0};
        TrackMouseEvent(&tme);
        g_gui.tracking = true;
    }
    if (changed) InvalidateRect(w, nullptr, FALSE);
}

static void onMouseLeave(HWND w) {
    g_gui.tracking = false;
    g_gui.hoverChip = -1;
    g_gui.hoverZone = g_gui.hoverRefresh = false;
    InvalidateRect(w, nullptr, FALSE);
}

static void onClick(HWND w, int x, int y) {
    POINT pt{x, y};
    if (PtInRect(&g_gui.refresh, pt)) { refreshPeersAsync(); return; }
    for (auto& c : g_gui.chips)
        if (PtInRect(&c.r, pt)) {
            std::lock_guard<std::mutex> lk(g_uiMtx);
            g_ui.selected = c.ip;
            InvalidateRect(w, nullptr, FALSE);
            return;
        }
    if (!PtInRect(&g_gui.zone, pt)) return;
    Mode m;
    {
        std::lock_guard<std::mutex> lk(g_uiMtx);
        m = g_ui.mode;
    }
    if (m == Idle) queueSend(browseFiles(w));
    else if (m == Done || m == Error) {
        KillTimer(w, kTimerRevert);
        std::lock_guard<std::mutex> lk(g_uiMtx);
        g_ui.mode = Idle;
        InvalidateRect(w, nullptr, FALSE);
    }
}

static void onDrop(HWND w, HDROP drop) {
    UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    std::vector<std::wstring> paths;
    wchar_t buf[MAX_PATH];
    for (UINT i = 0; i < n; ++i) {
        if (!DragQueryFileW(drop, i, buf, MAX_PATH)) continue;
        DWORD attr = GetFileAttributesW(buf);
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
            paths.push_back(buf);
    }
    DragFinish(drop);
    queueSend(std::move(paths));
}

static void applyDarkTitle(HWND w) {
    int one = 1;
    int bg = 0x0A0A0A;
    int fg = 0xEDEDED;
    DwmSetWindowAttribute(w, 20, &one, sizeof(one));
    DwmSetWindowAttribute(w, 19, &one, sizeof(one));
    DwmSetWindowAttribute(w, 35, &bg, sizeof(bg));
    DwmSetWindowAttribute(w, 36, &fg, sizeof(fg));
}

static LRESULT CALLBACK wndProc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
        case WM_CREATE:
            makeFonts();
            DragAcceptFiles(w, TRUE);
            SetTimer(w, kTimerRefresh, 5000, nullptr);
            refreshPeersAsync();
            {
                std::lock_guard<std::mutex> lk(g_uiMtx);
                g_seenResult = g_ui.resultSerial;
            }
            return 0;
        case WM_DESTROY:
            KillTimer(w, kTimerRefresh);
            KillTimer(w, kTimerAnimate);
            KillTimer(w, kTimerRevert);
            DragAcceptFiles(w, FALSE);
            freeFonts();
            g_window = nullptr;
            g_repaint = false;
            return 0;
        case WM_SIZE:
            InvalidateRect(w, nullptr, TRUE);
            if (wp == SIZE_MINIMIZED) DestroyWindow(w);
            return 0;
        case WM_CLOSE:
            DestroyWindow(w);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            drawWindow(w);
            return 0;
        case WM_TIMER:
            onTimer(w, (UINT)wp);
            return 0;
        case WM_LBUTTONDOWN:
            onClick(w, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        case WM_MOUSEMOVE:
            onMouseMove(w, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        case WM_MOUSELEAVE:
            onMouseLeave(w);
            return 0;
        case WM_SETCURSOR: {
            if (g_gui.hoverChip >= 0 || g_gui.hoverRefresh || g_gui.hoverZone) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        }
        case WM_DROPFILES:
            onDrop(w, (HDROP)wp);
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mm = (MINMAXINFO*)lp;
            mm->ptMinTrackSize.x = sc(380);
            mm->ptMinTrackSize.y = sc(540);
            return 0;
        }
        case kWmState:
            onState(w);
            return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

static HWND g_trayHwnd = nullptr;

static void openWindow();

static LRESULT CALLBACK trayProc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    if (m == kTrayMsg) {
        if (lp == WM_LBUTTONUP) {
            openWindow();
        } else if (lp == WM_RBUTTONUP) {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, kCmdOpen, L"Open");
            AppendMenuW(menu, MF_STRING, kCmdQuit, L"Quit");
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(w);
            TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, w, nullptr);
            PostMessageW(w, WM_NULL, 0, 0);
            DestroyMenu(menu);
        }
        return 0;
    }
    if (m == WM_COMMAND) {
        if (LOWORD(wp) == kCmdOpen) openWindow();
        else if (LOWORD(wp) == kCmdQuit) {
            NOTIFYICONDATAW nid{};
            nid.cbSize = sizeof(nid);
            nid.hWnd = w;
            nid.uID = 1;
            Shell_NotifyIconW(NIM_DELETE, &nid);
            PostQuitMessage(0);
        }
        return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

static HICON g_icon = nullptr;

static void openWindow() {
    HWND ex = g_window.load();
    if (ex) {
        ShowWindow(ex, SW_RESTORE);
        SetForegroundWindow(ex);
        return;
    }
    RECT r{0, 0, sc(440), sc(620)};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND w = CreateWindowExW(0, L"TaildropWindow", L"Taildrop",
                             WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                             r.right - r.left, r.bottom - r.top, nullptr, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    g_window = w;
    applyDarkTitle(w);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    SetProcessDPIAware();
    HDC sdc = GetDC(nullptr);
    g_S = GetDeviceCaps(sdc, LOGPIXELSY) / 96.0;
    ReleaseDC(nullptr, sdc);
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    g_icon = LoadIconW(inst, MAKEINTRESOURCEW(kIconRes));
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"TaildropWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = g_icon;
    wc.hIconSm = g_icon;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);
    WNDCLASSEXW tc{};
    tc.cbSize = sizeof(tc);
    tc.lpfnWndProc = trayProc;
    tc.hInstance = inst;
    tc.lpszClassName = L"TaildropTray";
    RegisterClassExW(&tc);
    g_trayHwnd = CreateWindowExW(0, L"TaildropTray", L"TaildropTray", 0, 0, 0, 0, 0,
                                 HWND_MESSAGE, nullptr, inst, nullptr);
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_trayHwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = kTrayMsg;
    nid.hIcon = g_icon;
    wcscpy_s(nid.szTip, L"Taildrop");
    Shell_NotifyIconW(NIM_ADD, &nid);
    std::thread(serverLoop).detach();
    std::thread(senderLoop).detach();
    std::thread(ensureTailscale).detach();
    std::thread(checkUpdate).detach();
    openWindow();
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
