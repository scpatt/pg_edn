#!/usr/bin/env bash
# Build the edn extension in a Postgres 17 container, boot it, and run the
# test suite. Requires Docker.
set -euo pipefail

IMAGE=pg_edn_test
CONTAINER=pg_edn_run
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cleanup() { docker rm -f "$CONTAINER" >/dev/null 2>&1 || true; }
trap cleanup EXIT

echo ">> building image ($IMAGE)"
docker build -t "$IMAGE" "$HERE"

echo ">> starting container ($CONTAINER)"
cleanup
docker run --name "$CONTAINER" -e POSTGRES_PASSWORD=pw -d "$IMAGE" >/dev/null

echo ">> waiting for postgres"
for _ in $(seq 1 30); do
  if docker exec "$CONTAINER" pg_isready -U postgres >/dev/null 2>&1; then break; fi
  sleep 1
done

echo ">> running tests"
docker exec -i "$CONTAINER" psql -U postgres -f - < "$HERE/test/edn_test.sql"
