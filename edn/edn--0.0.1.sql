\echo Use "CREATE EXTENSION edn" to load this file. \quit

CREATE FUNCTION edn_in(cstring)
RETURNS edn
AS '$libdir/edn'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION edn_out(edn)
RETURNS cstring
AS '$libdir/edn'
LANGUAGE C IMMUTABLE STRICT;

CREATE TYPE edn (
  INPUT = edn_in,
  OUTPUT = edn_out
);

CREATE FUNCTION deconstruct_array_input(text[])
RETURNS cstring
AS '$libdir/edn'
LANGUAGE C IMMUTABLE STRICT;
