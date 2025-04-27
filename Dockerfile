FROM public.ecr.aws/docker/library/alpine:3.22 AS base

FROM base AS build
RUN apk add --no-cache build-base cmake samurai \
                       linux-headers pkgconf git curl perl zip bash

ARG VCPKG_VERSION=2025.04.09
ENV VCPKG_ROOT=/vcpkg VCPKG_FORCE_SYSTEM_BINARIES=1 \
    VCPKG_DOWNLOADS=/root/.cache/vcpkg/downloads \
ADD https://github.com/microsoft/vcpkg.git#$VCPKG_VERSION $VCPKG_ROOT
RUN mkdir -p $VCPKG_DOWNLOADS && \
    $VCPKG_ROOT/bootstrap-vcpkg.sh -disableMetrics

COPY . /src
WORKDIR /src
ARG BUILD_TYPE=debug
RUN --mount=type=cache,target=/root/.cache/vcpkg \
    cmake -B ./build -G Ninja \
          -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
          -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
          -DCMAKE_EXE_LINKER_FLAGS=-static && \
    cmake --build ./build

FROM base AS runtime
COPY --from=build --chmod=755 /src/build/PresenceForPlex /app/

ENV HOME=/app XDG_RUNTIME_DIR=/app
VOLUME /app/.config
CMD ["/app/PresenceForPlex"]
