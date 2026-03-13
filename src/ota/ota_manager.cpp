#include "espnow_link/ota_manager.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>

#if defined(ARDUINO)
#include <Arduino.h>
#include <esp_heap_caps.h>
#endif

namespace espnow_link {

namespace {

constexpr uint32_t kCrc32Poly = 0xEDB88320U;
constexpr uint32_t kFnvOffset = 2166136261U;
constexpr uint32_t kFnvPrime = 16777619U;

const char* gateDecisionMessage(OtaGateDecision d) {
  switch (d) {
    case OtaGateDecision::Ready:
      return "ready";
    case OtaGateDecision::Denied:
      return "denied";
    case OtaGateDecision::Busy:
      return "busy";
    case OtaGateDecision::PrepFailed:
      return "prep_failed";
    default:
      return "unknown";
  }
}

}  // namespace

OtaManager::OtaManager(IOtaStorageBackend& storage,
                       const OtaManagerConfig& config,
                       IOtaUpdateGate* gate,
                       IOtaApplyExecutor* apply_executor)
    : storage_(storage), config_(config), gate_(gate), apply_executor_(apply_executor) {
  config_.root_path = normalizeDir_(config_.root_path);
  config_.in_dir = normalizeDir_(config_.in_dir);
  config_.stg_dir = normalizeDir_(config_.stg_dir);
  config_.img_dir = normalizeDir_(config_.img_dir);
  config_.man_dir = normalizeDir_(config_.man_dir);
  config_.st_dir = normalizeDir_(config_.st_dir);
}

OtaManager::~OtaManager() {
  releasePsramPool_();
}

bool OtaManager::begin(std::string& out_message) {
  std::string msg;
  if (!storage_.begin(msg)) {
    return fail_(OtaStatusCode::StorageNotReady, msg.empty() ? "ota storage begin failed" : msg, out_message);
  }
  if (!ensureFolders_(msg)) {
    return fail_(OtaStatusCode::StorageNotReady, msg.empty() ? "ota folders unavailable" : msg, out_message);
  }
  // One-slave single-slot model: reclaim OTA workspace before each receive.
  if (!clearDirContents_(config_.in_dir, msg)) {
    return fail_(OtaStatusCode::StorageNotReady, msg.empty() ? "ota in clear failed" : msg, out_message);
  }
  if (!clearDirContents_(config_.img_dir, msg)) {
    return fail_(OtaStatusCode::StorageNotReady, msg.empty() ? "ota img clear failed" : msg, out_message);
  }
  if (!clearDirContents_(config_.man_dir, msg)) {
    return fail_(OtaStatusCode::StorageNotReady, msg.empty() ? "ota man clear failed" : msg, out_message);
  }
  if (!ensurePsramPool_(msg)) {
    return fail_(OtaStatusCode::StorageNotReady, msg.empty() ? "ota psram pool unavailable" : msg, out_message);
  }

  PersistentStatusRecord persisted{};
  if (!loadStatusRecord_(persisted, msg)) {
    return fail_(OtaStatusCode::StorageNotReady,
                 msg.empty() ? "ota status record read failed" : msg,
                 out_message);
  }
  applyStatusRecordToRuntime_(persisted);

  std::string ready_msg = "ota ready";
  if (persisted.state == "pending_boot") {
    ready_msg = "ota ready (pending boot confirmation)";
  } else if (persisted.state == "boot_ok" && !persisted.confirmed_sw_version.empty()) {
    ready_msg = "ota ready (last update confirmed)";
  }
  setOk_(ready_msg, out_message);
  return true;
}

void OtaManager::tick() {
  expireReceiveIfStale_();
}

void OtaManager::setMaxFirmwareBytes(uint32_t max_bytes) {
  if (config_.max_firmware_bytes != max_bytes) {
    releasePsramPool_();
  }
  config_.max_firmware_bytes = max_bytes;
}

uint32_t OtaManager::maxFirmwareBytes() const {
  return config_.max_firmware_bytes;
}

void OtaManager::setUpdateGate(IOtaUpdateGate* gate) {
  gate_ = gate;
}

void OtaManager::setApplyExecutor(IOtaApplyExecutor* apply_executor) {
  apply_executor_ = apply_executor;
}

const OtaRuntimeStatus& OtaManager::status() const {
  return status_;
}

uint32_t OtaManager::contiguousReceiveSize() const {
  return receive_.active ? receive_.received_size : 0U;
}

bool OtaManager::beginReceive(const MacAddress& from,
                              uint32_t corr_id,
                              uint32_t total_size,
                              uint32_t chunk_size,
                              uint32_t image_crc32,
                              const FirmwareImageMetadata* metadata,
                              std::string& out_message) {
  expireReceiveIfStale_();

  if (total_size == 0 || chunk_size == 0) {
    return fail_(OtaStatusCode::InvalidArgument, "ota begin invalid size", out_message);
  }
  if (config_.max_firmware_bytes > 0 && total_size > config_.max_firmware_bytes) {
    return fail_(OtaStatusCode::ImageTooLarge, "image too large", out_message);
  }

  if (gate_ != nullptr) {
    const OtaGateResult gate = gate_->prepareForTransferWithMetadata(total_size, image_crc32, metadata);
    if (gate.decision != OtaGateDecision::Ready) {
      OtaStatusCode code = OtaStatusCode::GateDenied;
      if (gate.decision == OtaGateDecision::Busy) {
        code = OtaStatusCode::GateBusy;
      } else if (gate.decision == OtaGateDecision::PrepFailed) {
        code = OtaStatusCode::GatePrepFailed;
      }
      std::string msg = gate.message.empty() ? gateDecisionMessage(gate.decision) : gate.message;
      return fail_(code, msg, out_message);
    }
  }

  std::string msg;
  if (!ensureFolders_(msg)) {
    return fail_(OtaStatusCode::StorageNotReady, msg.empty() ? "ota folders unavailable" : msg, out_message);
  }
  if (!ensurePsramPool_(msg)) {
    return fail_(OtaStatusCode::StorageNotReady, msg.empty() ? "ota psram pool unavailable" : msg, out_message);
  }
  if (psram_pool_ == nullptr || psram_pool_size_ < total_size) {
    return fail_(OtaStatusCode::StorageNotReady, "ota psram pool too small", out_message);
  }

  if (receive_.active) {
    abortReceive(receive_.from, receive_.corr_id, nullptr);
  }

  receive_ = ReceiveSession{};
  receive_meta_ = FirmwareImageMetadata{};
  receive_.active = true;
  receive_.from = from;
  receive_.corr_id = corr_id;
  receive_.expected_size = total_size;
  receive_.expected_crc32 = image_crc32;
  receive_.chunk_size = chunk_size;
  receive_.received_size = 0;
  receive_.unique_received_size = 0;
  receive_.running_crc32 = 0xFFFFFFFFU;
  receive_.last_activity_epoch_s = nowEpochSec_();
  receive_.chunk_count = (total_size + chunk_size - 1U) / chunk_size;
  receive_.chunk_bitmap.assign((receive_.chunk_count + 7U) / 8U, 0U);
  if (metadata != nullptr) {
    receive_meta_ = *metadata;
  }

  receive_.temp_path = joinPath_(config_.in_dir, "u.tmp");
  receive_.final_path = joinPath_(config_.img_dir, "u.bin");

  (void)storage_.removePath(receive_.temp_path, msg);
  // Preflight writable probe so begin can fail early instead of failing after full transfer.
  if (!storage_.truncateFile(receive_.temp_path, msg)) {
    receive_ = ReceiveSession{};
    receive_meta_ = FirmwareImageMetadata{};
    return fail_(OtaStatusCode::StorageNotReady, msg.empty() ? "create temp failed" : msg, out_message);
  }
  (void)storage_.removePath(receive_.temp_path, msg);

  status_.state = OtaTransferState::Receiving;
  status_.code = OtaStatusCode::Ok;
  status_.expected_size = total_size;
  status_.received_size = 0;
  status_.expected_crc32 = image_crc32;
  status_.actual_crc32 = 0;
  status_.temp_path = receive_.temp_path;  // planned finalize target
  status_.image_path.clear();
  status_.message = "ota receiving";
  out_message = status_.message;
  return true;
}

bool OtaManager::writeReceiveChunk(const MacAddress& from,
                                   uint32_t corr_id,
                                   uint32_t offset,
                                   const uint8_t* data,
                                   size_t len,
                                   std::string& out_message) {
  expireReceiveIfStale_();
  if (!validateSession_(from, corr_id, out_message)) {
    return false;
  }
  if (data == nullptr || len == 0) {
    return fail_(OtaStatusCode::InvalidArgument, "ota chunk empty", out_message);
  }
  if (psram_pool_ == nullptr || psram_pool_size_ < receive_.expected_size) {
    return fail_(OtaStatusCode::StorageNotReady, "ota psram pool unavailable", out_message);
  }
  if (offset >= receive_.expected_size) {
    return fail_(OtaStatusCode::SizeMismatch, "ota chunk offset out of range", out_message);
  }
  if ((offset + static_cast<uint32_t>(len)) > receive_.expected_size) {
    return fail_(OtaStatusCode::SizeMismatch, "ota chunk exceeds declared size", out_message);
  }
  if (receive_.chunk_size == 0U || (offset % receive_.chunk_size) != 0U) {
    return fail_(OtaStatusCode::InvalidArgument, "ota chunk offset alignment invalid", out_message);
  }

  const uint32_t chunk_index = offset / receive_.chunk_size;
  if (chunk_index >= receive_.chunk_count) {
    return fail_(OtaStatusCode::InvalidArgument, "ota chunk index out of range", out_message);
  }
  const uint32_t expected_len = chunkSizeAt_(chunk_index);
  if (len != static_cast<size_t>(expected_len)) {
    return fail_(OtaStatusCode::SizeMismatch, "ota chunk size mismatch", out_message);
  }

  const bool already_received = isChunkReceived_(chunk_index);
  std::memcpy(psram_pool_ + offset, data, len);
  if (!already_received) {
    (void)markChunkReceived_(chunk_index);
    receive_.unique_received_size += static_cast<uint32_t>(len);
  }
  refreshContiguousReceived_();

  receive_.last_activity_epoch_s = nowEpochSec_();

  status_.state = OtaTransferState::Receiving;
  status_.code = OtaStatusCode::Ok;
  status_.received_size = receive_.received_size;
  out_message = already_received ? "ota chunk duplicate" : "ota chunk accepted";
  return true;
}

bool OtaManager::endReceive(const MacAddress& from,
                            uint32_t corr_id,
                            uint32_t total_size,
                            uint32_t image_crc32,
                            std::string& out_image_path,
                            std::string& out_message) {
  out_image_path.clear();
  expireReceiveIfStale_();
  if (!validateSession_(from, corr_id, out_message)) {
    return false;
  }

  if (total_size != receive_.expected_size || image_crc32 != receive_.expected_crc32) {
    return fail_(OtaStatusCode::SizeMismatch, "ota end metadata mismatch", out_message);
  }
  if (psram_pool_ == nullptr || psram_pool_size_ < receive_.expected_size) {
    return fail_(OtaStatusCode::StorageNotReady, "ota psram pool unavailable", out_message);
  }
  if (receive_.received_size != receive_.expected_size ||
      receive_.unique_received_size != receive_.expected_size) {
    return fail_(OtaStatusCode::SizeMismatch, "ota total size mismatch", out_message);
  }

  const uint32_t crc = crc32Finalize_(crc32Update_(0xFFFFFFFFU, psram_pool_, receive_.expected_size));
  status_.actual_crc32 = crc;
  if (crc != receive_.expected_crc32) {
    std::string msg;
    (void)storage_.removePath(receive_.temp_path, msg);
    receive_ = ReceiveSession{};
    receive_meta_ = FirmwareImageMetadata{};
    return fail_(OtaStatusCode::CrcMismatch, "ota crc mismatch", out_message);
  }

  std::string msg;
  // Free previous ready image first so SPIFFS does not need old+new image space simultaneously.
  (void)storage_.removePath(receive_.final_path, msg);
  const std::string final_name = fileNameFromPath_(receive_.final_path);
  if (!final_name.empty()) {
    const std::string manifest_path = joinPath_(config_.man_dir, manifestNameForImage_(final_name));
    (void)storage_.removePath(manifest_path, msg);
  }
  (void)storage_.removePath(receive_.temp_path, msg);
  if (!storage_.truncateFile(receive_.temp_path, msg)) {
    receive_ = ReceiveSession{};
    receive_meta_ = FirmwareImageMetadata{};
    return fail_(OtaStatusCode::StorageNotReady, msg.empty() ? "create temp failed" : msg, out_message);
  }
  constexpr uint32_t kFlushBlockBytes = 4096U;
  uint32_t written = 0U;
  while (written < receive_.expected_size) {
    const uint32_t n = std::min<uint32_t>(kFlushBlockBytes, receive_.expected_size - written);
    if (!storage_.writeAt(receive_.temp_path, written, psram_pool_ + written, static_cast<size_t>(n), msg)) {
      receive_ = ReceiveSession{};
      receive_meta_ = FirmwareImageMetadata{};
      return fail_(OtaStatusCode::StorageNotReady, msg.empty() ? "ota flush failed" : msg, out_message);
    }
    written += n;
#if defined(ARDUINO)
    yield();
#endif
  }
  if (!storage_.renamePath(receive_.temp_path, receive_.final_path, msg)) {
    receive_ = ReceiveSession{};
    receive_meta_ = FirmwareImageMetadata{};
    return fail_(OtaStatusCode::StorageNotReady, msg.empty() ? "ota promote failed" : msg, out_message);
  }

  out_image_path = receive_.final_path;
  const FirmwareImageMetadata persisted_meta = receive_meta_;
  receive_ = ReceiveSession{};
  receive_meta_ = FirmwareImageMetadata{};

  std::string manifest_msg;
  const bool manifest_ok = writeReadyManifest_(out_image_path, total_size, crc, persisted_meta, manifest_msg);
  const std::string final_msg =
      manifest_ok ? std::string("ota image ready")
                  : std::string("ota image ready (manifest pending: ") +
                        (manifest_msg.empty() ? std::string("write failed") : manifest_msg) + ")";

  status_.state = OtaTransferState::Ready;
  status_.code = OtaStatusCode::Ok;
  status_.received_size = total_size;
  status_.image_path = out_image_path;
  status_.temp_path.clear();
  status_.message = final_msg;
  out_message = status_.message;
  return true;
}

void OtaManager::abortReceive(const MacAddress& from, uint32_t corr_id, std::string* out_message) {
  if (!receive_.active || receive_.from != from || receive_.corr_id != corr_id) {
    if (out_message != nullptr) {
      *out_message = "ota no active transfer";
    }
    return;
  }
  std::string msg;
  (void)storage_.removePath(receive_.temp_path, msg);
  receive_ = ReceiveSession{};
  receive_meta_ = FirmwareImageMetadata{};
  status_.state = OtaTransferState::Failed;
  status_.code = OtaStatusCode::InvalidState;
  status_.message = "ota transfer aborted";
  if (out_message != nullptr) {
    *out_message = status_.message;
  }
}

void OtaManager::abortActiveReceive(std::string* out_message) {
  if (!receive_.active) {
    if (out_message != nullptr) {
      *out_message = "ota no active transfer";
    }
    return;
  }

  std::string msg;
  (void)storage_.removePath(receive_.temp_path, msg);
  receive_ = ReceiveSession{};
  receive_meta_ = FirmwareImageMetadata{};
  status_.state = OtaTransferState::Failed;
  status_.code = OtaStatusCode::InvalidState;
  status_.message = "ota transfer aborted";
  if (out_message != nullptr) {
    *out_message = status_.message;
  }
}

bool OtaManager::applyImage(const std::string& image_path,
                            uint32_t image_size,
                            uint32_t image_crc32,
                            std::string& out_message) {
  expireReceiveIfStale_();
  if (image_path.empty() || image_size == 0U) {
    return fail_(OtaStatusCode::InvalidArgument, "ota apply invalid image", out_message);
  }
  if (config_.max_firmware_bytes > 0 && image_size > config_.max_firmware_bytes) {
    return fail_(OtaStatusCode::ImageTooLarge, "image too large", out_message);
  }
  if (gate_ != nullptr) {
    const OtaGateResult gate = gate_->prepareForApply(image_path, image_size, image_crc32);
    if (gate.decision != OtaGateDecision::Ready) {
      OtaStatusCode code = OtaStatusCode::GateDenied;
      if (gate.decision == OtaGateDecision::Busy) {
        code = OtaStatusCode::GateBusy;
      } else if (gate.decision == OtaGateDecision::PrepFailed) {
        code = OtaStatusCode::GatePrepFailed;
      }
      std::string msg = gate.message.empty() ? gateDecisionMessage(gate.decision) : gate.message;
      return fail_(code, msg, out_message);
    }
  }
  if (apply_executor_ == nullptr) {
    return fail_(OtaStatusCode::ApplyRejected, "apply executor unavailable", out_message);
  }

  status_.state = OtaTransferState::Applying;
  status_.code = OtaStatusCode::Ok;
  status_.image_path = image_path;
  status_.expected_size = image_size;
  status_.expected_crc32 = image_crc32;
  status_.message = "ota applying";

  OtaApplyRequest req{};
  req.image_path = image_path;
  req.image_size = image_size;
  req.image_crc32 = image_crc32;
  const OtaApplyResult result = apply_executor_->applyImage(req);
  if (!result.ok) {
    return fail_(OtaStatusCode::ApplyFailed,
                 result.message.empty() ? "ota apply failed" : result.message,
                 out_message);
  }

  status_.state = OtaTransferState::Ready;
  std::string persist_msg;
  const bool persisted = persistPendingApply_(image_path, image_size, image_crc32, persist_msg);
  std::string final_msg = result.message.empty() ? "ota apply accepted" : result.message;
  if (!persisted) {
    final_msg += " (status persist failed: ";
    final_msg += persist_msg.empty() ? "write failed" : persist_msg;
    final_msg += ")";
  }
  setOk_(final_msg, out_message);
  return true;
}

bool OtaManager::rollback(std::string& out_message) {
  if (apply_executor_ == nullptr) {
    return fail_(OtaStatusCode::ApplyRejected, "rollback executor unavailable", out_message);
  }

  status_.state = OtaTransferState::Applying;
  status_.code = OtaStatusCode::Ok;
  status_.message = "ota rollback requested";

  const OtaApplyResult result = apply_executor_->rollbackImage();
  if (!result.ok) {
    return fail_(OtaStatusCode::ApplyFailed,
                 result.message.empty() ? "ota rollback failed" : result.message,
                 out_message);
  }

  setOk_(result.message.empty() ? "ota rollback accepted" : result.message, out_message);
  return true;
}

bool OtaManager::markBootSuccessful(const std::string& sw_version,
                                    const std::string& build_id,
                                    std::string& out_message) {
  std::string msg;
  if (!storage_.begin(msg)) {
    out_message = msg.empty() ? "ota storage begin failed" : msg;
    return false;
  }
  if (!ensureFolders_(msg)) {
    out_message = msg.empty() ? "ota folders unavailable" : msg;
    return false;
  }

  PersistentStatusRecord record{};
  if (!loadStatusRecord_(record, msg)) {
    out_message = msg.empty() ? "status read failed" : msg;
    return false;
  }
  if (record.state != "pending_boot") {
    out_message = "no pending ota apply";
    return true;
  }

  if (gate_ != nullptr) {
    const OtaGateResult gate = gate_->confirmBootedImage(record.image_path,
                                                         record.image_size,
                                                         record.image_crc32,
                                                         sw_version,
                                                         build_id);
    if (gate.decision != OtaGateDecision::Ready) {
      OtaStatusCode code = OtaStatusCode::GateDenied;
      if (gate.decision == OtaGateDecision::Busy) {
        code = OtaStatusCode::GateBusy;
      } else if (gate.decision == OtaGateDecision::PrepFailed) {
        code = OtaStatusCode::GatePrepFailed;
      }
      std::string gate_msg = gate.message.empty() ? gateDecisionMessage(gate.decision) : gate.message;
      status_.persistent_state = "pending_boot";
      return fail_(code, gate_msg, out_message);
    }
  }

  record.state = "boot_ok";
  record.epoch_s = nowEpochSec_();
  record.boot_report_pending = true;
  record.confirmed_sw_version = sw_version;
  record.confirmed_build_id = build_id;
  if (!saveStatusRecord_(record, msg)) {
    out_message = msg.empty() ? "status write failed" : msg;
    return false;
  }

  applyStatusRecordToRuntime_(record);
  out_message = "updated correctly";
  if (!record.confirmed_sw_version.empty()) {
    out_message += " sw=" + record.confirmed_sw_version;
  }
  if (!record.confirmed_build_id.empty()) {
    out_message += " build=" + record.confirmed_build_id;
  }
  status_.message = out_message;
  return true;
}

uint32_t OtaManager::crc32Update_(uint32_t running_crc, const uint8_t* data, size_t len) {
  if (data == nullptr || len == 0U) {
    return running_crc;
  }
  uint32_t crc = running_crc;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint32_t>(data[i]);
    for (uint8_t b = 0; b < 8U; ++b) {
      const uint32_t mask = static_cast<uint32_t>(-(static_cast<int32_t>(crc & 1U)));
      crc = (crc >> 1U) ^ (kCrc32Poly & mask);
    }
  }
  return crc;
}

uint32_t OtaManager::crc32Finalize_(uint32_t running_crc) {
  return ~running_crc;
}

uint32_t OtaManager::hashFileId_(const std::string& name) {
  uint32_t h = kFnvOffset;
  for (char c : name) {
    h ^= static_cast<uint8_t>(c);
    h *= kFnvPrime;
  }
  return h;
}

std::string OtaManager::normalizeDir_(const std::string& path) {
  if (path.empty()) return "/";
  std::string out = path;
  if (out[0] != '/') {
    out.insert(out.begin(), '/');
  }
  while (out.size() > 1U && out.back() == '/') {
    out.pop_back();
  }
  return out;
}

std::string OtaManager::joinPath_(const std::string& base, const std::string& name) {
  if (base.empty() || base == "/") {
    return std::string("/") + name;
  }
  return base + "/" + name;
}

std::string OtaManager::fileNameFromPath_(const std::string& path) {
  if (path.empty()) return std::string();
  const size_t p = path.find_last_of('/');
  if (p == std::string::npos || (p + 1U) >= path.size()) {
    return path;
  }
  return path.substr(p + 1U);
}

std::string OtaManager::makeFileStem_(uint32_t corr_id) {
  char buf[20] = {0};
  std::snprintf(buf, sizeof(buf), "%lX", static_cast<unsigned long>(corr_id));
  return std::string(buf);
}

std::string OtaManager::manifestNameForImage_(const std::string& file_name) {
  char buf[20] = {0};
  std::snprintf(buf, sizeof(buf), "m%lX", static_cast<unsigned long>(hashFileId_(file_name)));
  return std::string(buf);
}

std::string OtaManager::trim_(const std::string& value) {
  size_t b = 0;
  while (b < value.size() && std::isspace(static_cast<unsigned char>(value[b])) != 0) {
    ++b;
  }
  if (b >= value.size()) {
    return std::string();
  }
  size_t e = value.size();
  while (e > b && std::isspace(static_cast<unsigned char>(value[e - 1])) != 0) {
    --e;
  }
  return value.substr(b, e - b);
}

bool OtaManager::parseU32_(const std::string& value, uint32_t& out) {
  out = 0;
  if (value.empty()) {
    return false;
  }
  char* endp = nullptr;
  const unsigned long v = std::strtoul(value.c_str(), &endp, 0);
  if (endp == nullptr || *endp != '\0') {
    return false;
  }
  out = static_cast<uint32_t>(v);
  return true;
}

uint32_t OtaManager::nowEpochSec_() {
  const std::time_t now = std::time(nullptr);
  if (now <= 0) {
    return 0U;
  }
  return static_cast<uint32_t>(now);
}

std::string OtaManager::statusRecordPath_() const {
  return joinPath_(config_.st_dir, "os.m");
}

bool OtaManager::ensureFolders_(std::string& out_message) {
  if (!storage_.ensureDir(config_.root_path, out_message)) {
    return false;
  }
  if (!storage_.ensureDir(config_.in_dir, out_message)) {
    return false;
  }
  if (!storage_.ensureDir(config_.stg_dir, out_message)) {
    return false;
  }
  if (!storage_.ensureDir(config_.img_dir, out_message)) {
    return false;
  }
  if (!storage_.ensureDir(config_.man_dir, out_message)) {
    return false;
  }
  if (!storage_.ensureDir(config_.st_dir, out_message)) {
    return false;
  }
  out_message = "ok";
  return true;
}

bool OtaManager::loadStatusRecord_(PersistentStatusRecord& out_record, std::string& out_message) {
  out_record = PersistentStatusRecord{};

  OtaStorageStat st{};
  std::string msg;
  const std::string path = statusRecordPath_();
  if (!storage_.stat(path, st, msg)) {
    out_message = msg.empty() ? "status stat failed" : msg;
    return false;
  }
  if (!st.exists || st.is_dir || st.size_bytes == 0U) {
    out_message = "ok";
    return true;
  }
  if (st.size_bytes > 2048U) {
    out_message = "status record too large";
    return false;
  }

  std::vector<uint8_t> buf(static_cast<size_t>(st.size_bytes), 0U);
  size_t read_len = 0U;
  if (!storage_.readAt(path, 0U, buf.data(), buf.size(), read_len, msg)) {
    out_message = msg.empty() ? "status read failed" : msg;
    return false;
  }
  if (read_len == 0U) {
    out_message = "ok";
    return true;
  }

  const std::string text(reinterpret_cast<const char*>(buf.data()),
                         reinterpret_cast<const char*>(buf.data() + read_len));
  size_t pos = 0U;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) {
      end = text.size();
    }
    std::string line = trim_(text.substr(pos, end - pos));
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!line.empty()) {
      const size_t eq = line.find('=');
      if (eq != std::string::npos && eq > 0U) {
        const std::string key = trim_(line.substr(0U, eq));
        const std::string value = trim_(line.substr(eq + 1U));
        uint32_t v = 0U;
        if (key == "state") {
          out_record.state = value.empty() ? "none" : value;
        } else if (key == "epoch_s" && parseU32_(value, v)) {
          out_record.epoch_s = v;
        } else if (key == "boot_report_pending" && parseU32_(value, v)) {
          out_record.boot_report_pending = (v != 0U);
        } else if (key == "image_path") {
          out_record.image_path = value;
        } else if (key == "image_size" && parseU32_(value, v)) {
          out_record.image_size = v;
        } else if (key == "image_crc32" && parseU32_(value, v)) {
          out_record.image_crc32 = v;
        } else if (key == "confirmed_sw") {
          out_record.confirmed_sw_version = value;
        } else if (key == "confirmed_build") {
          out_record.confirmed_build_id = value;
        }
      }
    }
    pos = (end >= text.size()) ? text.size() : (end + 1U);
  }

  if (out_record.state.empty()) {
    out_record.state = "none";
  }
  out_message = "ok";
  return true;
}

bool OtaManager::saveStatusRecord_(const PersistentStatusRecord& record, std::string& out_message) {
  std::string msg;
  if (!storage_.ensureDir(config_.st_dir, msg)) {
    out_message = msg.empty() ? "status dir unavailable" : msg;
    return false;
  }

  char epoch_buf[16] = {0};
  char size_buf[16] = {0};
  char crc_buf[16] = {0};
  std::snprintf(epoch_buf, sizeof(epoch_buf), "%lu", static_cast<unsigned long>(record.epoch_s));
  std::snprintf(size_buf, sizeof(size_buf), "%lu", static_cast<unsigned long>(record.image_size));
  std::snprintf(crc_buf, sizeof(crc_buf), "0x%08lX", static_cast<unsigned long>(record.image_crc32));

  std::string body;
  body.reserve(320U);
  body += "state=" + record.state + "\n";
  body += "epoch_s=" + std::string(epoch_buf) + "\n";
  body += "boot_report_pending=" + std::string(record.boot_report_pending ? "1" : "0") + "\n";
  body += "image_path=" + record.image_path + "\n";
  body += "image_size=" + std::string(size_buf) + "\n";
  body += "image_crc32=" + std::string(crc_buf) + "\n";
  body += "confirmed_sw=" + record.confirmed_sw_version + "\n";
  body += "confirmed_build=" + record.confirmed_build_id + "\n";

  const std::string path = statusRecordPath_();
  if (!storage_.truncateFile(path, msg)) {
    out_message = msg.empty() ? "status truncate failed" : msg;
    return false;
  }
  if (!storage_.writeAt(path,
                        0U,
                        reinterpret_cast<const uint8_t*>(body.data()),
                        body.size(),
                        msg)) {
    out_message = msg.empty() ? "status write failed" : msg;
    return false;
  }
  out_message = "ok";
  return true;
}

void OtaManager::applyStatusRecordToRuntime_(const PersistentStatusRecord& record) {
  status_.persistent_state = record.state;
  status_.persistent_epoch_s = record.epoch_s;
  status_.boot_report_pending = record.boot_report_pending;
  status_.confirmed_sw_version = record.confirmed_sw_version;
  status_.confirmed_build_id = record.confirmed_build_id;
  if (record.image_size != 0U) {
    status_.expected_size = record.image_size;
    if (status_.received_size == 0U) {
      status_.received_size = record.image_size;
    }
  }
  if (record.image_crc32 != 0U) {
    status_.expected_crc32 = record.image_crc32;
    if (status_.actual_crc32 == 0U) {
      status_.actual_crc32 = record.image_crc32;
    }
  }
  if ((record.state == "pending_boot" || record.state == "boot_ok") && !record.image_path.empty()) {
    status_.image_path = record.image_path;
  }
}

bool OtaManager::persistPendingApply_(const std::string& image_path,
                                      uint32_t image_size,
                                      uint32_t image_crc32,
                                      std::string& out_message) {
  PersistentStatusRecord record{};
  record.state = "pending_boot";
  record.epoch_s = nowEpochSec_();
  record.boot_report_pending = false;
  record.image_path = image_path;
  record.image_size = image_size;
  record.image_crc32 = image_crc32;
  record.confirmed_sw_version.clear();
  record.confirmed_build_id.clear();
  if (!saveStatusRecord_(record, out_message)) {
    return false;
  }
  applyStatusRecordToRuntime_(record);
  return true;
}

bool OtaManager::peekBootCompletionNotice(FirmwareImageMetadata& out_meta, uint32_t& out_epoch_s) const {
  out_meta = FirmwareImageMetadata{};
  out_epoch_s = 0U;
  if (status_.persistent_state != "boot_ok" || !status_.boot_report_pending) {
    return false;
  }
  out_meta.sw_version = status_.confirmed_sw_version;
  out_meta.build_id = status_.confirmed_build_id;
  out_epoch_s = status_.persistent_epoch_s;
  return true;
}

bool OtaManager::clearBootCompletionNotice(std::string& out_message) {
  PersistentStatusRecord record{};
  std::string msg;
  if (!loadStatusRecord_(record, msg)) {
    out_message = msg.empty() ? "status read failed" : msg;
    return false;
  }
  if (record.state != "boot_ok" || !record.boot_report_pending) {
    out_message = "ok";
    return true;
  }
  record.boot_report_pending = false;
  if (!saveStatusRecord_(record, msg)) {
    out_message = msg.empty() ? "status write failed" : msg;
    return false;
  }
  applyStatusRecordToRuntime_(record);
  out_message = "ok";
  return true;
}

bool OtaManager::writeReadyManifest_(const std::string& image_path,
                                     uint32_t image_size,
                                     uint32_t image_crc32,
                                     const FirmwareImageMetadata& metadata,
                                     std::string& out_message) {
  const std::string file_name = fileNameFromPath_(image_path);
  if (file_name.empty()) {
    out_message = "invalid image path";
    return false;
  }

  std::string msg;
  if (!storage_.ensureDir(config_.man_dir, msg)) {
    out_message = msg.empty() ? "manifest dir unavailable" : msg;
    return false;
  }

  const std::string manifest_path = joinPath_(config_.man_dir, manifestNameForImage_(file_name));
  char body[768] = {0};
  const uint32_t created_epoch_s = static_cast<uint32_t>(std::time(nullptr));
  const int n = std::snprintf(body,
                              sizeof(body),
                              "file_id=%lu\n"
                              "file_name=%s\n"
                              "size_bytes=%lu\n"
                              "crc32=0x%08lX\n"
                              "version=%s\n"
                              "build_id=%s\n"
                              "created_epoch_s=%lu\n"
                              "state=ready\n"
                              "required_app_bytes=%lu\n",
                              static_cast<unsigned long>(hashFileId_(file_name)),
                              file_name.c_str(),
                              static_cast<unsigned long>(image_size),
                              static_cast<unsigned long>(image_crc32),
                              metadata.sw_version.c_str(),
                              metadata.build_id.c_str(),
                              static_cast<unsigned long>(created_epoch_s),
                              static_cast<unsigned long>(image_size));
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(body)) {
    out_message = "manifest format failed";
    return false;
  }

  if (!storage_.truncateFile(manifest_path, msg)) {
    out_message = msg.empty() ? "manifest truncate failed" : msg;
    return false;
  }
  if (!storage_.writeAt(manifest_path,
                        0U,
                        reinterpret_cast<const uint8_t*>(body),
                        static_cast<size_t>(n),
                        msg)) {
    out_message = msg.empty() ? "manifest write failed" : msg;
    return false;
  }

  out_message = "ok";
  return true;
}

bool OtaManager::clearDirContents_(const std::string& dir_path, std::string& out_message) {
  std::vector<std::string> names;
  std::string msg;
  if (!storage_.listDir(dir_path, names, msg)) {
    out_message = msg.empty() ? "list failed" : msg;
    return false;
  }
  for (const auto& name : names) {
    if (name.empty() || name == "." || name == "..") {
      continue;
    }
    const std::string path = joinPath_(dir_path, name);
    if (!storage_.removePath(path, msg)) {
      out_message = msg.empty() ? "remove failed" : msg;
      return false;
    }
  }
  out_message = "ok";
  return true;
}

bool OtaManager::ensurePsramPool_(std::string& out_message) {
  if (config_.max_firmware_bytes == 0U) {
    out_message = "ota max_firmware_bytes not configured";
    return false;
  }
  if (psram_pool_ != nullptr && psram_pool_size_ == config_.max_firmware_bytes) {
    out_message = "ok";
    return true;
  }

  releasePsramPool_();

#if defined(ARDUINO)
  if (config_.psram_required && !psramFound()) {
    out_message = "psram required but not available";
    return false;
  }
  const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (largest < static_cast<size_t>(config_.max_firmware_bytes)) {
    out_message = "psram free block too small";
    return false;
  }
  psram_pool_ = static_cast<uint8_t*>(heap_caps_malloc(static_cast<size_t>(config_.max_firmware_bytes),
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (psram_pool_ == nullptr) {
    out_message = "psram allocation failed";
    return false;
  }
  psram_pool_size_ = config_.max_firmware_bytes;
  out_message = "ok";
  return true;
#else
  out_message = "psram-only ota requires arduino target";
  return false;
#endif
}

void OtaManager::releasePsramPool_() {
#if defined(ARDUINO)
  if (psram_pool_ != nullptr) {
    heap_caps_free(psram_pool_);
  }
#endif
  psram_pool_ = nullptr;
  psram_pool_size_ = 0U;
}

bool OtaManager::markChunkReceived_(uint32_t chunk_index) {
  if (chunk_index >= receive_.chunk_count) {
    return false;
  }
  const uint32_t byte_index = chunk_index >> 3U;
  const uint8_t bit_mask = static_cast<uint8_t>(1U << (chunk_index & 0x07U));
  if (byte_index >= receive_.chunk_bitmap.size()) {
    return false;
  }
  receive_.chunk_bitmap[byte_index] |= bit_mask;
  return true;
}

bool OtaManager::isChunkReceived_(uint32_t chunk_index) const {
  if (chunk_index >= receive_.chunk_count) {
    return false;
  }
  const uint32_t byte_index = chunk_index >> 3U;
  const uint8_t bit_mask = static_cast<uint8_t>(1U << (chunk_index & 0x07U));
  if (byte_index >= receive_.chunk_bitmap.size()) {
    return false;
  }
  return (receive_.chunk_bitmap[byte_index] & bit_mask) != 0U;
}

uint32_t OtaManager::chunkSizeAt_(uint32_t chunk_index) const {
  if (chunk_index >= receive_.chunk_count || receive_.chunk_size == 0U) {
    return 0U;
  }
  if (chunk_index + 1U < receive_.chunk_count) {
    return receive_.chunk_size;
  }
  const uint32_t used = chunk_index * receive_.chunk_size;
  return receive_.expected_size - used;
}

void OtaManager::refreshContiguousReceived_() {
  if (receive_.chunk_size == 0U) {
    receive_.received_size = 0U;
    return;
  }
  uint32_t contiguous = receive_.received_size;
  while (contiguous < receive_.expected_size) {
    const uint32_t chunk_index = contiguous / receive_.chunk_size;
    if (!isChunkReceived_(chunk_index)) {
      break;
    }
    const uint32_t n = chunkSizeAt_(chunk_index);
    if (n == 0U) {
      break;
    }
    contiguous += n;
  }
  receive_.received_size = contiguous;
}

void OtaManager::expireReceiveIfStale_() {
  if (!receive_.active) {
    return;
  }
  if (config_.receive_timeout_s == 0U) {
    return;
  }
  const uint32_t now_s = nowEpochSec_();
  if (now_s == 0U || receive_.last_activity_epoch_s == 0U) {
    return;
  }
  const uint32_t elapsed = (now_s >= receive_.last_activity_epoch_s)
                               ? (now_s - receive_.last_activity_epoch_s)
                               : 0U;
  if (elapsed < config_.receive_timeout_s) {
    return;
  }

  std::string msg;
  (void)storage_.removePath(receive_.temp_path, msg);
  receive_ = ReceiveSession{};
  receive_meta_ = FirmwareImageMetadata{};
  status_.state = OtaTransferState::Failed;
  status_.code = OtaStatusCode::Timeout;
  status_.message = "ota transfer timeout";
}

bool OtaManager::fail_(OtaStatusCode code, const std::string& message, std::string& out_message) {
  status_.code = code;
  status_.state = OtaTransferState::Failed;
  status_.message = message;
  out_message = message;
  return false;
}

void OtaManager::setOk_(const std::string& message, std::string& out_message) {
  status_.code = OtaStatusCode::Ok;
  status_.message = message;
  out_message = message;
}

bool OtaManager::validateSession_(const MacAddress& from, uint32_t corr_id, std::string& out_message) const {
  if (!receive_.active) {
    out_message = "ota no active transfer";
    return false;
  }
  if (receive_.corr_id != corr_id || receive_.from != from) {
    out_message = "ota transfer context mismatch";
    return false;
  }
  return true;
}

}  // namespace espnow_link
