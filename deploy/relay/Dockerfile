# NexLevel relay server. Build from the NEXLEVEL folder:
#   docker build -f deploy/relay/Dockerfile -t nexlevel-relay .
#   docker run -d --restart unless-stopped -p 47800:47800 --name nexlevel-relay nexlevel-relay
FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ cmake ninja-build ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY core /src/core
RUN cmake -S /src/core -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DNEXLEVEL_BUILD_TESTS=OFF -DNEXLEVEL_BUILD_TOOLS=ON \
    && cmake --build /build --target nexlevel_relay

FROM debian:bookworm-slim
COPY --from=build /build/nexlevel_relay /usr/local/bin/nexlevel_relay
USER nobody
EXPOSE 47800
ENTRYPOINT ["/usr/local/bin/nexlevel_relay", "--port", "47800"]
