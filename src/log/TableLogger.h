#pragma once

namespace yagpcc {
class SetQueryReq;
}

class TableLogger {
public:
  static void log_query_req(const yagpcc::SetQueryReq &req);
};
