// Tests for tk_update.cpp: version order, manifest parsing and the server signature.
// The sample manifest below was signed on mytoolkit.ru by /root/wb_mirror.php --sign-test.
#include "tk_update.h"

#include <cstdio>

using namespace tkupdate::detail;

static int fails = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);      \
            fails++;                                                      \
        }                                                                 \
    } while (0)

static const char* kSigned = R"({
    "name": "wireless-battery",
    "version": "1.0.8.1",
    "file": "WirelessBattery-Setup-1.0.8.1.exe",
    "size": 2861915,
    "sha256": "0f644e5985c36a946e703e0cf4ec421e403bf423a879fcc06798933715f5e8a0",
    "sig": "421a6cecc0e6db5e9be0640b764ecc6ca20f358f1cfb8e8c5fe170f01a9413c670505ecf97eeb8b1256a939d252c66332b0cf4345641801ae14911a22bc08573",
    "upstream": "v0.0.0",
    "date": "2026-01-01"
})";

int main() {
    CHECK(CompareVersions("1.0.8.1", "1.0.8.0") > 0);
    CHECK(CompareVersions("1.0.10.1", "1.0.9.9") > 0);
    CHECK(CompareVersions("1.0.8.1", "1.0.8.1") == 0);
    CHECK(CompareVersions("2.0.0.1", "10.0.0.1") < 0);
    CHECK(CompareVersions("garbage", "1.0.0.0") < 0);
    CHECK(CompareVersions("1.0.8", "1.0.0.0") < 0);

    Manifest m;
    CHECK(ParseManifest(kSigned, m));
    CHECK(m.version == "1.0.8.1" && m.size == 2861915 && m.file == "WirelessBattery-Setup-1.0.8.1.exe");
    CHECK(VerifyManifest(m));

    Manifest t = m;
    t.version = "9.9.9.9";
    CHECK(!VerifyManifest(t));
    t = m;
    t.sha256[0] = t.sha256[0] == '0' ? '1' : '0';
    CHECK(!VerifyManifest(t));
    t = m;
    t.size++;
    CHECK(!VerifyManifest(t));
    t = m;
    t.file = "Other.exe";
    CHECK(!VerifyManifest(t));
    t = m;
    t.sig[10] = t.sig[10] == 'a' ? 'b' : 'a';
    CHECK(!VerifyManifest(t));

    std::string bad = kSigned;
    bad.replace(bad.find("WirelessBattery-Setup-1.0.8.1.exe"), 33, "..\\\\..\\\\evil-Setup-1.0.8.1.exe");
    CHECK(!ParseManifest(bad, t));
    std::string other = kSigned;
    other.replace(other.find("wireless-battery"), 16, "something-else!!");
    CHECK(!ParseManifest(other, t));
    CHECK(!ParseManifest("{}", t));

    std::string h;
    CHECK(Sha256Hex("abc", h) && h == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    std::printf(fails ? "tk_update tests: %d FAILED\n" : "tk_update tests: ok\n", fails);
    return fails ? 1 : 0;
}
