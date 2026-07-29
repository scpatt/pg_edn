-- Round-trip, validation, and error tests for the edn type.
-- Runs without ON_ERROR_STOP so the expected-error cases display their
-- messages instead of aborting the whole script.

CREATE EXTENSION IF NOT EXISTS edn;

\echo '################ round-trip / canonicalization ################'

\echo '== string =='
SELECT '"hello"'::edn;

\echo '== empty map =='
SELECT '{}'::edn;

\echo '== simple map =='
SELECT '{"a" "b"}'::edn;

\echo '== multi-entry map (canonical spacing) =='
SELECT '{"a" "b" "c" "d"}'::edn;

\echo '== nested map =='
SELECT '{"a" {"b" "c"}}'::edn;

\echo '== whitespace + commas normalized =='
SELECT '{  "a"  ,  "b"  }'::edn;

\echo '== stored in a table, survives insert/select =='
CREATE TEMP TABLE t (id int, doc edn);
INSERT INTO t VALUES (1, '{"name" "sam"}'), (2, '{"nested" {"k" "v"}}');
SELECT id, doc FROM t ORDER BY id;

\echo '################ expected errors ################'

\echo '== duplicate key (top level) =='
SELECT '{"a" "b" "a" "c"}'::edn;

\echo '== duplicate key (nested) =='
SELECT '{"a" {"b" "c" "b" "d"}}'::edn;

\echo '== odd number of forms =='
SELECT '{"a"}'::edn;

\echo '== unterminated string =='
SELECT '{"a" "b}'::edn;

\echo '== trailing garbage =='
SELECT '{} extra'::edn;

\echo '== unsupported top-level value =='
SELECT '42'::edn;

\echo '== unexpected end of input =='
SELECT '{'::edn;
