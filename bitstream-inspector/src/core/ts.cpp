#include "core/ts.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>

namespace bs {

namespace {

TsPidInfo& pidInfo(TsReport& report, int pid) {
  for (auto& info : report.pids)
    if (info.pid == pid) return info;
  TsPidInfo info;
  info.pid = pid;
  report.pids.push_back(info);
  return report.pids.back();
}

void parsePat(const uint8_t* payload, size_t size, TsReport& report) {
  if (size < 9) return;
  size_t pointer = payload[0];
  if (pointer + 4 > size) return;
  const uint8_t* section = payload + 1 + pointer;
  size_t left = size - 1 - pointer;
  if (section[0] != 0x00 || left < 8) return;
  size_t section_length = ((section[1] & 0x0f) << 8) | section[2];
  if (section_length + 3 > left) return;
  for (size_t pos = 8; pos + 4 <= section_length + 3; pos += 4) {
    int program = (section[pos] << 8) | section[pos + 1];
    int pid = ((section[pos + 2] & 0x1f) << 8) | section[pos + 3];
    TsPidInfo& info = pidInfo(report, pid);
    info.referenced = true;
    info.pmt = program != 0;
  }
}

void parsePmt(const uint8_t* payload, size_t size, TsReport& report) {
  if (size < 12) return;
  size_t pointer = payload[0];
  if (pointer + 12 > size) return;
  const uint8_t* section = payload + 1 + pointer;
  size_t left = size - 1 - pointer;
  if (section[0] != 0x02 || left < 12) return;
  size_t section_length = ((section[1] & 0x0f) << 8) | section[2];
  if (section_length + 3 > left) return;
  size_t program_info_length = ((section[10] & 0x0f) << 8) | section[11];
  size_t pos = 12 + program_info_length;
  const size_t end = std::min(left, section_length + 3);
  while (pos + 5 <= end) {
    int stream_type = section[pos];
    int pid = ((section[pos + 1] & 0x1f) << 8) | section[pos + 2];
    int es_info = ((section[pos + 3] & 0x0f) << 8) | section[pos + 4];
    TsPidInfo& info = pidInfo(report, pid);
    info.stream_type = stream_type;
    info.referenced = true;
    pos += 5 + es_info;
  }
}

int detectPacketSize(const std::vector<uint8_t>& data, int& sync_offset) {
  for (int size : {188, 192, 204}) {
    const int offset = size == 192 ? 4 : 0;
    int hits = 0;
    for (int n = 0; n < 5; n++) {
      size_t pos = static_cast<size_t>(offset) + static_cast<size_t>(n) * size;
      if (pos < data.size() && data[pos] == 0x47) hits++;
    }
    if (hits >= 4) {
      sync_offset = offset;
      return size;
    }
  }
  return 0;
}

}  // namespace

bool analyzeTsBuffer(const std::vector<uint8_t>& data, TsReport& report) {
  report = TsReport();
  int sync_offset = 0;
  const int size = detectPacketSize(data, sync_offset);
  if (!size) {
    report.error = "no MPEG-TS sync pattern found";
    return false;
  }
  report.valid = true;
  report.packet_size = size;
  std::map<int, int> last_cc;
  std::map<int, bool> has_cc;
  std::vector<double> pcr_us;
  const size_t stride = static_cast<size_t>(size);
  for (size_t pos = static_cast<size_t>(sync_offset); pos + 188 <= data.size();
       pos += stride) {
    const uint8_t* packet = data.data() + pos;
    if (packet[0] != 0x47) continue;
    report.packets++;
    const int tei = packet[1] & 0x80;
    const int pusi = packet[1] & 0x40;
    const int pid = ((packet[1] & 0x1f) << 8) | packet[2];
    const int scrambling = (packet[3] >> 6) & 0x3;
    const int adaptation = (packet[3] >> 4) & 0x3;
    const int cc = packet[3] & 0x0f;
    if (tei) report.transport_errors++;
    if (scrambling) report.scrambled_packets++;
    TsPidInfo& info = pidInfo(report, pid);
    info.packets++;
    if ((adaptation & 1) != 0) {
      auto it = last_cc.find(pid);
      if (it != last_cc.end() && has_cc[pid] &&
          cc != ((it->second + 1) & 0x0f) && cc != it->second) {
        report.continuity_errors++;
      }
      last_cc[pid] = cc;
      has_cc[pid] = true;
    }
    size_t payload_pos = 4;
    if (adaptation & 0x2) {
      if (payload_pos >= 188) continue;
      const int adaptation_length = packet[payload_pos];
      if (adaptation_length > 0 && payload_pos + 1 < 188) {
        const int flags = packet[payload_pos + 1];
        if ((flags & 0x10) && payload_pos + 7 < 188) {
          const uint8_t* p = packet + payload_pos + 2;
          const uint64_t base = (static_cast<uint64_t>(p[0]) << 25) |
                                (static_cast<uint64_t>(p[1]) << 17) |
                                (static_cast<uint64_t>(p[2]) << 9) |
                                (static_cast<uint64_t>(p[3]) << 1) |
                                (p[4] >> 7);
          const double seconds = static_cast<double>(base) / 90000.0;
          if (report.pcr_count == 0) report.first_pcr = base;
          report.last_pcr = base;
          report.pcr_count++;
          pcr_us.push_back(seconds * 1e6);
        }
      }
      payload_pos += 1 + static_cast<size_t>(adaptation_length);
    }
    if (!(adaptation & 1) || payload_pos + 1 >= 188) continue;
    const uint8_t* payload = packet + payload_pos;
    const size_t payload_size = 188 - payload_pos;
    if (pusi && pid == 0) parsePat(payload, payload_size, report);
    if (pusi) {
      for (const auto& entry : report.pids) {
        if (entry.pid == pid && entry.pmt) {
          parsePmt(payload, payload_size, report);
          break;
        }
      }
    }
  }
  if (pcr_us.size() >= 2) {
    double total = 0.0;
    for (size_t i = 1; i < pcr_us.size(); i++)
      total += pcr_us[i] - pcr_us[i - 1];
    const double average = total / static_cast<double>(pcr_us.size() - 1);
    double jitter = 0.0;
    for (size_t i = 1; i < pcr_us.size(); i++)
      jitter = std::max(jitter, std::fabs(pcr_us[i] - pcr_us[i - 1] - average));
    report.pcr_jitter_us = jitter;
  }
  if (report.continuity_errors)
    report.warnings.push_back("continuity counter errors detected");
  if (report.transport_errors)
    report.warnings.push_back("transport error indicator set");
  return true;
}

bool analyzeTsFile(const std::string& path, TsReport& report) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    report = TsReport();
    report.error = "cannot open file";
    return false;
  }
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
  return analyzeTsBuffer(data, report);
}

std::string tsCsv(const TsReport& report) {
  std::string out = "pid,packets,stream_type,pmt,referenced\r\n";
  for (const auto& info : report.pids) {
    out += std::to_string(info.pid);
    out += ',';
    out += std::to_string(static_cast<unsigned long long>(info.packets));
    out += ',';
    out += std::to_string(info.stream_type);
    out += ',';
    out += info.pmt ? "1" : "0";
    out += ',';
    out += info.referenced ? "1" : "0";
    out += "\r\n";
  }
  return out;
}

}  // namespace bs
