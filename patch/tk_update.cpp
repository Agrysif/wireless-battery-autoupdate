// Silent automatic updates for the mytoolkit.ru build of Wireless Battery.
// Not part of upstream Wireless Battery: added by patch/apply.py of
// https://github.com/Agrysif/wireless-battery-autoupdate (GPL-3.0-or-later, like the app).
//
// Every 6 hours: download https://mytoolkit.ru/files/programmy/wireless-battery/latest.json,
// check its ECDSA P-256 signature (the private key never leaves the server), and if it names a newer
// version, download that installer, compare its SHA-256 with the signed one and run it silently.
// The installer closes this copy, upgrades it in place and starts it again (/TKUPDATE=1).
#include "tk_update.h"
#include "tk_version.h"

#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <winhttp.h>

#include <cstring>
#include <vector>

namespace tkupdate {
namespace {

const wchar_t* kHost = L"mytoolkit.ru";
const wchar_t* kDir = L"/files/programmy/wireless-battery/";
const wchar_t* kSettingsKey = L"Software\\WirelessBattery";
const wchar_t* kUninstallKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{7E0B5C2A-3F4D-4C8E-9B61-2D5A8F1C4E93}_is1";
const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr DWORD kFirstDelay = 2 * 60 * 1000;
constexpr DWORD kInterval = 6 * 60 * 60 * 1000;
constexpr DWORD kBusyRetry = 30 * 60 * 1000;
constexpr size_t kMaxInstaller = 64u << 20;

// Public half of /root/keys/wireless-battery-update.pem on mytoolkit.ru (X then Y).
const unsigned char kPublicKey[64] = {
    0x6a, 0xe9, 0x5f, 0x61, 0x5b, 0x6a, 0xbd, 0x08, 0x86, 0xd8, 0x34, 0xf4, 0x64, 0x19, 0x76, 0x12,
    0x79, 0x4a, 0x0a, 0xe3, 0x79, 0x5b, 0x49, 0xdf, 0x9b, 0x55, 0x4c, 0x6f, 0xd7, 0x1d, 0x28, 0x61,
    0xb9, 0xb7, 0x68, 0x2a, 0xc9, 0xb7, 0xfe, 0xc8, 0x7d, 0x1c, 0xfb, 0xc6, 0x30, 0x52, 0x73, 0x32,
    0xdb, 0x70, 0x7a, 0xcf, 0x31, 0x2a, 0x96, 0x33, 0x62, 0xc5, 0xc6, 0x58, 0xdb, 0x6a, 0xd7, 0x6d,
};

std::string Narrow(const wchar_t* w) {
    std::string s;
    for (; *w; w++) s += (char)*w;  // ASCII only (version strings)
    return s;
}

std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

std::wstring ExePath() {
    std::vector<wchar_t> buf(MAX_PATH);
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
        if (n < buf.size()) return std::wstring(buf.data(), n);
        buf.resize(buf.size() * 2);
    }
}

std::wstring ExeDir() {
    std::wstring p = ExePath();
    size_t k = p.find_last_of(L'\\');
    return k == std::wstring::npos ? L"" : p.substr(0, k);
}

bool ReadString(HKEY root, const wchar_t* key, const wchar_t* name, std::wstring& out) {
    DWORD size = 0;
    if (RegGetValueW(root, key, name, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS || size < sizeof(wchar_t)) return false;
    std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1);
    if (RegGetValueW(root, key, name, RRF_RT_REG_SZ, nullptr, buf.data(), &size) != ERROR_SUCCESS) return false;
    out = buf.data();
    return true;
}

DWORD ReadDword(const wchar_t* name, DWORD def) {
    DWORD v = def, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_DWORD, nullptr, &v, &sz) != ERROR_SUCCESS) v = def;
    return v;
}

void WriteDword(const wchar_t* name, DWORD v) { RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, name, REG_DWORD, &v, sizeof(v)); }

std::wstring TrimSlash(std::wstring s) {
    while (!s.empty() && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
    return s;
}

bool HttpGet(const std::wstring& path, std::string& out, size_t maxBytes) {
    out.clear();
    std::wstring agent = std::wstring(L"WirelessBattery-tk/") + TK_VERSION_W;
    HINTERNET s = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return false;
    WinHttpSetTimeouts(s, 15000, 15000, 30000, 60000);
    HINTERNET c = WinHttpConnect(s, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH)
                    : nullptr;
    bool ok = false;
    if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr)) {
        DWORD code = 0, sz = sizeof(code);
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz,
                            WINHTTP_NO_HEADER_INDEX);
        if (code == 200) {
            ok = true;
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(r, &avail)) { ok = false; break; }
                if (!avail) break;
                size_t old = out.size();
                if (old + avail > maxBytes) { ok = false; break; }
                out.resize(old + avail);
                DWORD got = 0;
                if (!WinHttpReadData(r, &out[old], avail, &got)) { ok = false; break; }
                out.resize(old + got);
            }
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}

bool IsHex(const std::string& s, size_t len) {
    if (s.size() != len) return false;
    for (char ch : s)
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
    return true;
}

bool FromHex(const std::string& s, std::vector<unsigned char>& out) {
    if (s.size() % 2) return false;
    out.clear();
    auto nib = [](char ch) { return ch <= '9' ? ch - '0' : ch - 'a' + 10; };
    for (size_t i = 0; i < s.size(); i += 2) out.push_back((unsigned char)(nib(s[i]) * 16 + nib(s[i + 1])));
    return true;
}

bool Sha256(const void* data, size_t n, unsigned char out[32]) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return false;
    BCRYPT_HASH_HANDLE h = nullptr;
    bool ok = BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0 &&
              BCryptHashData(h, (PUCHAR)data, (ULONG)n, 0) == 0 &&
              BCryptFinishHash(h, out, 32, 0) == 0;
    if (h) BCryptDestroyHash(h);
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

// Fullscreen game, presentation or "busy": do not make the widget blink now.
bool UserBusy() {
    QUERY_USER_NOTIFICATION_STATE st;
    if (SHQueryUserNotificationState(&st) != S_OK) return false;
    return st == QUNS_BUSY || st == QUNS_RUNNING_D3D_FULL_SCREEN || st == QUNS_PRESENTATION_MODE;
}

bool AutostartOn() {
    std::wstring v;
    if (!ReadString(HKEY_CURRENT_USER, kRunKey, L"WirelessBattery", v)) return false;
    return lstrcmpiW(v.c_str(), (L"\"" + ExePath() + L"\"").c_str()) == 0;
}

enum Result { kNothing, kBusy, kStarted };

Result CheckOnce() {
    using namespace detail;
    std::string json;
    if (!HttpGet(std::wstring(kDir) + L"latest.json", json, 8192)) return kNothing;
    Manifest m;
    if (!ParseManifest(json, m) || !VerifyManifest(m)) return kNothing;
    if (CompareVersions(m.version, Narrow(TK_VERSION_W)) <= 0) return kNothing;

    // A version whose installer already ran twice without effect is not retried until a newer one appears.
    std::wstring tried;
    ReadString(HKEY_CURRENT_USER, kSettingsKey, L"TkUpdateTried", tried);
    DWORD tries = tried == Widen(m.version) ? ReadDword(L"TkUpdateTries", 0) : 0;
    if (tries >= 2) return kNothing;
    if (UserBusy()) return kBusy;

    std::string exe;
    if (!HttpGet(std::wstring(kDir) + Widen(m.file), exe, kMaxInstaller)) return kNothing;
    std::string hex;
    if (exe.size() != m.size || !Sha256Hex(exe, hex) || hex != m.sha256 || exe.compare(0, 2, "MZ") != 0) return kNothing;

    wchar_t tmp[MAX_PATH + 1];
    DWORD n = GetTempPathW(MAX_PATH + 1, tmp);
    if (!n || n > MAX_PATH) return kNothing;
    std::wstring dir = std::wstring(tmp) + L"WirelessBattery-update";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring path = dir + L"\\" + Widen(m.file);
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return kNothing;
    DWORD written = 0;
    bool ok = WriteFile(f, exe.data(), (DWORD)exe.size(), &written, nullptr) && written == exe.size();
    CloseHandle(f);
    if (!ok) { DeleteFileW(path.c_str()); return kNothing; }

    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"TkUpdateTried", REG_SZ, Widen(m.version).c_str(),
                    (DWORD)((m.version.size() + 1) * sizeof(wchar_t)));
    WriteDword(L"TkUpdateTries", tries + 1);

    // Keep the user's "Start with Windows" choice: a silent install would otherwise select the task again.
    std::wstring cmd = L"\"" + path + L"\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /TKUPDATE=1 /MERGETASKS=\"" +
                       (AutostartOn() ? L"autostart" : L"!autostart") + L"\"";
    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(path.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) return kNothing;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return kStarted;  // the installer closes this process with --quit before replacing the files
}

DWORD WINAPI Thread(void*) {
    DWORD wait = kFirstDelay;
    for (;;) {
        Sleep(wait);
        wait = kInterval;
        if (!Enabled() || !Available()) continue;
        Result r = CheckOnce();
        if (r == kBusy) wait = kBusyRetry;
        if (r == kStarted) wait = kBusyRetry;  // if the installer failed to start the upgrade, look again later
    }
}

}  // namespace

namespace detail {

bool ParseVersion(const std::string& s, std::array<int, 4>& out) {
    size_t i = 0;
    for (int part = 0; part < 4; part++) {
        if (part) {
            if (i >= s.size() || s[i] != '.') return false;
            i++;
        }
        size_t start = i;
        long v = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9' && i - start < 6) v = v * 10 + (s[i++] - '0');
        if (i == start) return false;
        out[part] = (int)v;
    }
    return i == s.size();
}

int CompareVersions(const std::string& a, const std::string& b) {
    std::array<int, 4> x{}, y{};
    bool okx = ParseVersion(a, x), oky = ParseVersion(b, y);
    if (!okx || !oky) return (int)okx - (int)oky;
    for (int i = 0; i < 4; i++)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

static std::string Field(const std::string& j, const char* key) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = j.find(k);
    if (p == std::string::npos) return "";
    p = j.find(':', p + k.size());
    if (p == std::string::npos) return "";
    p++;
    while (p < j.size() && (j[p] == ' ' || j[p] == '\t' || j[p] == '\r' || j[p] == '\n')) p++;
    if (p >= j.size()) return "";
    if (j[p] == '"') {
        size_t e = j.find('"', p + 1);
        return e == std::string::npos ? "" : j.substr(p + 1, e - p - 1);
    }
    size_t e = p;
    while (e < j.size() && j[e] >= '0' && j[e] <= '9') e++;
    return j.substr(p, e - p);
}

bool ParseManifest(const std::string& json, Manifest& m) {
    if (Field(json, "name") != "wireless-battery") return false;
    m.version = Field(json, "version");
    m.file = Field(json, "file");
    m.sha256 = Field(json, "sha256");
    m.sig = Field(json, "sig");
    std::string size = Field(json, "size");
    std::array<int, 4> v{};
    if (!ParseVersion(m.version, v) || !IsHex(m.sha256, 64) || !IsHex(m.sig, 128)) return false;
    if (size.empty() || size.size() > 10) return false;
    m.size = std::stoull(size);
    if (!m.size || m.size > kMaxInstaller) return false;
    // A plain file name next to latest.json, nothing else.
    if (m.file.size() < 5 || m.file.size() > 80 || m.file.compare(m.file.size() - 4, 4, ".exe") != 0) return false;
    for (char ch : m.file)
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '.' || ch == '-' || ch == '_'))
            return false;
    return true;
}

bool VerifyManifest(const Manifest& m) {
    // Exactly what the server signs: name|version|file|size|sha256
    std::string msg = "wireless-battery|" + m.version + "|" + m.file + "|" + std::to_string(m.size) + "|" + m.sha256;
    unsigned char hash[32];
    std::vector<unsigned char> sig;
    if (!Sha256(msg.data(), msg.size(), hash) || !FromHex(m.sig, sig) || sig.size() != 64) return false;
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) != 0) return false;
    std::vector<unsigned char> blob(sizeof(BCRYPT_ECCKEY_BLOB) + sizeof(kPublicKey));
    BCRYPT_ECCKEY_BLOB hdr{ BCRYPT_ECDSA_PUBLIC_P256_MAGIC, 32 };
    memcpy(blob.data(), &hdr, sizeof(hdr));
    memcpy(blob.data() + sizeof(hdr), kPublicKey, sizeof(kPublicKey));
    BCRYPT_KEY_HANDLE key = nullptr;
    bool ok = false;
    if (BCryptImportKeyPair(alg, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key, blob.data(), (ULONG)blob.size(), 0) == 0) {
        ok = BCryptVerifySignature(key, nullptr, hash, sizeof(hash), sig.data(), (ULONG)sig.size(), 0) == 0;
        BCryptDestroyKey(key);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

bool Sha256Hex(const std::string& data, std::string& hex) {
    unsigned char h[32];
    if (!Sha256(data.data(), data.size(), h)) return false;
    static const char* d = "0123456789abcdef";
    hex.clear();
    for (unsigned char b : h) { hex += d[b >> 4]; hex += d[b & 15]; }
    return true;
}

}  // namespace detail

bool Available() {
    // Inno Setup writes this key only for per-user installs; its InstallLocation must be this copy's folder.
    std::wstring loc;
    if (!ReadString(HKEY_CURRENT_USER, kUninstallKey, L"InstallLocation", loc)) return false;
    return lstrcmpiW(TrimSlash(loc).c_str(), TrimSlash(ExeDir()).c_str()) == 0;
}

bool Enabled() { return ReadDword(L"AutoUpdate", 1) != 0; }

void SetEnabled(bool on) { WriteDword(L"AutoUpdate", on ? 1 : 0); }

void Start() {
    static bool started = false;
    if (started) return;
    started = true;
    if (HANDLE t = CreateThread(nullptr, 0, Thread, nullptr, 0, nullptr)) CloseHandle(t);
}

}  // namespace tkupdate
