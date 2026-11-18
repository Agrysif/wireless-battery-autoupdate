// Silent automatic updates for the mytoolkit.ru build of Wireless Battery.
// Not part of upstream Wireless Battery: added by patch/apply.py of
// https://github.com/Agrysif/wireless-battery-autoupdate (GPL-3.0-or-later, like the app).
#pragma once
#include <windows.h>
#include <array>
#include <string>

namespace tkupdate {

constexpr UINT TK_ID_AUTOUPDATE = 950;  // tray menu command id (free range between monitors/languages and devices)

bool Available();        // a per-user installed copy (the only kind that can update itself without asking)
bool Enabled();          // "Update automatically" in the tray menu, on by default
void SetEnabled(bool on);
void Start();            // background thread: first check after 2 minutes, then every 6 hours

namespace detail {  // exposed for tests
struct Manifest {
    std::string version, file, sha256, sig;
    unsigned long long size = 0;
};
bool ParseVersion(const std::string& s, std::array<int, 4>& out);
int CompareVersions(const std::string& a, const std::string& b);  // <0, 0, >0; unparsable counts as lowest
bool ParseManifest(const std::string& json, Manifest& m);         // fields present and well-formed
bool VerifyManifest(const Manifest& m);                            // ECDSA P-256 signature of the server key
bool Sha256Hex(const std::string& data, std::string& hex);
}  // namespace detail

}  // namespace tkupdate
