#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "espnow_link/ota_apply.hpp"
#include "espnow_link/ota_paths.hpp"
#include "espnow_link/ota_storage.hpp"
#include "espnow_link/ota_types.hpp"
#include "espnow_link/ota_update_gate.hpp"
#include "espnow_link/firmware.hpp"
#include "espnow_link/types.hpp"

namespace espnow_link {

/** @brief Config for OTA folder policy and size limits. */
struct OtaManagerConfig {
  std::string root_path = ota_paths::kRoot;
  std::string in_dir = ota_paths::kIn;
  std::string stg_dir = ota_paths::kStaging;
  std::string img_dir = ota_paths::kImage;
  std::string man_dir = ota_paths::kManifest;
  std::string st_dir = ota_paths::kState;
  uint32_t max_firmware_bytes = 0;  // 0 = no library-side cap
  uint32_t receive_timeout_s = 12;  // 0 = disabled
  bool psram_required = true;       // strict mode: OTA RX requires PSRAM pool
};

/**
 * @brief OTA core state machine for storage-backed receive/apply operations.
 *
 * This class is transport-agnostic and is designed to be used via `OtaFirmwareSink`
 * for inbound begin/chunk/end streams.
 */
class OtaManager {
 public:
  /**
   * @brief Construct OTA manager.
   * @param storage App-provided storage backend.
   * @param config OTA folder/limit configuration.
   * @param gate Optional device update gate.
   * @param apply_executor Optional apply executor.
   */
  OtaManager(IOtaStorageBackend& storage,
             const OtaManagerConfig& config = {},
             IOtaUpdateGate* gate = nullptr,
             IOtaApplyExecutor* apply_executor = nullptr);
  ~OtaManager();

  /** @brief Initialize backend and ensure OTA folder structure exists. */
  bool begin(std::string& out_message);

  /** @brief Update max allowed firmware size in bytes (`0` disables cap). */
  void setMaxFirmwareBytes(uint32_t max_bytes);

  /** @brief Read configured max firmware size in bytes. */
  uint32_t maxFirmwareBytes() const;

  /** @brief Bind/replace update gate hook. */
  void setUpdateGate(IOtaUpdateGate* gate);

  /** @brief Bind/replace apply executor hook. */
  void setApplyExecutor(IOtaApplyExecutor* apply_executor);

  /** @brief Current runtime OTA status snapshot. */
  const OtaRuntimeStatus& status() const;

  /** @brief Highest contiguous received byte offset for active transfer. */
  uint32_t contiguousReceiveSize() const;

  /** @brief Advance timeout housekeeping for receive session. */
  void tick();

  /**
   * @brief Begin inbound transfer session.
   * @param from Source MAC.
   * @param corr_id Correlation ID.
   * @param total_size Declared image size.
   * @param chunk_size Declared chunk size.
   * @param image_crc32 Declared image CRC32.
   * @param out_message Result message.
   * @return true when transfer session is accepted.
   */
  bool beginReceive(const MacAddress& from,
                    uint32_t corr_id,
                    uint32_t total_size,
                    uint32_t chunk_size,
                    uint32_t image_crc32,
                    const FirmwareImageMetadata* metadata,
                    std::string& out_message);

  /**
   * @brief Write one transfer chunk.
   * @param from Source MAC.
   * @param corr_id Correlation ID.
   * @param offset Declared chunk offset.
   * @param data Chunk bytes.
   * @param len Chunk length.
   * @param out_message Result message.
   * @return true when chunk is accepted and persisted.
   */
  bool writeReceiveChunk(const MacAddress& from,
                         uint32_t corr_id,
                         uint32_t offset,
                         const uint8_t* data,
                         size_t len,
                         std::string& out_message);

  /**
   * @brief Finalize transfer session.
   * @param from Source MAC.
   * @param corr_id Correlation ID.
   * @param total_size Final total size.
   * @param image_crc32 Final CRC32.
   * @param out_image_path Final promoted image path on success.
   * @param out_message Result message.
   * @return true on verified and promoted image.
   */
  bool endReceive(const MacAddress& from,
                  uint32_t corr_id,
                  uint32_t total_size,
                  uint32_t image_crc32,
                  std::string& out_image_path,
                  std::string& out_message);

  /** @brief Abort active receive session and cleanup temp file. */
  void abortReceive(const MacAddress& from, uint32_t corr_id, std::string* out_message = nullptr);
  /** @brief Abort whichever receive session is currently active. */
  void abortActiveReceive(std::string* out_message = nullptr);

  /**
   * @brief Apply existing image through app-owned executor.
   * @param image_path Absolute/normalized image path.
   * @param image_size Size in bytes.
   * @param image_crc32 CRC32.
   * @param out_message Result message.
   * @return true when apply accepted by executor.
   */
  bool applyImage(const std::string& image_path,
                  uint32_t image_size,
                  uint32_t image_crc32,
                  std::string& out_message);

  /** @brief Request rollback to previous image through app-owned executor. */
  bool rollback(std::string& out_message);

  /** @brief Check whether a one-shot boot completion notice is pending. */
  bool peekBootCompletionNotice(FirmwareImageMetadata& out_meta, uint32_t& out_epoch_s) const;

  /** @brief Clear boot completion notice after master notification was sent. */
  bool clearBootCompletionNotice(std::string& out_message);

  /**
   * @brief Confirm that device booted successfully after a previously applied image.
   *
   * Call once during startup after app init is stable. If no pending apply exists,
   * this returns success with a no-op message.
   */
  bool markBootSuccessful(const std::string& sw_version,
                          const std::string& build_id,
                          std::string& out_message);

 private:
  struct PersistentStatusRecord {
    std::string state = "none";
    uint32_t epoch_s = 0;
    bool boot_report_pending = false;
    std::string image_path;
    uint32_t image_size = 0;
    uint32_t image_crc32 = 0;
    std::string confirmed_sw_version;
    std::string confirmed_build_id;
  };

  struct ReceiveSession {
    bool active = false;
    MacAddress from{};
    uint32_t corr_id = 0;
    uint32_t expected_size = 0;
    uint32_t expected_crc32 = 0;
    uint32_t chunk_size = 0;
    uint32_t received_size = 0;       // contiguous bytes from offset 0
    uint32_t unique_received_size = 0;
    uint32_t running_crc32 = 0xFFFFFFFFU;
    uint32_t last_activity_epoch_s = 0;
    std::string temp_path;
    std::string final_path;
    std::vector<uint8_t> chunk_bitmap;
    uint32_t chunk_count = 0;
  };

  static uint32_t crc32Update_(uint32_t running_crc, const uint8_t* data, size_t len);
  static uint32_t crc32Finalize_(uint32_t running_crc);
  static uint32_t hashFileId_(const std::string& name);
  static std::string normalizeDir_(const std::string& path);
  static std::string joinPath_(const std::string& base, const std::string& name);
  static std::string fileNameFromPath_(const std::string& path);
  static std::string makeFileStem_(uint32_t corr_id);
  static std::string manifestNameForImage_(const std::string& file_name);
  static std::string trim_(const std::string& value);
  static bool parseU32_(const std::string& value, uint32_t& out);
  static uint32_t nowEpochSec_();
  std::string statusRecordPath_() const;
  bool ensureFolders_(std::string& out_message);
  bool loadStatusRecord_(PersistentStatusRecord& out_record, std::string& out_message);
  bool saveStatusRecord_(const PersistentStatusRecord& record, std::string& out_message);
  void applyStatusRecordToRuntime_(const PersistentStatusRecord& record);
  bool persistPendingApply_(const std::string& image_path,
                            uint32_t image_size,
                            uint32_t image_crc32,
                            std::string& out_message);
  bool writeReadyManifest_(const std::string& image_path,
                           uint32_t image_size,
                           uint32_t image_crc32,
                           const FirmwareImageMetadata& metadata,
                           std::string& out_message);
  bool clearDirContents_(const std::string& dir_path, std::string& out_message);
  bool ensureIoScratch_(size_t size);
  bool ensurePsramPool_(std::string& out_message);
  void releasePsramPool_();
  bool markChunkReceived_(uint32_t chunk_index);
  bool isChunkReceived_(uint32_t chunk_index) const;
  uint32_t chunkSizeAt_(uint32_t chunk_index) const;
  void refreshContiguousReceived_();
  void expireReceiveIfStale_();
  bool fail_(OtaStatusCode code, const std::string& message, std::string& out_message);
  void setOk_(const std::string& message, std::string& out_message);
  bool validateSession_(const MacAddress& from, uint32_t corr_id, std::string& out_message) const;

  IOtaStorageBackend& storage_;
  OtaManagerConfig config_;
  IOtaUpdateGate* gate_ = nullptr;
  IOtaApplyExecutor* apply_executor_ = nullptr;
  ReceiveSession receive_{};
  FirmwareImageMetadata receive_meta_{};
  OtaRuntimeStatus status_{};
  std::vector<uint8_t> io_scratch_{};
  uint8_t* psram_pool_ = nullptr;
  uint32_t psram_pool_size_ = 0;
};

}  // namespace espnow_link
