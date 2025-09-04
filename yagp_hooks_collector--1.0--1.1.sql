/* yagp_hooks_collector--1.0--1.1.sql */

-- complain if script is sourced in psql, rather than via ALTER EXTENSION
\echo Use "ALTER EXTENSION yagp_hooks_collector UPDATE TO '1.1'" to load this file. \quit

CREATE FUNCTION __yagp_init_log_on_master()
RETURNS void
AS 'MODULE_PATHNAME', 'yagp_init_log'
LANGUAGE C STRICT VOLATILE EXECUTE ON MASTER;

CREATE FUNCTION __yagp_init_log_on_segments()
RETURNS void
AS 'MODULE_PATHNAME', 'yagp_init_log'
LANGUAGE C STRICT VOLATILE EXECUTE ON ALL SEGMENTS;

-- Creates schema __yagp and log table inside it
SELECT __yagp_init_log_on_master();
SELECT __yagp_init_log_on_segments();

CREATE VIEW yagp_log AS
  SELECT * FROM __yagp.log -- master
  UNION ALL
  SELECT * FROM gp_dist_random('__yagp.log') -- segments
ORDER BY tmid, ssid, ccnt;

CREATE FUNCTION __yagp_truncate_log_on_master()
RETURNS void
AS 'MODULE_PATHNAME', 'yagp_truncate_log'
LANGUAGE C STRICT VOLATILE EXECUTE ON MASTER;

CREATE FUNCTION __yagp_truncate_log_on_segments()
RETURNS void
AS 'MODULE_PATHNAME', 'yagp_truncate_log'
LANGUAGE C STRICT VOLATILE EXECUTE ON ALL SEGMENTS;

CREATE FUNCTION yagp_truncate_log()
RETURNS void AS $$
BEGIN
    PERFORM __yagp_truncate_log_on_master();
    PERFORM __yagp_truncate_log_on_segments();
END;
$$ LANGUAGE plpgsql VOLATILE;

