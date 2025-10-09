#!/bin/bash
# Package build script for Presence For Plex
# Usage: ./scripts/package.sh [platform] [build_type]
# Example: ./scripts/package.sh linux release

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Default values
PLATFORM=${1:-$(uname -s | tr '[:upper:]' '[:lower:]')}
BUILD_TYPE=${2:-Release}
BUILD_DIR="build/release"

echo -e "${GREEN}=== Presence For Plex Package Builder ===${NC}"
echo "Platform: $PLATFORM"
echo "Build Type: $BUILD_TYPE"
echo "Build Directory: $BUILD_DIR"
echo ""

# Detect platform if not specified
case "$PLATFORM" in
    linux|Linux)
        PLATFORM="linux"
        GENERATORS="DEB;RPM;TGZ;STGZ"
        ;;
    darwin|Darwin|macos|macOS)
        PLATFORM="macos"
        GENERATORS="DragNDrop;ZIP"
        ;;
    windows|Windows|win|Win)
        PLATFORM="windows"
        GENERATORS="NSIS;ZIP"
        ;;
    *)
        echo -e "${RED}Unknown platform: $PLATFORM${NC}"
        echo "Supported platforms: linux, macos, windows"
        exit 1
        ;;
esac

# Step 1: Configure
echo -e "${YELLOW}[1/4] Configuring CMake...${NC}"
if [ "$PLATFORM" = "windows" ]; then
    cmake -B "$BUILD_DIR" -S . -G "Ninja" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DBUILD_TESTING=OFF
else
    cmake --preset=release
fi

# Step 2: Build
echo -e "${YELLOW}[2/4] Building...${NC}"
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel

# Step 3: Create packages
echo -e "${YELLOW}[3/4] Creating packages...${NC}"
cd "$BUILD_DIR"

# Create packages directory
mkdir -p packages

# Run CPack
if cpack -C "$BUILD_TYPE" -G "$GENERATORS"; then
    echo -e "${GREEN}✓ Packaging successful${NC}"
else
    echo -e "${RED}✗ Packaging failed${NC}"
    exit 1
fi

# Step 4: List packages
echo -e "${YELLOW}[4/4] Generated packages:${NC}"
echo ""

if [ -d "packages" ] && [ "$(ls -A packages)" ]; then
    ls -lh packages/
else
    # Fallback: search in build directory
    find . -maxdepth 1 -type f \( \
        -name "*.exe" -o \
        -name "*.dmg" -o \
        -name "*.deb" -o \
        -name "*.rpm" -o \
        -name "*.tar.gz" -o \
        -name "*.zip" -o \
        -name "*.sh" \
    \) -exec ls -lh {} \;
fi

echo ""
echo -e "${GREEN}=== Packaging Complete ===${NC}"
echo -e "Packages are located in: ${GREEN}$BUILD_DIR/packages/${NC}"
