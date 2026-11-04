#include "fdr/sink.hpp"

namespace fdr {

CsvSink::CsvSink(std::ostream& out) : out_(&out) {
  *out_ << "timestamp_us,channel,length,payload_hex\n";
}

void CsvSink::emit(const Record& record) {
  static const char kHex[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(record.payload.size() * 2);
  for (const std::uint8_t b : record.payload) {
    hex.push_back(kHex[b >> 4U]);
    hex.push_back(kHex[b & 0x0FU]);
  }
  *out_ << record.timestamp_us << ',' << record.channel << ',' << record.payload.size() << ','
       << hex << '\n';
}

void UdpSink::emit(const Record& record) {
  sender_.send(record.payload.data(), record.payload.size());
}

}  // namespace fdr
