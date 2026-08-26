#pragma once

#include <cstdint>
#include <ostream>
#include <string>

#include "fdr/net.hpp"
#include "fdr/record.hpp"

namespace fdr {

// Where records go when exporting or replaying.
class Sink {
 public:
  virtual ~Sink() = default;
  virtual void emit(const Record& record) = 0;
};

// One CSV row per record: timestamp_us,channel,length,payload_hex
class CsvSink final : public Sink {
 public:
  explicit CsvSink(std::ostream& out);  // writes the header row immediately
  void emit(const Record& record) override;

 private:
  std::ostream& out_;
};

// Sends each payload as one UDP datagram.
class UdpSink final : public Sink {
 public:
  UdpSink(const std::string& host, std::uint16_t port) : sender_(host, port) {}
  void emit(const Record& record) override;

 private:
  UdpSender sender_;
};

}  // namespace fdr
