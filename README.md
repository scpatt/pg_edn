# pg_edn

A Postgres extension that adds an `edn` type for [EDN](https://github.com/edn-format/edn) data.

Early days. Right now the parser handles maps and strings (nested to any depth).
It validates the input (balanced maps, even number of forms, unique keys,
closed strings) and prints it back in a canonical form. Everything else in the
EDN spec (nil, booleans, numbers, symbols, keywords, vectors, lists, sets,
tagged elements) is still TODO.

## Example

```sql
CREATE EXTENSION edn;

SELECT '{"name" "sam" "nested" {"k" "v"}}'::edn;
--            edn
-- ------------------------------------
--  {"name" "sam", "nested" {"k" "v"}}

CREATE TABLE docs (id serial, body edn);
INSERT INTO docs (body) VALUES ('{"a" "b"}');
```

Bad input is rejected: duplicate keys, an odd number of map forms, unclosed
strings, trailing junk.

## Building

It's a PGXS extension, so you need a Postgres whose `pg_config` is on your PATH.

I use [mise](https://mise.jdx.dev/) so the Postgres toolchain stays out of the
system package manager. The version is pinned in `mise.toml`; `mise install`
builds it under `~/.local/share/mise`, and `mise uninstall postgres@17.10` takes
it away again. As a bonus it gives the editor real macOS headers for
IntelliSense (wired up in `.vscode/`).

```sh
mise install
cd edn
mise exec -- make
mise exec -- make install
```

Then, in a database:

```sql
CREATE EXTENSION edn;
```

If you already have Postgres set up, skip mise and just run `make` with its
`pg_config` on your PATH.

## Tests

`edn/run.sh` builds the extension inside a `postgres:17` container and runs
`edn/test/edn_test.sql` against it. No local Postgres needed.

```sh
cd edn
./run.sh
```

## Layout

- `edn/edn.c` — the type's in/out functions and the parser
- `edn/edn--0.0.1.sql`, `edn/edn.control` — extension SQL and control file
- `edn/test/edn_test.sql` — the tests
- `edn/Dockerfile`, `edn/run.sh` — containerised build + test
