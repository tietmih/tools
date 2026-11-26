#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bs {

struct TsPidInfo {
  int pid = 0;
  uint64_t packets = 0;
  int stream_type = -1;
  bool pmt = false;
  bool referenced = false;
};

struct TsReport {
  bool valid = false;
  std::string error;
  int packet_size = 0;
  uint64_t packets = 0;
  int continuity_errors = 0;
  int transport_errors = 0;
  int scrambled_packets = 0;
  int pcr_count = 0;
  uint64_t first_pcr = 0;
  uint64_t last_pcr = 0;
  double pcr_jitter_us = 0.0;
  std::vector<TsPidInfo> pids;
  std::vector<std::string> warnings;
};

bool analyzeTsFile(const std::string& path, TsReport& report);
bool analyzeTsBuffer(const std::vector<uint8_t>& data, TsReport& report);
std::string tsCsv(const TsReport& report);

}  // namespace bs
