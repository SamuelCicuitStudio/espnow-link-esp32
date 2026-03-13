#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "espnow_link/control.hpp"
#include "espnow_link/events.hpp"
#include "espnow_link/firmware.hpp"
#include "espnow_link/hooks.hpp"
#include "espnow_link/persistence.hpp"
#include "espnow_link/security.hpp"
#include "espnow_link/transport.hpp"
#include "espnow_link/time.hpp"
#include "espnow_link/types.hpp"

namespace espnow_link {

// Wrap an existing project transport class that already exposes matching methods.
template <typename TTransport>
class TransportAdapter final : public ITransport {
 public:
  explicit TransportAdapter(TTransport& impl) : impl_(impl) {}

  bool begin(uint8_t channel, bool encrypted) override { return impl_.begin(channel, encrypted); }
  void end() override { impl_.end(); }
  bool addPeer(const MacAddress& mac, bool encrypted, const LmkKey* lmk) override {
    return impl_.addPeer(mac, encrypted, lmk);
  }
  bool removePeer(const MacAddress& mac) override { return impl_.removePeer(mac); }
  bool send(const MacAddress& to, const uint8_t* data, size_t len) override { return impl_.send(to, data, len); }
  bool setChannel(uint8_t channel) override { return impl_.setChannel(channel); }
  uint8_t getChannel() const override { return impl_.getChannel(); }

 private:
  TTransport& impl_;
};

// Wrap an existing project NVS/storage class.
template <typename TStorage>
class PersistenceAdapter final : public IPersistenceBackend {
 public:
  explicit PersistenceAdapter(TStorage& impl) : impl_(impl) {}

  bool putBlob(const std::string& key, const uint8_t* data, size_t len) override {
    return impl_.putBlob(key, data, len);
  }

  bool getBlob(const std::string& key, std::vector<uint8_t>& out) override {
    return impl_.getBlob(key, out);
  }

  bool eraseKey(const std::string& key) override { return impl_.eraseKey(key); }

 private:
  TStorage& impl_;
};

// Wrap an existing event dispatcher.
template <typename TEvents>
class EventSinkAdapter final : public IEventSink {
 public:
  explicit EventSinkAdapter(TEvents& impl) : impl_(impl) {}

  void onEvent(const Event& event) override { impl_.onEvent(event); }

 private:
  TEvents& impl_;
};

// Wrap an existing control dispatcher.
template <typename TControl>
class ControlPlaneAdapter final : public IControlPlane {
 public:
  explicit ControlPlaneAdapter(TControl& impl) : impl_(impl) {}

  bool onPullRequest(const MacAddress& from,
                     uint32_t corr_id,
                     const uint8_t* payload,
                     size_t len) override {
    return impl_.onPullRequest(from, corr_id, payload, len);
  }

  bool onPullResponse(const MacAddress& from,
                      uint32_t corr_id,
                      const uint8_t* payload,
                      size_t len) override {
    return impl_.onPullResponse(from, corr_id, payload, len);
  }

 private:
  TControl& impl_;
};

// Wrap an existing platform-effects class (serial/rgb/channel policy).
template <typename THooks>
class PlatformHooksAdapter final : public IPlatformHooks {
 public:
  explicit PlatformHooksAdapter(THooks& impl) : impl_(impl) {}

  void onRxFrame(const MacAddress& from,
                 MessageType type,
                 uint32_t corr_id,
                 size_t len,
                 int rssi) override {
    impl_.onRxFrame(from, type, corr_id, len, rssi);
  }

  void onTxFrame(const MacAddress& to,
                 MessageType type,
                 uint32_t corr_id,
                 size_t len,
                 bool ok) override {
    impl_.onTxFrame(to, type, corr_id, len, ok);
  }

  void onRgbBlink(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) override {
    impl_.onRgbBlink(r, g, b, ms);
  }

  bool getBootChannel(uint8_t& out_channel) override {
    return getBootChannelImpl(impl_, out_channel, 0);
  }

  bool allowChannelSwitch(uint8_t current_channel, uint8_t requested_channel) override {
    return allowChannelSwitchImpl(impl_, current_channel, requested_channel, 0);
  }

  void onChannelCommitted(uint8_t channel) override {
    onChannelCommittedImpl(impl_, channel, 0);
  }

 private:
  template <typename TImpl>
  static auto getBootChannelImpl(TImpl& impl, uint8_t& out_channel, int)
      -> decltype(impl.getBootChannel(out_channel), bool()) {
    return impl.getBootChannel(out_channel);
  }

  template <typename TImpl>
  static bool getBootChannelImpl(TImpl&, uint8_t&, long) {
    return false;
  }

  template <typename TImpl>
  static auto allowChannelSwitchImpl(TImpl& impl, uint8_t current_channel, uint8_t requested_channel, int)
      -> decltype(impl.allowChannelSwitch(current_channel, requested_channel), bool()) {
    return impl.allowChannelSwitch(current_channel, requested_channel);
  }

  template <typename TImpl>
  static bool allowChannelSwitchImpl(TImpl&, uint8_t, uint8_t, long) {
    return true;
  }

  template <typename TImpl>
  static auto onChannelCommittedImpl(TImpl& impl, uint8_t channel, int)
      -> decltype(impl.onChannelCommitted(channel), void()) {
    impl.onChannelCommitted(channel);
  }

  template <typename TImpl>
  static void onChannelCommittedImpl(TImpl&, uint8_t, long) {}

  THooks& impl_;
};

// Wrap an existing firmware update writer/stream destination.
template <typename TFirmware>
class FirmwareSinkAdapter final : public IFirmwareStreamSink {
 public:
  explicit FirmwareSinkAdapter(TFirmware& impl) : impl_(impl) {}

  bool begin(const MacAddress& from,
             uint32_t corr_id,
             uint32_t total_size,
             uint32_t chunk_size,
             uint32_t image_crc32) override {
    return impl_.begin(from, corr_id, total_size, chunk_size, image_crc32);
  }

  bool writeChunk(const MacAddress& from,
                  uint32_t corr_id,
                  uint32_t offset,
                  const uint8_t* data,
                  size_t len) override {
    return impl_.writeChunk(from, corr_id, offset, data, len);
  }

  bool end(const MacAddress& from,
           uint32_t corr_id,
           uint32_t total_size,
           uint32_t image_crc32) override {
    return impl_.end(from, corr_id, total_size, image_crc32);
  }

  void abort(const MacAddress& from, uint32_t corr_id) override { impl_.abort(from, corr_id); }

 private:
  TFirmware& impl_;
};


// Wrap an existing time source (master clock or external RTC).
template <typename TTimeSource>
class TimeSourceAdapter final : public ITimeSource {
 public:
  explicit TimeSourceAdapter(TTimeSource& impl) : impl_(impl) {}

  bool nowEpochSec(uint64_t& out_epoch_s) override { return impl_.nowEpochSec(out_epoch_s); }

 private:
  TTimeSource& impl_;
};

// Wrap an existing time sink (typically slave RTC set operation).
template <typename TTimeSink>
class TimeSinkAdapter final : public ITimeSink {
 public:
  explicit TimeSinkAdapter(TTimeSink& impl) : impl_(impl) {}

  bool setEpochSec(uint64_t epoch_s) override { return impl_.setEpochSec(epoch_s); }

 private:
  TTimeSink& impl_;
};
}  // namespace espnow_link


