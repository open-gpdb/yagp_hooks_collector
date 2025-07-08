#pragma once

#include "memory/PgContext.h"

#include <memory>
#include <unordered_map>
#include <string>

#define typeid __typeid
extern "C" {
#include "utils/metrics_utils.h"
#include "cdb/ml_ipc.h"
#ifdef IC_TEARDOWN_HOOK
#include "cdb/ic_udpifc.h"
#endif
}
#undef typeid

class UDSConnector;
struct QueryDesc;
namespace yagpcc {
class SetQueryReq;
}

class EventSender {
public:
  void executor_before_start(QueryDesc *query_desc, int eflags);
  void executor_after_start(QueryDesc *query_desc, int eflags);
  void executor_end(QueryDesc *query_desc);
  void query_metrics_collect(QueryMetricsStatus status, void *arg);
  void ic_metrics_collect();
  void analyze_stats_collect(QueryDesc *query_desc);
  void incr_depth() { nesting_level++; }
  void decr_depth() { nesting_level--; }
  EventSender();

private:
  enum QueryState { UNKNOWN, SUBMIT, START, END, DONE };

  struct QueryItem {
    QueryState state = QueryState::UNKNOWN;
    yagpcc::SetQueryReq *message = nullptr;

    QueryItem(yagpcc::SetQueryReq *msg);
  };

  void update_query_state(QueryDesc *query_desc, QueryItem *query,
                          QueryState new_state, bool success = true);
  QueryItem *get_query_message(QueryDesc *query_desc);
  void collect_query_submit(QueryDesc *query_desc);
  void collect_query_done(QueryDesc *query_desc, QueryMetricsStatus status);
  void cleanup_messages();
  void update_nested_counters(QueryDesc *query_desc);

  UDSConnector *connector = nullptr;
  int nesting_level = 0;
  int64_t nested_calls = 0;
  double nested_timing = 0;
#ifdef IC_TEARDOWN_HOOK
  ICStatistics ic_statistics;
#endif
  using QueryKey = std::pair<int, int>;
  using QueryValue = QueryItem;
  using QueryMapAllocator =
      PallocZeroAllocator<std::pair<const QueryKey, QueryValue>>;
  struct pair_hash {
    std::size_t operator()(const QueryKey &p) const {
      auto h1 = std::hash<int>{}(p.first);
      auto h2 = std::hash<int>{}(p.second);
      return h1 ^ h2;
    }
  };
  std::unordered_map<QueryKey, QueryValue, pair_hash, std::equal_to<QueryKey>,
                     QueryMapAllocator>
      query_msgs;
};