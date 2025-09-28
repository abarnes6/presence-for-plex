# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

### Building the Project
```bash
# Configure with CMake preset (recommended)
cmake --preset=debug    # For debug build
cmake --preset=release  # For release build

# Build
cmake --build build/debug    # Debug build
cmake --build build/release  # Release build
```

### Running the Application
```bash
# Debug build
./build/debug/PresenceForPlex

# Release build
./build/release/PresenceForPlex
```