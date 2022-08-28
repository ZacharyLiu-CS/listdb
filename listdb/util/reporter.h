#pragma once

#include <fstream>
#include <sstream>
#include <utility>
#include <array>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>

#include "clock.h"

class Reporter {
 public:
  enum class OpType {
    kFlush,
    kCompaction,
    kPut,
    kGet
  };
  Reporter(const std::string& fname, uint64_t report_interval_msecs = 1000, std::string header = "");
  ~Reporter();
  void Start();
  void ReportFinishedOps(OpType op_type, int64_t num_ops);

 private:
  std::string Header() const { return "msecs_elapsed,flush_done,compaction_done,put_done,get_done"; }
  void SleepAndReport();

  static constexpr uint64_t kMicrosInMilliSecond = 1000U;

  const uint64_t report_interval_msecs_;
  std::ofstream report_file_;
  std::array<std::atomic<int64_t>, 4> total_ops_done_;
  std::array<int64_t, 4> last_report_;
  std::thread reporting_thread_;
  std::mutex mu_;
  std::condition_variable stop_cv_;
  bool start_;
  bool stop_;
};
