#include "HalRadio.h"

#if defined(CROSSMUX_LORA_CHAT) && CROSSMUX_LORA_CHAT && defined(FREEINK_DEVICE_PAPERMONO) && !defined(SIMULATOR)
#include <Arduino.h>
#include <Logging.h>
#include <PaperMonoBoard.h>
#include <RadioLib.h>
#include <SPI.h>
#include <hal/Arduino/ArduinoHal.h>

namespace {
// Fixed single-owner objects: RadioLib receives an existing HAL/Module so its
// convenience constructors do not allocate their Arduino HAL with bare new.
SPIClass radioSpi(HSPI);
ArduinoHal radioHal(radioSpi, SPISettings(8000000, MSBFIRST, SPI_MODE0));
Module radioModule(&radioHal, 41, 5, RADIOLIB_NC, 21);
SX1262 radio(&radioModule);
constexpr uint32_t TX_TIMEOUT_MS = 5000;
bool receive() { return freeink::papermono::setRadioTransmit(false) && radio.startReceive() == RADIOLIB_ERR_NONE; }
}  // namespace

bool HalRadio::supported() { return true; }
uint64_t HalRadio::deviceId() { return ESP.getEfuseMac(); }
uint32_t HalRadio::randomNumber() { return esp_random(); }
bool HalRadio::begin() {
  if (started_) return true;
  if (!freeink::papermono::setRadioPower(true)) {
    freeink::papermono::setRadioPower(false);
    return false;
  }
  radioSpi.begin(39, 40, 38, 41);
  // Short-message profile. Both peers must use this profile. Power is kept
  // conservative for initial bench tests; frequency is an explicit build option.
#ifndef CROSSMUX_RF_FREQUENCY_MHZ
#define CROSSMUX_RF_FREQUENCY_MHZ 868.0f
#endif
  const int result = radio.begin(CROSSMUX_RF_FREQUENCY_MHZ, 125.0f, 9, 5, 0x12, 10, 12, 3.0f, true);
  started_ = result == RADIOLIB_ERR_NONE;
  if (!started_ || radio.setCurrentLimit(140) != RADIOLIB_ERR_NONE || !receive()) {
    LOG_ERR("RF", "Radio initialization failed: %d", result);
    end();
    return false;
  }
  LOG_INF("RF", "SX1262 ready: %.1f MHz / BW125 / SF9 / CR4:5 / 10 dBm", CROSSMUX_RF_FREQUENCY_MHZ);
  return true;
}
void HalRadio::end() {
  if (started_) radio.sleep();
  radioSpi.end();
  freeink::papermono::setRadioPower(false);
  transmitting_ = started_ = false;
}
bool HalRadio::send(const uint8_t* data, size_t length) {
  if (!started_ || transmitting_ || !data || !length || length > 255) return false;
  if (radio.standby() != RADIOLIB_ERR_NONE || !freeink::papermono::setRadioTransmit(true) ||
      radio.startTransmit(data, length) != RADIOLIB_ERR_NONE) {
    receive();
    return false;
  }
  transmitting_ = true;
  sentAt_ = millis();
  return true;
}
HalRadio::Result HalRadio::poll(uint8_t* data, size_t capacity, size_t& length, int16_t& rssi) {
  length = 0;
  if (!started_) return Result::Idle;
  // Poll the level-triggered IRQ every 10 ms. No ISR allocation, SPI or I2C.
  if (transmitting_) {
    if (!digitalRead(5) && millis() - sentAt_ < TX_TIMEOUT_MS) return Result::Idle;
    const bool finished = digitalRead(5);
    radio.finishTransmit();
    transmitting_ = false;
    return receive() && finished ? Result::Sent : Result::Failed;
  }
  if (!digitalRead(5)) return Result::Idle;
  const size_t available = radio.getPacketLength();
  bool ok = available > 0 && available <= capacity;
  if (ok) {
    ok = radio.readData(data, available) == RADIOLIB_ERR_NONE;
    rssi = static_cast<int16_t>(radio.getRSSI());
    if (ok) length = available;
  }
  if (!receive()) return Result::Failed;
  return ok ? Result::Received : Result::Idle;
}
#else
bool HalRadio::supported() { return false; }
uint64_t HalRadio::deviceId() { return 0; }
uint32_t HalRadio::randomNumber() { return 0; }
bool HalRadio::begin() { return false; }
void HalRadio::end() {}
bool HalRadio::send(const uint8_t*, size_t) { return false; }
HalRadio::Result HalRadio::poll(uint8_t*, size_t, size_t& length, int16_t&) {
  length = 0;
  return Result::Idle;
}
#endif
