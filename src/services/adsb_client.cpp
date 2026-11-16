#include "services/adsb_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#include "config.h"
#include "services/adsb_store.h"
#include "services/http_body_framing.h"

namespace services::adsb {

namespace {

/**
 * Community feeds that all serve the same readsb JSON. The first is the
 * normal one; the others take over when it stops answering.
 */
struct Source {
  const char* name;
  const char* prefix;   // up to the latitude
  const char* lon_sep;  // between latitude and longitude
  const char* dist_sep; // between longitude and distance (nautical miles)
};
constexpr Source kSources[] = {
    {"adsb.fi", "https://opendata.adsb.fi/api/v3/lat/", "/lon/", "/dist/"},
    {"adsb.lol", "https://api.adsb.lol/v2/lat/", "/lon/", "/dist/"},
    {"airplanes.live", "https://api.airplanes.live/v2/point/", "/", "/"},
};
constexpr size_t kSourceCount = sizeof(kSources) / sizeof(kSources[0]);
/** Failed requests in a row before moving to the next feed. */
constexpr uint8_t kFailuresBeforeSwitch = 2;
/** While on a fallback, go back and try the first feed this often. */
constexpr unsigned long kRetryPrimaryMs = 15UL * 60UL * 1000UL;

size_t s_source = 0;
uint8_t s_fail_streak = 0;
unsigned long s_fallback_since_ms = 0;
uint32_t s_last_fetch_ms = 0;
uint32_t s_ok_count = 0;
uint32_t s_fail_count = 0;
int s_last_status = 0;
constexpr float kKmPerNm = 1.852f;
constexpr int kConnectTimeoutMs = 5000;  // TLS handshake needs room
constexpr unsigned long kRequestTimeoutMs = 6000;

constexpr unsigned long kMinFetchSpacingMs = 1100;

PollFn s_poll_fn = nullptr;
TaskHandle_t s_fetch_task = nullptr;

void pollNetwork() {
  if (s_poll_fn != nullptr) {
    s_poll_fn();
  }
}

int performGetWithPoll(HTTPClient& http) {
  http.setConnectTimeout(kConnectTimeoutMs);
  const unsigned long deadline = millis() + kRequestTimeoutMs;
  while (millis() < deadline) {
    pollNetwork();
    const int code = http.GET();
    if (code > 0) {
      return code;
    }
    if (code != HTTPC_ERROR_CONNECTION_REFUSED &&
        code != HTTPC_ERROR_NOT_CONNECTED) {
      return code;
    }
    delay(5);
  }
  return HTTPC_ERROR_READ_TIMEOUT;
}

/**
 * Pumps the socket one byte at a time, in blocks, without buffering the whole
 * response.
 *
 * Each refill runs the network poll callback, which is why HTTPClient's own
 * body readers (getString/writeToStream) can't be used here -- they block
 * without giving the fetch task a chance to poll. That also means they can't
 * de-chunk for us, so the wire image comes out raw and BodyFramer unwraps it.
 *
 * Where the body ends is BodyFramer's business, not this class's: it stops
 * pulling at the right byte. Reading a little past that point into buffer_ is
 * harmless while the connection is torn down after every fetch.
 */
class PollingSocketSource {
 public:
  PollingSocketSource(HTTPClient& http, WiFiClient& stream,
                      unsigned long deadline)
      : http_(&http), stream_(&stream), deadline_(deadline) {}

  /** Next raw byte, or -1 once the socket closes or the deadline passes. */
  int read() {
    if (pos_ >= len_ && !refill()) {
      return -1;
    }
    return static_cast<unsigned char>(buffer_[pos_++]);
  }

 private:
  bool refill() {
    pos_ = 0;
    len_ = 0;
    while (millis() < deadline_) {
      pollNetwork();
      const int available = stream_->available();
      if (available > 0) {
        const int to_read = available > static_cast<int>(sizeof(buffer_))
                                ? static_cast<int>(sizeof(buffer_))
                                : available;
        const int read_bytes = stream_->readBytes(buffer_, to_read);
        if (read_bytes > 0) {
          len_ = static_cast<size_t>(read_bytes);
          return true;
        }
      }
      if (!http_->connected() && stream_->available() <= 0) {
        break;  // server closed and the socket is drained
      }
      delay(1);
    }
    return false;
  }

  HTTPClient* http_;
  WiFiClient* stream_;
  unsigned long deadline_;
  char buffer_[512];
  size_t pos_ = 0;
  size_t len_ = 0;
};

using BodyReader = services::http::BodyFramer<PollingSocketSource>;

float kmToNauticalMiles(float km) { return km / kKmPerNm; }

}  // namespace

void setPollFn(PollFn fn) { s_poll_fn = fn; }

void requestRefresh() {
  if (s_fetch_task != nullptr) {
    xTaskNotifyGive(s_fetch_task);
  }
}

void waitForNextFetch(unsigned long interval_ms) {
  s_fetch_task = xTaskGetCurrentTaskHandle();
  const bool early = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(interval_ms)) != 0;
  if (early) {
    // adsb.fi allows one request per second; never poll faster than that.
    vTaskDelay(pdMS_TO_TICKS(kMinFetchSpacingMs));
    ulTaskNotifyTake(pdTRUE, 0);  // fold requests that arrived meanwhile
  }
}

namespace {

bool fetchFrom(const Source& src, double center_lat, double center_lon,
               float fetch_radius_km, bool military_only);

}  // namespace

FetchStats stats() {
  return FetchStats{kSources[s_source].name, s_last_fetch_ms, s_ok_count, s_fail_count,
                    s_last_status};
}

bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km,
                 bool military_only) {
  if (s_source != 0 && millis() - s_fallback_since_ms > kRetryPrimaryMs) {
    Serial.printf("adsb: trying %s again\n", kSources[0].name);
    s_source = 0;
    s_fail_streak = 0;
  }

  const unsigned long started = millis();
  const bool ok = fetchFrom(kSources[s_source], center_lat, center_lon, fetch_radius_km,
                            military_only);
  s_last_fetch_ms = millis() - started;
  if (ok) {
    ++s_ok_count;
    s_fail_streak = 0;
    return true;
  }
  ++s_fail_count;
  if (++s_fail_streak >= kFailuresBeforeSwitch) {
    s_fail_streak = 0;
    s_source = (s_source + 1) % kSourceCount;
    s_fallback_since_ms = millis();
    Serial.printf("adsb: switching to %s\n", kSources[s_source].name);
  }
  return false;
}

namespace {

bool fetchFrom(const Source& src, double center_lat, double center_lon,
               float fetch_radius_km, bool military_only) {
  const float dist_nm = kmToNauticalMiles(fetch_radius_km);

  s_last_status = 0;
  String url = src.prefix;
  url += String(center_lat, 6);
  url += src.lon_sep;
  url += String(center_lon, 6);
  url += src.dist_sep;
  url += String(dist_nm, 0);

  // Keep only the fields we render; the rest never reaches RAM.
  JsonDocument filter;
  buildFilter(filter);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("adsb: http.begin failed");
    return false;
  }

  // HTTPClient only records Transfer-Encoding in the collected headers when
  // it is asked for up front; _transferEncoding itself is private.
  static const char* kWantedHeaders[] = {"Transfer-Encoding"};
  http.collectHeaders(kWantedHeaders, 1);

  http.setTimeout(kRequestTimeoutMs);
  const int code = performGetWithPoll(http);
  s_last_status = code;
  if (code != HTTP_CODE_OK) {
    Serial.printf("adsb: HTTP %d\n", code);
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) {
    Serial.println("adsb: no response stream");
    http.end();
    return false;
  }

  // On HTTP/1.1 the CDN answers with Transfer-Encoding: chunked, and
  // getStreamPtr() hands back the raw socket -- chunk sizes and all. BodyFramer
  // strips that framing back off.
  const services::http::BodyFraming framing =
      http.header("Transfer-Encoding").equalsIgnoreCase("chunked")
          ? services::http::BodyFraming::kChunked
          : services::http::BodyFraming::kIdentity;

  PollingSocketSource source(http, *stream, millis() + kRequestTimeoutMs);
  BodyReader body(source, framing, http.getSize());
  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));
  // Read off the terminating chunk the parser stopped short of, so the socket
  // sits at the end of the message. Not needed while every fetch builds its own
  // connection, but a prerequisite for ever reusing one.
  body.drain();
  http.end();
  if (err) {
    if (body.framingError()) {
      Serial.println("adsb: malformed chunked body");
    } else if (body.bytesRead() == 0) {
      Serial.println("adsb: empty response");
    } else {
      Serial.printf("adsb: JSON parse error: %s\n", err.c_str());
    }
    return false;
  }

  // Parse into a private buffer, then publish atomically so a reader on
  // another thread never sees a half-updated list. Static: only the single
  // fetch task calls this, and it keeps ~6 KB off that task's stack.
  static Aircraft parsed[kMaxAircraft];
  const size_t n = parseAircraft(doc["ac"].as<JsonArray>(), parsed,
                                 kMaxAircraft, military_only);
  publish(parsed, n);
  Serial.printf("adsb: %u aircraft (%s)\n", static_cast<unsigned>(n), src.name);
  return true;
}

}  // namespace

}  // namespace services::adsb
