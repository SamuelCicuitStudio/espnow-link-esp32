#pragma once

#include <string>

#include "espnow_link/cli_master.hpp"
#include "espnow_link/node_runtime.hpp"

#if defined(ARDUINO)
#include <Arduino.h>

namespace espnow_link {

/**
 * @brief Arduino `Stream` backed CLI IO adapter.
 *
 * Default output stream is global `Serial` when no stream is explicitly bound.
 * Bind another stream (for example `Serial1`) from the example/application code
 * to move CLI traffic to a different UART channel.
 */
class ArduinoStreamCliIo final : public IMasterCliIo {
 public:
  /** @brief Construct with optional stream override. `nullptr` means default `Serial`. */
  explicit ArduinoStreamCliIo(Stream* stream = nullptr) : stream_(stream) {}

  /** @brief Bind CLI IO to a specific stream (for example `Serial1`). */
  void bind(Stream& stream) { stream_ = &stream; }

  /** @brief Return currently bound stream pointer (null when default `Serial` is used). */
  Stream* boundStream() const { return stream_; }

  /** @brief Write text without newline to active stream. */
  void write(const std::string& text) override { out().print(text.c_str()); }

  /** @brief Write text with newline to active stream. */
  void writeln(const std::string& text) override { out().println(text.c_str()); }

 private:
  Stream& out() const { return (stream_ != nullptr) ? *stream_ : defaultStream(); }
  static Stream& defaultStream() { return Serial; }

  Stream* stream_ = nullptr;
};

/**
 * @brief Arduino `Stream` input poller that forwards complete lines to `MasterCli`.
 *
 * Default input stream is global `Serial` when no stream is explicitly bound.
 */
class ArduinoStreamCliInput final : public INodeRuntimePoller {
 public:
  /** @brief Construct with target CLI and optional stream override. */
  explicit ArduinoStreamCliInput(MasterCli& cli, Stream* stream = nullptr) : cli_(cli), stream_(stream) {}

  /** @brief Bind input to a specific stream (for example `Serial1`). */
  void bind(Stream& stream) { stream_ = &stream; }

  /** @brief Return currently bound stream pointer (null when default `Serial` is used). */
  Stream* boundStream() const { return stream_; }

  /** @brief Poll stream bytes and dispatch complete `\\n` terminated lines to CLI. */
  void poll() override {
    Stream& s = in();
    while (s.available() > 0) {
      const char c = static_cast<char>(s.read());
      if (c == '\r') {
        continue;
      }
      if (c == '\n') {
        cli_.handleLine(line_);
        line_.clear();
        continue;
      }
      line_.push_back(c);
    }
  }

 private:
  Stream& in() const { return (stream_ != nullptr) ? *stream_ : defaultStream(); }
  static Stream& defaultStream() { return Serial; }

  MasterCli& cli_;
  Stream* stream_ = nullptr;
  std::string line_{};
};

}  // namespace espnow_link

#endif  // defined(ARDUINO)
