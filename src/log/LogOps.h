#pragma once

#include <string>

extern "C" {
#include "postgres.h"
#include "fmgr.h"
}

namespace yagpcc {
class SetQueryReq;
}

extern "C" {
/*
 * CREATE SCHEMA __yagp;
 * CREATE TABLE __yagp.log (...);
 */
void init_log();

/* SELECT * FROM __yagp.log */
Datum select_log(PG_FUNCTION_ARGS);

/* TRUNCATE __yagp.log */
void truncate_log();
}

/* INSERT INTO __yagp.log VALUES (...) */
void insert_log(const yagpcc::SetQueryReq &req);
