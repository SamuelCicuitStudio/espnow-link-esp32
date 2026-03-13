#pragma once

#include <cstdint>
#include <ctime>
#include <sys/time.h>

namespace espnow_link {

/** @brief Epoch time source interface used by manager/pairing logic. */
class ITimeSource {
 public:
  virtual ~ITimeSource() = default;
  /**
   * @brief Read current epoch time in seconds.
   * @param out_epoch_s Output epoch seconds.
   * @return true on success.
   */
  virtual bool nowEpochSec(uint64_t& out_epoch_s) = 0;
};

/** @brief Epoch time sink interface used for inbound time synchronization. */
class ITimeSink {
 public:
  virtual ~ITimeSink() = default;
  /**
   * @brief Apply epoch time in seconds.
   * @param epoch_s Epoch seconds to set.
   * @return true on success.
   */
  virtual bool setEpochSec(uint64_t epoch_s) = 0;
};

/** @brief Null source that reports no time available. */
class NullTimeSource : public ITimeSource {
 public:
  bool nowEpochSec(uint64_t&) override { return false; }
};

/** @brief Null sink that ignores time updates. */
class NullTimeSink : public ITimeSink {
 public:
  bool setEpochSec(uint64_t) override { return false; }
};

/** @brief System-backed source using libc `time()`. */
class SystemTimeSource : public ITimeSource {
 public:
  bool nowEpochSec(uint64_t& out_epoch_s) override {
    const time_t t = time(nullptr);
    if (t <= 0) {
      return false;
    }
    out_epoch_s = static_cast<uint64_t>(t);
    return true;
  }
};

/** @brief System-backed sink using `settimeofday()`. */
class SystemTimeSink : public ITimeSink {
 public:
  bool setEpochSec(uint64_t epoch_s) override {
    struct timeval tv {};
    tv.tv_sec = static_cast<time_t>(epoch_s);
    tv.tv_usec = 0;
    return settimeofday(&tv, nullptr) == 0;
  }
};

}  // namespace espnow_link
