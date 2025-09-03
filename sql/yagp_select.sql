CREATE EXTENSION yagp_hooks_collector;

CREATE OR REPLACE FUNCTION yagp_status_order(status text)
RETURNS integer
AS $$
BEGIN
    RETURN CASE status
        WHEN 'QUERY_STATUS_SUBMIT' THEN 1
        WHEN 'QUERY_STATUS_START' THEN 2
        WHEN 'QUERY_STATUS_END' THEN 3
        WHEN 'QUERY_STATUS_DONE' THEN 4
        ELSE 999
    END;
END;
$$ LANGUAGE plpgsql IMMUTABLE;

SET yagpcc.enable TO TRUE;
SET yagpcc.report_nested_queries TO TRUE;

-- Basic SELECT tests
SET yagpcc.log_to_table TO TRUE;

SELECT 1;
SELECT COUNT(*) FROM generate_series(1,10);

SET yagpcc.log_to_table TO FALSE;
SELECT dbid, ccnt, query_text, query_status FROM yagp_log ORDER BY dbid, ccnt, yagp_status_order(query_status) ASC;
SELECT yagp_truncate_log() IS NOT NULL AS t;

-- Transaction test
SET yagpcc.log_to_table TO TRUE;

BEGIN;
SELECT 1;
COMMIT;

SET yagpcc.log_to_table TO FALSE;
SELECT dbid, ccnt, query_text, query_status FROM yagp_log ORDER BY dbid, ccnt, yagp_status_order(query_status) ASC;
SELECT yagp_truncate_log() IS NOT NULL AS t;

-- CTE test
SET yagpcc.log_to_table TO TRUE;

WITH t AS (VALUES (1), (2))
SELECT * FROM t;

SET yagpcc.log_to_table TO FALSE;
SELECT dbid, ccnt, query_text, query_status FROM yagp_log ORDER BY dbid, ccnt, yagp_status_order(query_status) ASC;
SELECT yagp_truncate_log() IS NOT NULL AS t;

-- Prepared statement test
SET yagpcc.log_to_table TO TRUE;

PREPARE test_stmt AS SELECT 1;
EXECUTE test_stmt;
DEALLOCATE test_stmt;

SET yagpcc.log_to_table TO FALSE;
SELECT dbid, ccnt, query_text, query_status FROM yagp_log ORDER BY dbid, ccnt, yagp_status_order(query_status) ASC;
SELECT yagp_truncate_log() IS NOT NULL AS t;

DROP FUNCTION yagp_status_order(text);
DROP EXTENSION yagp_hooks_collector;