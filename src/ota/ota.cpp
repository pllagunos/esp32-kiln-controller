#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>

#include "common.h"
#include "ota.h"

#ifndef OTA_VERSION
  #define OTA_VERSION "local_development"
#endif

static const char* GITHUB_OWNER   = "pllagunos";
static const char* GITHUB_REPO    = "esp32-kiln-controller";
static const char* FIRMWARE_ASSET = "esp32doit-devkit-v1_firmware.bin";

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

static bool checkForUpdate() {
  // Clear stale state so any failure leaves them empty (prevents masking errors as UP_TO_DATE)
  s_latest_version = "";
  s_latest_tag     = "";
  s_firmware_url   = "";
  s_firmware_md5   = "";

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

  // Parse MD5 from release body — CI embeds "Firmware MD5: <hash>" so it is fetched
  // over the already-verified api.github.com connection rather than the insecure CDN.
  String body = doc["body"].as<String>();
  int mdxIdx = body.indexOf("Firmware MD5: ");
  if (mdxIdx >= 0) {
    s_firmware_md5 = body.substring(mdxIdx + 14, mdxIdx + 46);
    s_firmware_md5.trim();
    s_firmware_md5.toLowerCase();
  }

  JsonArray assets = doc["assets"];
  for (JsonObject asset : assets) {
    if (asset["name"].as<String>() == String(FIRMWARE_ASSET)) {
      s_firmware_url = asset["browser_download_url"].as<String>();
      break;
    }
  }

  // Compare tag (e.g. "v1.0.1") not the free-form release title
  bool different = (s_latest_tag != String(OTA_VERSION));
  bool hasAsset  = !s_firmware_url.isEmpty();
  return different && hasAsset;
}

static bool performUpdate() {
  // GitHub release assets redirect to objects.githubusercontent.com (different CA chain).
  // The firmware MD5 was fetched from the trusted api.github.com response; calling
  // Update.setMD5() here ensures a tampered or corrupted binary is rejected.
  WiFiClientSecure secure;
  secure.setInsecure();

  HTTPClient http;
  http.begin(secure, s_firmware_url);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  int code = http.GET();
  if (code != 200) {
    log_e("Firmware download returned %d", code);
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  if (contentLength <= 0) {
    log_e("Invalid Content-Length: %d", contentLength);
    http.end();
    return false;
  }

  if (!Update.begin(contentLength, U_FLASH)) {
    log_e("Not enough space for OTA, error: %d", Update.getError());
    http.end();
    return false;
  }

  if (!s_firmware_md5.isEmpty()) {
    Update.setMD5(s_firmware_md5.c_str());
    log_i("MD5 verification enabled: %s", s_firmware_md5.c_str());
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
