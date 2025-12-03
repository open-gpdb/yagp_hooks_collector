CREATE EXTENSION yagp_hooks_collector;

CREATE OR REPLACE FUNCTION select_previous_insert_query_text()
RETURNS TEXT AS $$
DECLARE
    found_query TEXT;
BEGIN
    SELECT query_text INTO found_query
    FROM yagpcc.log
    WHERE query_text LIKE '%INSERT%'
      AND query_text NOT LIKE '%SELECT query_text%'
      AND query_text NOT LIKE '%select_previous_insert_query_text%'
    ORDER BY datetime DESC
    LIMIT 1;

    RETURN found_query;
END;
$$ LANGUAGE plpgsql VOLATILE;

SET yagpcc.enable TO TRUE;
SET yagpcc.logging_mode to 'TBL';
CREATE TABLE test_utf8 (id int, name text) DISTRIBUTED RANDOMLY;

-- 1 byte chars
SET yagpcc.max_text_size to 35;
-- byte index:                   34 - H
INSERT INTO test_utf8 VALUES (1, 'HelloWorld');

SELECT octet_length(select_previous_insert_query_text()) = 35 AS correct_length;

-- 2 byte chars
SET yagpcc.max_text_size to 35;
-- byte index:                   34 |cut| 35 - Р
INSERT INTO test_utf8 VALUES (2, 'РУССКИЙЯЗЫК');

SELECT octet_length(select_previous_insert_query_text()) = 36 AS correct_length;

-- 4 byte chars
SET yagpcc.max_text_size to 35;
-- byte number:                  34 |cut| 35, 36, 37 - emoji
INSERT INTO test_utf8 VALUES (3, '😀');

SELECT octet_length(select_previous_insert_query_text()) = 38 AS correct_length;

-- Cleanup
DROP TABLE test_utf8;
RESET yagpcc.max_text_size;
RESET yagpcc.enable;

DROP EXTENSION yagp_hooks_collector;
