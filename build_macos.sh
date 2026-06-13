#!/bin/bash
# ModbusHub - macOS Build Script
# Usage: ./build_macos.sh [clean|build|run|all|rebuild]

set -e

# Configuration
BUILD_DIR="build_macos"
APP_NAME="modbus_master"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

print_status() {
    echo -e "${CYAN}=== $1 ===${NC}"
}

check_prerequisites() {
    echo -e "${YELLOW}Checking prerequisites...${NC}"
    
    local missing=0
    
    # Check cmake
    if ! command -v cmake &> /dev/null; then
        echo -e "${RED}✗ cmake not found${NC}"
        echo "  Install: brew install cmake"
        missing=1
    else
        echo -e "${GREEN}✓ cmake $(cmake --version | head -1 | cut -d' ' -f3)${NC}"
    fi
    
    # Check Qt (Homebrew installation)
    if command -v qmake &> /dev/null; then
        echo -e "${GREEN}✓ Qt found${NC}"
    elif [ -d "/opt/homebrew/opt/qt" ] || [ -d "/usr/local/opt/qt" ]; then
        echo -e "${GREEN}✓ Qt found (Homebrew)${NC}"
        # Add Qt to PATH
        if [ -d "/opt/homebrew/opt/qt" ]; then
            export PATH="/opt/homebrew/opt/qt/bin:$PATH"
        else
            export PATH="/usr/local/opt/qt/bin:$PATH"
        fi
    else
        echo -e "${RED}✗ Qt not found${NC}"
        echo "  Install: brew install qt"
        missing=1
    fi
    
    if [ $missing -ne 0 ]; then
        echo -e "\n${RED}Missing dependencies. Please install them first.${NC}"
        exit 1
    fi
    echo
}

do_clean() {
    print_status "Cleaning build directory"
    rm -rf "$SCRIPT_DIR/$BUILD_DIR"
    echo -e "${GREEN}✓ Clean complete${NC}"
}

do_build() {
    check_prerequisites
    print_status "Building ModbusHub"
    
    mkdir -p "$SCRIPT_DIR/$BUILD_DIR"
    cd "$SCRIPT_DIR/$BUILD_DIR"
    
    # Set Qt path for Homebrew
    if [ -d "/opt/homebrew/opt/qt" ]; then
        export CMAKE_PREFIX_PATH="/opt/homebrew/opt/qt"
    elif [ -d "/usr/local/opt/qt" ]; then
        export CMAKE_PREFIX_PATH="/usr/local/opt/qt"
    fi
    
    # Configure if needed
    if [ ! -f "Makefile" ]; then
        echo -e "${YELLOW}Configuring CMake...${NC}"
        cmake .. -DCMAKE_BUILD_TYPE=Release
    fi
    
    # Build
    echo -e "${YELLOW}Compiling...${NC}"
    make -j$(sysctl -n hw.ncpu)
    
    cd "$SCRIPT_DIR"
    
    if [ -f "$BUILD_DIR/$APP_NAME" ]; then
        echo -e "${GREEN}✓ Build successful!${NC}"
        echo "  Executable: $BUILD_DIR/$APP_NAME"
        return 0
    else
        echo -e "${RED}✗ Build failed!${NC}"
        return 1
    fi
}

do_run() {
    if [ ! -f "$SCRIPT_DIR/$BUILD_DIR/$APP_NAME" ]; then
        echo -e "${YELLOW}Executable not found, building first...${NC}"
        do_build
    fi
    
    print_status "Running ModbusHub"
    cd "$SCRIPT_DIR/$BUILD_DIR"
    ./$APP_NAME
}

# Main
echo -e "\n${CYAN}ModbusHub Build System (macOS)${NC}\n"

ACTION=${1:-build}

case "$ACTION" in
    clean)   do_clean ;;
    build)   do_build ;;
    run)     do_run ;;
    rebuild) do_clean; do_build ;;
    all)     do_build && do_run ;;
    *)
        echo "Usage: $0 [clean|build|run|all|rebuild]"
        echo
        echo "  clean   - Remove build directory"
        echo "  build   - Build the application (default)"
        echo "  run     - Run the application"
        echo "  all     - Build and run"
        echo "  rebuild - Clean and build"
        exit 1
        ;;
esac

echo -e "\n${GREEN}Done!${NC}"
