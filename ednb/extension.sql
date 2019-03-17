CREATE EXTENSION ednb;

CREATE TABLE blah (
	hello ednb
);

INSERT INTO blah VALUES('hello');

SELECT * FROM blah;

DROP TABLE blah;
DROP EXTENSION ednb;
