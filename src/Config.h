#pragma once

#include <string>
#include <string_view>

class Config {
public:
  static void init();
  static std::string uds_path();
  static bool enable_analyze();
  static bool enable_cdbstats();
  static bool enable_collector();
  static bool filter_user(std::string_view username);
  static bool report_nested_queries();
  static size_t max_text_size();
  static size_t max_plan_size();
  static int min_analyze_time();
  static void sync();
};