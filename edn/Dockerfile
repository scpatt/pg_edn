FROM postgres:17

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        postgresql-server-dev-17 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . /build

RUN make clean || true \
    && make \
    && make install
