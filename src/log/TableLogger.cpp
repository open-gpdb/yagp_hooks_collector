#include "TableLogger.h"
#include "Config.h"
#include "LogOps.h"

void TableLogger::log_query_req(const yagpcc::SetQueryReq &req) {
  if (Config::log_to_table()) {
    insert_log(req);
  }
}
