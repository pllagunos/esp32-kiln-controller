#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <SPIFFS.h>
#include <vector>

#include "common.h"
#include "ota.h"

#ifndef OTA_VERSION
  #define OTA_VERSION "local_development"
#endif

static const char* GITHUB_OWNER   = "pllagunos";
static const char* GITHUB_REPO    = "esp32-kiln-controller";
static const char* FIRMWARE_ASSET = "esp32doit-devkit-v1_firmware.bin";
static const char* SPIFFS_ASSET   = "esp32doit-devkit-v1_spiffs.bin";

// Root CA cert covering api.github.com (Sectigo Public Server Authentication chain, 2026).
// Intermediate: Sectigo Public Server Authentication CA DV E36
// Root (cross-signed): Sectigo Public Server Authentication Root E46
static const char* GITHUB_CA = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDXzCCAuagAwIBAgIQNuBZ7YiN1Xrt1XC2cn+b2jAKBggqhkjOPQQDAzBfMQsw
CQYDVQQGEwJHQjEYMBYGA1UEChMPU2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQDEy1T
ZWN0aWdvIFB1YmxpYyBTZXJ2ZXIgQXV0aGVudGljYXRpb24gUm9vdCBFNDYwHhcN
MjEwMzIyMDAwMDAwWhcNMzYwMzIxMjM1OTU5WjBgMQswCQYDVQQGEwJHQjEYMBYG
A1UEChMPU2VjdGlnbyBMaW1pdGVkMTcwNQYDVQQDEy5TZWN0aWdvIFB1YmxpYyBT
ZXJ2ZXIgQXV0aGVudGljYXRpb24gQ0EgRFYgRTM2MFkwEwYHKoZIzj0CAQYIKoZI
zj0DAQcDQgAEaKGnbAUnBYljHDmn/yUhxe3TLxKYuyzc9VXoSaCEV5F73Fhfa/Si
/RMsmwTFW3R9s7J6JpYZFmu4do3vk/Vgl6OCAYEwggF9MB8GA1UdIwQYMBaAFNEi
2kxZ8UtfJjiqndbu6w3D+6lhMB0GA1UdDgQWBBQXmagEwW/kLXCoChA9A9PpGrgm
YzAOBgNVHQ8BAf8EBAMCAYYwEgYDVR0TAQH/BAgwBgEB/wIBADAdBgNVHSUEFjAU
BggrBgEFBQcDAQYIKwYBBQUHAwIwGwYDVR0gBBQwEjAGBgRVHSAAMAgGBmeBDAEC
ATBUBgNVHR8ETTBLMEmgR6BFhkNodHRwOi8vY3JsLnNlY3RpZ28uY29tL1NlY3Rp
Z29QdWJsaWNTZXJ2ZXJBdXRoZW50aWNhdGlvblJvb3RFNDYuY3JsMIGEBggrBgEF
BQcBAQR4MHYwTwYIKwYBBQUHMAKGQ2h0dHA6Ly9jcnQuc2VjdGlnby5jb20vU2Vj
dGlnb1B1YmxpY1NlcnZlckF1dGhlbnRpY2F0aW9uUm9vdEU0Ni5wN2MwIwYIKwYB
BQUHMAGGF2h0dHA6Ly9vY3NwLnNlY3RpZ28uY29tMAoGCCqGSM49BAMDA2cAMGQC
MFsKnBQDh64l+v+aUYWjDCJKQMxHUUGmcwAYDIjJ9pbRYItMCIx5xu0oUb6sIfTX
qQIwPddcsDE4KdeLu1hJdpHgdLvsHAK3vygyLGujMU9xBJCDackRT93VHEE0gppg
NqdV
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIDRjCCAsugAwIBAgIQGp6v7G3o4ZtcGTFBto2Q3TAKBggqhkjOPQQDAzCBiDEL
MAkGA1UEBhMCVVMxEzARBgNVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNl
eSBDaXR5MR4wHAYDVQQKExVUaGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMT
JVVTRVJUcnVzdCBFQ0MgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMjEwMzIy
MDAwMDAwWhcNMzgwMTE4MjM1OTU5WjBfMQswCQYDVQQGEwJHQjEYMBYGA1UEChMP
U2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQDEy1TZWN0aWdvIFB1YmxpYyBTZXJ2ZXIg
QXV0aGVudGljYXRpb24gUm9vdCBFNDYwdjAQBgcqhkjOPQIBBgUrgQQAIgNiAAR2
+pmpbiDt+dd34wc7qNs9Xzjoq1WmVk/WSOrsfy2qw7LFeeyZYX8QeccCWvkEN/U0
NSt3zn8gj1KjAIns1aeibVvjS5KToID1AZTc8GgHHs3u/iVStSBDHBv+6xnOQ6Oj
ggEgMIIBHDAfBgNVHSMEGDAWgBQ64QmG1M8ZwpZ2dEl23OA1xmNjmjAdBgNVHQ4E
FgQU0SLaTFnxS18mOKqd1u7rDcP7qWEwDgYDVR0PAQH/BAQDAgGGMA8GA1UdEwEB
/wQFMAMBAf8wHQYDVR0lBBYwFAYIKwYBBQUHAwEGCCsGAQUFBwMCMBEGA1UdIAQK
MAgwBgYEVR0gADBQBgNVHR8ESTBHMEWgQ6BBhj9odHRwOi8vY3JsLnVzZXJ0cnVz
dC5jb20vVVNFUlRydXN0RUNDQ2VydGlmaWNhdGlvbkF1dGhvcml0eS5jcmwwNQYI
KwYBBQUHAQEEKTAnMCUGCCsGAQUFBzABhhlodHRwOi8vb2NzcC51c2VydHJ1c3Qu
Y29tMAoGCCqGSM49BAMDA2kAMGYCMQCMCyBit99vX2ba6xEkDe+YO7vC0twjbkv9
PKpqGGuZ61JZryjFsp+DFpEclCVy4noCMQCwvZDXD/m2Ko1HA5Bkmz7YQOFAiNDD
49IWa2wdT7R3DtODaSXH/BiXv8fwB9su4tU=
-----END CERTIFICATE-----
)EOF";


static String s_latest_version;
static String s_latest_tag;
static String s_firmware_url;
static String s_firmware_md5;
static String s_spiffs_url;
static String s_spiffs_md5;

static bool checkForUpdate() {
  // Clear stale state so any failure leaves them empty (prevents masking errors as UP_TO_DATE)
  s_latest_version = "";
  s_latest_tag     = "";
  s_firmware_url   = "";
  s_firmware_md5   = "";
  s_spiffs_url     = "";
  s_spiffs_md5     = "";

  WiFiClientSecure secure;
  secure.setCACert(GITHUB_CA);

  HTTPClient http;
  String url = "https://api.github.com/repos/" + String(GITHUB_OWNER) + "/" + String(GITHUB_REPO) + "/releases/latest";
  http.begin(secure, url);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.addHeader("Accept", "application/vnd.github+json");
  http.addHeader("User-Agent", "ESP32-OTA");
  http.addHeader("X-GitHub-Api-Version", "2022-11-28");

  int code = http.GET();
  if (code != 200) {
    log_e("GitHub API returned %d", code);
    http.end();
    return false;
  }

  StaticJsonDocument<8192> doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();

  if (err) {
    log_e("JSON parse error: %s", err.c_str());
    return false;
  }

  s_latest_version = doc["name"].as<String>();
  s_latest_tag     = doc["tag_name"].as<String>();

  // Parse MD5s from release body — CI embeds them over the already-verified
  // api.github.com connection rather than the insecure CDN.
  String body = doc["body"].as<String>();
  int mdxIdx = body.indexOf("Firmware MD5: ");
  if (mdxIdx >= 0) {
    s_firmware_md5 = body.substring(mdxIdx + 14, mdxIdx + 46);
    s_firmware_md5.trim();
    s_firmware_md5.toLowerCase();
  }
  int spiffsIdx = body.indexOf("SPIFFS MD5: ");
  if (spiffsIdx >= 0) {
    s_spiffs_md5 = body.substring(spiffsIdx + 12, spiffsIdx + 44);
    s_spiffs_md5.trim();
    s_spiffs_md5.toLowerCase();
  }

  JsonArray assets = doc["assets"];
  for (JsonObject asset : assets) {
    String name = asset["name"].as<String>();
    if (name == String(FIRMWARE_ASSET)) {
      s_firmware_url = asset["browser_download_url"].as<String>();
    } else if (name == String(SPIFFS_ASSET)) {
      s_spiffs_url = asset["browser_download_url"].as<String>();
    }
  }

  // Compare tag (e.g. "v1.0.1") not the free-form release title
  bool different = (s_latest_tag != String(OTA_VERSION));
  bool hasAsset  = !s_firmware_url.isEmpty();
  return different && hasAsset;
}

static bool flashPartition(const String& url, const String& md5, int command) {
  WiFiClientSecure secure;
  secure.setInsecure();

  HTTPClient http;
  http.begin(secure, url);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  int code = http.GET();
  if (code != 200) {
    log_e("Download returned %d", code);
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  if (contentLength <= 0) {
    log_e("Invalid Content-Length: %d", contentLength);
    http.end();
    return false;
  }

  if (!Update.begin(contentLength, command)) {
    log_e("Not enough space for update, error: %d", Update.getError());
    http.end();
    return false;
  }

  if (!md5.isEmpty()) {
    Update.setMD5(md5.c_str());
    log_i("MD5 verification enabled: %s", md5.c_str());
  } else {
    log_w("No MD5 in release notes — skipping integrity check");
  }

  Update.onProgress([](size_t written, size_t total) {
    log_i("OTA progress: %u / %u (%u%%)", written, total, (written * 100) / total);
  });

  WiFiClient* stream = http.getStreamPtr();
  size_t written = Update.writeStream(*stream);
  http.end();

  if (written != (size_t)contentLength) {
    log_e("OTA write mismatch: wrote %u of %d", written, contentLength);
    return false;
  }

  if (!Update.end(true)) {
    log_e("Update.end() failed, error: %d", Update.getError());
    return false;
  }

  return true;
}

// Backs up all user JSON files, flashes the new SPIFFS image, then restores them.
// This preserves firing programs, WiFi credentials, and InfluxDB settings.
//
// Two-phase approach: collect paths first (iterator only), then reopen each file
// by explicit path. This avoids an ESP32 SPIFFS bug where f.size() returns 0 for
// files opened via openNextFile(), which would produce empty backups.
static bool preserveAndFlashSPIFFS() {
  struct SavedFile { String path; std::vector<uint8_t> data; };

  // Phase 1: collect JSON file paths (do not read data here — size() is unreliable)
  std::vector<String> jsonPaths;
  {
    File root = SPIFFS.open("/");
    if (root) {
      File f = root.openNextFile();
      while (f) {
        if (!f.isDirectory()) {
          String name = f.name();
          if (!name.startsWith("/")) name = "/" + name;
          if (name.endsWith(".json")) jsonPaths.push_back(name);
        }
        f.close();
        f = root.openNextFile();
      }
      root.close();
    }
  }

  // Phase 2: read each file by explicit open so size() is accurate
  std::vector<SavedFile> preserved;
  for (const String& path : jsonPaths) {
    File f = SPIFFS.open(path, FILE_READ);
    if (!f) { log_w("Cannot open %s for backup", path.c_str()); continue; }
    size_t fsize = f.size();
    if (fsize == 0) { log_w("Skipping %s — 0 bytes", path.c_str()); f.close(); continue; }
    SavedFile sf;
    sf.path = path;
    sf.data.resize(fsize);
    size_t got = f.read(sf.data.data(), fsize);
    f.close();
    if (got == fsize) {
      preserved.push_back(std::move(sf));
      log_i("Preserving %s (%u bytes)", path.c_str(), got);
    } else {
      log_w("Read mismatch %s: got %u of %u bytes — skipping", path.c_str(), got, fsize);
    }
  }

  if (!flashPartition(s_spiffs_url, s_spiffs_md5, U_SPIFFS)) return false;

  SPIFFS.end();
  if (!SPIFFS.begin(true)) {
    log_e("Failed to remount SPIFFS after flash");
    return false;
  }

  log_i("SPIFFS free after flash: %u / %u bytes",
        SPIFFS.totalBytes() - SPIFFS.usedBytes(), SPIFFS.totalBytes());

  bool anyRestoreFailed = false;
  for (auto& sf : preserved) {
    File out = SPIFFS.open(sf.path, FILE_WRITE);
    if (!out) {
      log_e("Cannot open %s for restore — user data lost", sf.path.c_str());
      anyRestoreFailed = true;
      continue;
    }
    size_t written = out.write(sf.data.data(), sf.data.size());
    out.close();
    if (written == sf.data.size()) {
      log_i("Restored %s (%u bytes)", sf.path.c_str(), written);
    } else {
      log_e("Write mismatch %s: wrote %u of %u bytes — user data may be lost",
            sf.path.c_str(), written, sf.data.size());
      anyRestoreFailed = true;
    }
  }
  if (anyRestoreFailed) log_e("One or more user data files were NOT fully restored");
  return true;
}

static bool performUpdate() {
  log_i("Flashing firmware…");
  if (!flashPartition(s_firmware_url, s_firmware_md5, U_FLASH)) {
    return false;
  }

  if (!s_spiffs_url.isEmpty()) {
    // Only flash SPIFFS if its version differs from the current one to avoid
    // unnecessary flashes (and the associated preserve/restore cycle).
    String currentSpiffsVersion;
    File vf = SPIFFS.open("/spiffs_version.txt", FILE_READ);
    if (vf) { currentSpiffsVersion = vf.readString(); currentSpiffsVersion.trim(); vf.close(); }

    if (currentSpiffsVersion == s_latest_tag) {
      log_i("SPIFFS already at version %s — skipping flash", s_latest_tag.c_str());
    } else {
      log_i("Flashing SPIFFS (current: %s → new: %s)…", currentSpiffsVersion.c_str(), s_latest_tag.c_str());
      if (!preserveAndFlashSPIFFS()) {
        log_w("SPIFFS flash failed — device will restart with new firmware only");
      }
    }
  }

  return true;
}

void ota_task(void* parameter) {
  while (1) {
    OtaStatus status;
    xSemaphoreTake(mutex, portMAX_DELAY);
    status = g_ota_status;
    xSemaphoreGive(mutex);

    if (status == OtaStatus::CHECKING) {
      bool available = checkForUpdate();

      xSemaphoreTake(mutex, portMAX_DELAY);
      g_ota_latest_version = s_latest_version;
      g_ota_latest_tag     = s_latest_tag;
      g_ota_status = available ? OtaStatus::UPDATE_AVAILABLE : OtaStatus::UP_TO_DATE;
      if (!available && s_latest_version.isEmpty()) g_ota_status = OtaStatus::ERROR;
      xSemaphoreGive(mutex);
    }

    else if (status == OtaStatus::UPDATING) {
      bool success = performUpdate();
      if (success) {
        log_i("OTA complete — restarting");
        SPIFFS.end();  // flush write cache before hard reset
        ESP.restart();
      } else {
        xSemaphoreTake(mutex, portMAX_DELAY);
        g_ota_status = OtaStatus::ERROR;
        xSemaphoreGive(mutex);
      }
    }

    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}
