CREATE EXTENSION IF NOT EXISTS edn;

SELECT deconstruct_array_input('{\"hello\", \"world\"}');

-- CREATE TABLE blah (
-- 	hello edn
-- );
--
-- INSERT INTO blah VALUES('{"hello" {"goodbye" "world"}  "blah" {"haha" "yeah"}}');
--
-- SELECT * FROM blah;
--
-- DROP TABLE blah;
DROP EXTENSION edn;
