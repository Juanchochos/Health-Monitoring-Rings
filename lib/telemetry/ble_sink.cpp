#include "ble_sink.h"

#include <bluefruit.h>
#include <math.h>

#include "app_config.h"

namespace {

// Base 4f9a0000-7c1e-4b6d-9a2f-3e8c5d1b7a60, little-endian; byte 12 is the
// 16-bit short id.
#define CARDIO_UUID(id) \
  {0x60, 0x7a, 0x1b, 0x5d, 0x8c, 0x3e, 0x2f, 0x9a, 0x6d, 0x4b, 0x1e, 0x7c, (id), 0x00, 0x9a, 0x4f}

const uint8_t kUuidService[16] = CARDIO_UUID(0x01);
const uint8_t kUuidBeat[16] = CARDIO_UUID(0x02);
const uint8_t kUuidStatus[16] = CARDIO_UUID(0x03);
const uint8_t kUuidRaw[16] = CARDIO_UUID(0x04);

constexpr uint8_t kBodySensorFinger = 3;
constexpr uint16_t kRawMtu = 247;
constexpr uint32_t kHrmKeepaliveMs = 2000;

BLEService g_hrs(UUID16_SVC_HEART_RATE);
BLECharacteristic g_hrm(UUID16_CHR_HEART_RATE_MEASUREMENT);
BLECharacteristic g_bsl(UUID16_CHR_BODY_SENSOR_LOCATION);

BLEService g_plx(UUID16_SVC_PLX);
BLECharacteristic g_plx_cont(UUID16_CHR_PLX_CONTINUOUS_MEAS);
BLECharacteristic g_plx_feat(UUID16_CHR_PLX_FEATURES);

BLEService g_cardio(kUuidService);
BLECharacteristic g_beat(kUuidBeat);
BLECharacteristic g_status(kUuidStatus);
BLECharacteristic g_raw(kUuidRaw);

volatile uint16_t g_conn_hdl = BLE_CONN_HANDLE_INVALID;
bool g_raw_enabled = false;

void on_connect(uint16_t conn_hdl)
{
  g_conn_hdl = conn_hdl;
  if (g_raw_enabled) {
    BLEConnection *conn = Bluefruit.Connection(conn_hdl);
    conn->requestPHY(BLE_GAP_PHY_2MBPS);
    conn->requestDataLengthUpdate();
    conn->requestMtuExchange(kRawMtu);
  }
}

void on_disconnect(uint16_t conn_hdl, uint8_t reason)
{
  (void)reason;
  if (conn_hdl == g_conn_hdl) {
    g_conn_hdl = BLE_CONN_HANDLE_INVALID;
  }
}

void setup_notify_chr(BLECharacteristic &c, uint8_t props, uint16_t len, bool fixed)
{
  c.setProperties(props);
  c.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  if (fixed) {
    c.setFixedLen(len);
  } else {
    c.setMaxLen(len);
  }
  c.begin();
}

}  // namespace

bool BleSink::begin()
{
  g_raw_enabled = raw_enabled_;
  if (raw_enabled_) {
    Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  }
  if (!Bluefruit.begin()) {
    return false;
  }
  Bluefruit.autoConnLed(false);  // LED_BLUE is not a status LED on the ring
  Bluefruit.setName(BLE_DEVICE_NAME);
  Bluefruit.Periph.setConnectCallback(on_connect);
  Bluefruit.Periph.setDisconnectCallback(on_disconnect);

  g_hrs.begin();
  setup_notify_chr(g_hrm, CHR_PROPS_NOTIFY, BLE_HRM_MAX_LEN, false);
  setup_notify_chr(g_bsl, CHR_PROPS_READ, 1, true);
  g_bsl.write8(kBodySensorFinger);

  g_plx.begin();
  setup_notify_chr(g_plx_cont, CHR_PROPS_NOTIFY, BLE_PLX_CONT_LEN, true);
  setup_notify_chr(g_plx_feat, CHR_PROPS_READ, BLE_PLX_FEATURES_LEN, true);
  uint8_t feat[BLE_PLX_FEATURES_LEN];
  ble_plx_features_encode(feat);
  g_plx_feat.write(feat, sizeof(feat));

  g_cardio.begin();
  setup_notify_chr(g_beat, CHR_PROPS_READ | CHR_PROPS_NOTIFY, BLE_BEAT_LEN, true);
  setup_notify_chr(g_status, CHR_PROPS_READ | CHR_PROPS_NOTIFY, BLE_STATUS_LEN, true);
  if (raw_enabled_) {
    setup_notify_chr(g_raw, CHR_PROPS_NOTIFY, kRawMtu - 3, false);
  }

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(g_hrs, g_plx);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);  // 20 ms fast, 152.5 ms slow (0.625 ms units)
  Bluefruit.Advertising.setFastTimeout(30);
  return Bluefruit.Advertising.start(0);
}

void BleSink::notify_hrm(uint16_t hr_bpm, bool contact, uint32_t rr_us)
{
  if (!g_hrm.notifyEnabled()) {
    return;
  }
  uint8_t buf[BLE_HRM_MAX_LEN];
  const uint16_t rr = ble_rr_to_1024(rr_us);
  const size_t len = ble_hrm_encode(buf, hr_bpm, contact, &rr, rr_us ? 1 : 0);
  if (g_hrm.notify(buf, (uint16_t)len)) {
    last_hrm_ms_ = millis();
  }
}

void BleSink::publish_vitals(const vitals_t &v)
{
  last_vitals_ = v;
  if (g_conn_hdl == BLE_CONN_HANDLE_INVALID) {
    return;
  }
  if (g_plx_cont.notifyEnabled()) {
    uint8_t buf[BLE_PLX_CONT_LEN];
    ble_plx_continuous_encode(buf, &v);
    g_plx_cont.notify(buf, sizeof(buf));
  }
  // Without beats (no contact, warming up) keep the HR view current.
  if (millis() - last_hrm_ms_ >= kHrmKeepaliveMs) {
    const bool hr_ok = (v.flags & VITALS_FLAG_HR_VALID) != 0;
    notify_hrm(hr_ok ? (uint16_t)lroundf(v.hr_bpm) : 0,
               (v.flags & VITALS_FLAG_ECG_CONTACT) != 0, 0);
  }
}

void BleSink::publish_beat(const beat_event_t &b)
{
  uint8_t buf[BLE_BEAT_LEN];
  ble_beat_encode(buf, &b);
  g_beat.write(buf, sizeof(buf));
  if (g_conn_hdl == BLE_CONN_HANDLE_INVALID) {
    return;
  }
  if (g_beat.notifyEnabled()) {
    g_beat.notify(buf, sizeof(buf));
  }
  if (b.flags & VITALS_FLAG_HR_VALID) {
    const float hr = (last_vitals_.flags & VITALS_FLAG_HR_VALID)
                         ? last_vitals_.hr_bpm
                         : (b.rr_us ? 60e6f / (float)b.rr_us : 0.0f);
    notify_hrm((uint16_t)lroundf(hr), (b.flags & VITALS_FLAG_ECG_CONTACT) != 0, b.rr_us);
  }
}

void BleSink::publish_status(const telemetry_status_t &s)
{
  uint8_t buf[BLE_STATUS_LEN];
  ble_status_encode(buf, &s);
  g_status.write(buf, sizeof(buf));
  if (g_conn_hdl != BLE_CONN_HANDLE_INVALID && g_status.notifyEnabled()) {
    g_status.notify(buf, sizeof(buf));
  }
}

bool BleSink::wants_raw() const
{
  return raw_enabled_ && g_conn_hdl != BLE_CONN_HANDLE_INVALID && g_raw.notifyEnabled();
}

void BleSink::publish_raw(const raw_record_t &r)
{
  const uint16_t hdl = g_conn_hdl;
  if (hdl == BLE_CONN_HANDLE_INVALID) {
    raw_count_ = 0;
    return;
  }
  BLEConnection *conn = Bluefruit.Connection(hdl);
  const size_t per_packet = conn ? ble_raw_records_per_packet(conn->getMtu() - 3) : 0;
  // A 23-byte MTU would need one notification per sample; wait for the
  // larger MTU requested on connect instead.
  if (per_packet < 4) {
    return;
  }
  raw_pending_[raw_count_++] = r;
  if (raw_count_ >= per_packet || raw_count_ >= kRawMaxRecords) {
    uint8_t buf[1 + kRawMaxRecords * BLE_RAW_RECORD_LEN];
    const size_t len = ble_raw_encode(buf, raw_pending_, raw_count_);
    g_raw.notify(buf, (uint16_t)len);
    raw_count_ = 0;
  }
}
