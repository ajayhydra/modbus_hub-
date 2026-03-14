#!/bin/bash
# ModbusHub - Linux Build Script
# Usage: ./build_linux.sh [clean|build|run|all|rebuild]

set -e

# Configuration
BUILD_DIR="build_linux"
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
        echo "  Install: sudo apt install cmake"
        missing=1
    else
        echo -e "${GREEN}✓ cmake$(cmake --version | head -1 | cut -d' ' -f3)${NC}"
    fi
    
    # Check Qt6 (required for Polling Graph / QtCharts)
    if command -v qmake6 &> /dev/null || pkg-config --exists Qt6Core 2>/dev/null; then
        echo -e "${GREEN}✓ Qt6 found${NC}"
    else
        echo -e "${RED}✗ Qt6 not found${NC}"
        echo "  Install: sudo apt install qt6-base-dev qt6-charts-dev"
        missing=1
    fi

    # Check Qt6 Charts (required for Polling Graph)
    if pkg-config --exists Qt6Charts 2>/dev/null || dpkg -l libqt6charts6-dev > /dev/null 2>&1; then
        echo -e "${GREEN}✓ Qt6 Charts found${NC}"
    else
        echo -e "${RED}✗ Qt6 Charts not found${NC}"
        echo "  Install: sudo apt install qt6-charts-dev"
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
    
    # Configure if needed
    if [ ! -f "Makefile" ]; then
        echo -e "${YELLOW}Configuring CMake...${NC}"
        cmake .. -DCMAKE_BUILD_TYPE=Release -DUSE_QT6=ON
    fi
    
    # Build
    echo -e "${YELLOW}Compiling...${NC}"
    make -j$(nproc)
    
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
echo -e "\n${CYAN}ModbusHub Build System (Linux)${NC}\n"

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
