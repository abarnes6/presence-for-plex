# Packaging Guide

This document describes the packaging system for Presence For Plex across Windows, macOS, and Linux platforms.

## Overview

The project uses CMake + CPack for cross-platform packaging with platform-specific package formats:

- **Windows**: NSIS installer (`.exe`) + Portable ZIP
- **macOS**: DMG disk image (`.dmg`) + ZIP
- **Linux**: DEB (`.deb`) + RPM (`.rpm`) + TGZ (`.tar.gz`) + Self-extracting archive (`.sh`)

## Quick Start

### Local Packaging

#### Windows
```bash
# Using CMake presets
cmake --preset release
cmake --build build/release --config Release
cd build/release
cpack -C Release -G "NSIS;ZIP"
```

#### macOS
```bash
cmake --preset release
cmake --build build/release --config Release
cd build/release
cpack -C Release -G "DragNDrop;ZIP"
```

#### Linux
```bash
cmake --preset release
cmake --build build/release --config Release
cd build/release
cpack -C Release -G "DEB;RPM;TGZ;STGZ"
```

### GitHub Actions (Automated)

Packages are automatically built on:
- **Push to main**: Creates artifacts for all platforms
- **Tag push (v*.*.*)**: Creates GitHub Release with all packages
- **Pull requests**: Builds packages for validation

#### Creating a Release

```bash
# Create and push a tag
git tag v0.5.0
git push origin v0.5.0

# GitHub Actions will automatically:
# 1. Build for all platforms
# 2. Create packages
# 3. Create GitHub Release
# 4. Upload all packages to the release
```

## Package Formats

### Windows Packages

#### NSIS Installer (`.exe`)
- **Location**: `build/release/PresenceForPlex-0.4.0-win64.exe`
- **Features**:
  - GUI installer with customization options
  - Desktop shortcut creation
  - Start Menu integration
  - Uninstaller included
  - Optional auto-start (configurable via app settings)

#### Portable ZIP (`.zip`)
- **Location**: `build/release/PresenceForPlex-0.4.0-win64-portable.zip`
- **Features**:
  - No installation required
  - Extract and run
  - Portable configuration storage

### macOS Packages

#### DMG Disk Image (`.dmg`)
- **Location**: `build/release/PresenceForPlex-0.4.0-macos.dmg`
- **Features**:
  - Drag-and-drop installation to Applications
  - Custom background (if configured)
  - Code-signed (if certificates available)
  - Notarized (if certificates available)

#### Portable ZIP (`.zip`)
- **Location**: `build/release/PresenceForPlex-0.4.0-macos.zip`
- **Features**: Compressed application bundle

### Linux Packages

#### Debian Package (`.deb`)
- **Location**: `build/release/presence-for-plex_0.4.0_amd64.deb`
- **Supported**: Ubuntu, Debian, Linux Mint, Pop!_OS
- **Features**:
  - Desktop entry integration
  - Icon installation
  - Automatic dependency resolution
  - System-wide installation to `/usr`

**Installation**:
```bash
sudo dpkg -i presence-for-plex_0.4.0_amd64.deb
sudo apt-get install -f  # Install dependencies if needed
```

#### RPM Package (`.rpm`)
- **Location**: `build/release/presence-for-plex-0.4.0.x86_64.rpm`
- **Supported**: Fedora, RHEL, CentOS, openSUSE
- **Features**: Same as DEB package

**Installation**:
```bash
sudo rpm -i presence-for-plex-0.4.0.x86_64.rpm
# or
sudo dnf install presence-for-plex-0.4.0.x86_64.rpm
```

#### Tarball (`.tar.gz`)
- **Location**: `build/release/PresenceForPlex-0.4.0-linux-x86_64.tar.gz`
- **Universal Linux package**
- **Manual installation required**

**Installation**:
```bash
tar -xzf PresenceForPlex-0.4.0-linux-x86_64.tar.gz
cd PresenceForPlex-0.4.0-linux-x86_64
sudo cp bin/PresenceForPlex /usr/local/bin/
sudo cp share/applications/presence-for-plex.desktop /usr/share/applications/
sudo cp share/icons/hicolor/256x256/apps/presence-for-plex.png /usr/share/icons/hicolor/256x256/apps/
```

#### Self-Extracting Archive (`.sh`)
- **Location**: `build/release/PresenceForPlex-0.4.0-linux-x86_64.sh`
- **Interactive installer**

**Installation**:
```bash
chmod +x PresenceForPlex-0.4.0-linux-x86_64.sh
./PresenceForPlex-0.4.0-linux-x86_64.sh
```

## Customization

### Package Configuration

Package settings are in `cmake/packaging.cmake`:

```cmake
# Change package metadata
set(CPACK_PACKAGE_VENDOR "Your Name")
set(CPACK_PACKAGE_CONTACT "your@email.com")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://your-website.com")

# Add more package formats
set(CPACK_GENERATOR "NSIS;ZIP;WIX")  # Windows
```

### Windows NSIS Customization

Edit `cmake/packaging.cmake` NSIS section:

```cmake
# Custom welcome message
set(CPACK_NSIS_WELCOME_TITLE "Welcome to Presence For Plex")

# Custom license text
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

# Modify installation behavior
set(CPACK_NSIS_MODIFY_PATH ON)  # Add to PATH
```

### macOS DMG Customization

1. Create custom background: `assets/dmg_background.png` (600x400px)
2. Edit `cmake/packaging/macos_dmg_setup.scpt` for window layout
3. Add code signing:

```cmake
set(CPACK_BUNDLE_APPLE_CERT_APP "Developer ID Application: Your Name")
```

### Linux Desktop Entry

Edit `assets/presence-for-plex.desktop`:

```desktop
[Desktop Entry]
Name=Presence For Plex
Exec=PresenceForPlex %U
Icon=presence-for-plex
Categories=Network;AudioVideo;Player;Qt;
```

## CI/CD Integration

### GitHub Actions Workflow

The workflow (`.github/workflows/cmake-multi-platform.yml`) handles:

1. **Build Matrix**: Ubuntu (gcc/clang), Windows (MSVC), macOS (clang)
2. **Dependency Installation**: Platform-specific package managers
3. **Build**: Release configuration with all optimizations
4. **Package**: All supported formats per platform
5. **Artifact Upload**: 90-day retention
6. **Release Creation**: Automatic on version tags

### Workflow Triggers

```yaml
on:
  push:
    branches: ["main"]      # Build on every push
    tags: ['v*.*.*']        # Create release on version tags
  pull_request:             # Validate PRs
  workflow_dispatch:        # Manual trigger
```

### Manual Workflow Dispatch

From GitHub:
1. Go to Actions tab
2. Select "CMake Multi-Platform Build and Package"
3. Click "Run workflow"
4. Select branch and run

## Package Verification

### Windows
```powershell
# Check installer integrity
Get-FileHash PresenceForPlex-0.4.0-win64.exe -Algorithm SHA256

# Silent install (for testing)
.\PresenceForPlex-0.4.0-win64.exe /S
```

### macOS
```bash
# Verify DMG
hdiutil verify PresenceForPlex-0.4.0-macos.dmg

# Check code signature (if signed)
codesign -vvv --deep --strict /Volumes/Presence\ For\ Plex/Presence\ For\ Plex.app
```

### Linux
```bash
# Verify DEB package
dpkg --info presence-for-plex_0.4.0_amd64.deb
dpkg --contents presence-for-plex_0.4.0_amd64.deb

# Verify RPM package
rpm -qip presence-for-plex-0.4.0.x86_64.rpm
rpm -qlp presence-for-plex-0.4.0.x86_64.rpm
```

## Troubleshooting

### Missing Dependencies

**Linux**: Install build dependencies
```bash
# Debian/Ubuntu
sudo apt-get install build-essential cmake ninja-build rpm dpkg-dev

# Fedora/RHEL
sudo dnf install gcc-c++ cmake ninja-build rpm-build
```

### Package Not Generated

1. Check CMake configuration:
   ```bash
   cmake --build build/release --target help | grep package
   ```

2. Verbose CPack output:
   ```bash
   cpack -C Release -G NSIS --verbose
   ```

3. Check package output directory:
   ```bash
   ls -la build/release/packages/
   ```

### NSIS Not Found (Windows)

Install NSIS:
```bash
# Using Chocolatey
choco install nsis

# Or download from https://nsis.sourceforge.io/
```

### DMG Creation Fails (macOS)

Ensure Xcode Command Line Tools are installed:
```bash
xcode-select --install
```

## Best Practices

1. **Version Bumping**: Update version in `CMakeLists.txt`:
   ```cmake
   project(PresenceForPlex VERSION 0.5.0 LANGUAGES CXX)
   ```

2. **Changelog**: Update README.md or CHANGELOG.md before tagging

3. **Testing**: Always test packages locally before creating release

4. **Signing**:
   - Windows: Use SignTool with code signing certificate
   - macOS: Use `codesign` with Developer ID certificate
   - Linux: Use `debsigs` for DEB, `rpmsign` for RPM

5. **Dependencies**: Keep dependency versions in sync across platforms

## Additional Resources

- [CPack Documentation](https://cmake.org/cmake/help/latest/module/CPack.html)
- [NSIS Documentation](https://nsis.sourceforge.io/Docs/)
- [Debian Packaging Guide](https://www.debian.org/doc/manuals/debian-faq/pkg-basics.en.html)
- [RPM Packaging Guide](https://rpm-packaging-guide.github.io/)
- [macOS Code Signing](https://developer.apple.com/documentation/security/notarizing_macos_software_before_distribution)
