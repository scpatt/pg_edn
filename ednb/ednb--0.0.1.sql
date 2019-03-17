\echo Use "CREATE EXTENSION ednb" to load this file. \quit

CREATE FUNCTION ednb_in(cstring)
RETURNS ednb
AS '$libdir/ednb'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION ednb_out(ednb)
RETURNS cstring
AS '$libdir/ednb'
LANGUAGE C IMMUTABLE STRICT;

CREATE TYPE ednb (
  INPUT = ednb_in,
  OUTPUT = ednb_out
);
