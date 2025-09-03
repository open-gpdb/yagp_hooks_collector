/* yagp_hooks_collector--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION yagp_hooks_collector" to load this file. \quit

CREATE FUNCTION __yagp_stat_messages_reset_f_on_master()
RETURNS void
AS 'MODULE_PATHNAME', 'yagp_stat_messages_reset'
LANGUAGE C EXECUTE ON MASTER;

CREATE FUNCTION __yagp_stat_messages_reset_f_on_segments()
RETURNS void
AS 'MODULE_PATHNAME', 'yagp_stat_messages_reset'
LANGUAGE C EXECUTE ON ALL SEGMENTS;

CREATE FUNCTION yagp_stat_messages_reset()
RETURNS void
AS
$$
  SELECT __yagp_stat_messages_reset_f_on_master();
  SELECT __yagp_stat_messages_reset_f_on_segments();
$$
LANGUAGE SQL EXECUTE ON MASTER;

CREATE FUNCTION __yagp_stat_messages_f_on_master()
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'yagp_stat_messages'
LANGUAGE C STRICT VOLATILE EXECUTE ON MASTER;

CREATE FUNCTION __yagp_stat_messages_f_on_segments()
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'yagp_stat_messages'
LANGUAGE C STRICT VOLATILE EXECUTE ON ALL SEGMENTS;

CREATE VIEW yagp_stat_messages AS
  SELECT C.*
	FROM __yagp_stat_messages_f_on_master() as C (
    segid int,
    total_messages bigint,
    send_failures bigint,
    connection_failures bigint,
    other_errors bigint,
    max_message_size int
	)
  UNION ALL
  SELECT C.*
	FROM __yagp_stat_messages_f_on_segments() as C (
    segid int,
    total_messages bigint,
    send_failures bigint,
    connection_failures bigint,
    other_errors bigint,
    max_message_size int
	)
ORDER BY segid;

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

CREATE FUNCTION __yagp_select_log_on_master()
RETURNS SETOF __yagp.log
AS 'MODULE_PATHNAME', 'yagp_select_log'
LANGUAGE C STRICT VOLATILE EXECUTE ON MASTER;

CREATE FUNCTION __yagp_select_log_on_segments()
RETURNS SETOF __yagp.log
AS 'MODULE_PATHNAME', 'yagp_select_log'
LANGUAGE C STRICT VOLATILE EXECUTE ON ALL SEGMENTS;

CREATE VIEW yagp_log AS
  SELECT * FROM __yagp_select_log_on_master()
  UNION ALL
  SELECT * FROM __yagp_select_log_on_segments()
ORDER BY tmid, ssid, ccnt;

CREATE FUNCTION __yagp_truncate_log_on_master()
RETURNS void
AS 'MODULE_PATHNAME', 'yagp_truncate_log'
LANGUAGE C STRICT VOLATILE EXECUTE ON MASTER;

CREATE FUNCTION __yagp_truncate_log_on_segments()
RETURNS void
AS 'MODULE_PATHNAME', 'yagp_truncate_log'
LANGUAGE C STRICT VOLATILE EXECUTE ON ALL SEGMENTS;

CREATE OR REPLACE FUNCTION yagp_truncate_log()
RETURNS void AS $$
BEGIN
    PERFORM __yagp_truncate_log_on_master();
    PERFORM __yagp_truncate_log_on_segments();
END;
$$ LANGUAGE plpgsql VOLATILE;

GRANT SELECT ON yagp_log TO PUBLIC;
GRANT EXECUTE ON FUNCTION yagp_truncate_log() TO PUBLIC;
