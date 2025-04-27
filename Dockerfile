FROM public.ecr.aws/docker/library/alpine:3.22 AS base

FROM base AS build
RUN apk add --no-cache build-base cmake samurai \
                       curl-dev yaml-cpp-dev nlohmann-json

COPY . /src
WORKDIR /src
ARG BUILD_TYPE=debug
RUN cmake -B ./build -G Ninja \
          -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
          -DUSE_DYNAMIC_LINKS=ON && \
    cmake --build ./build && \
    strip ./build/PresenceForPlex

FROM base AS runtime
RUN apk add --no-cache curl yaml-cpp
COPY --from=build --chmod=755 /src/build/PresenceForPlex /app/

ENV HOME=/app XDG_CONFIG_DIR=/config XDG_RUNTIME_DIR=/app/run
VOLUME /config/presence-for-plex
CMD ["/app/PresenceForPlex"]
