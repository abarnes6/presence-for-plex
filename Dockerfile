FROM public.ecr.aws/docker/library/alpine:3.22 AS base

FROM base AS build
RUN apk add --no-cache build-base cmake samurai git openssl-dev

COPY . /src
WORKDIR /src
ARG BUILD_TYPE=debug
RUN cmake -B ./build -G Ninja -DCMAKE_BUILD_TYPE=$BUILD_TYPE && \
    cmake --build ./build

FROM base AS runtime
RUN apk add --no-cache libstdc++
COPY --from=build --chmod=755 /src/build/PresenceForPlex /app/

ENV HOME=/app XDG_CONFIG_DIR=/config XDG_RUNTIME_DIR=/app/run
VOLUME /config
CMD ["/app/PresenceForPlex"]
