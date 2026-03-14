cmake_minimum_required(VERSION 3.16)
project(ModbusHub VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)

# Enable Qt MOC, UIC, RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Compiler flags
add_compile_options(-O3 -DNDEBUG)

# Option to choose between Qt and legacy builds
option(USE_QT "Build with Qt (cross-platform)" ON)
option(USE_QT6 "Use Qt6 instead of Qt5" ON)
# Set ON to produce a fully self-contained binary with no Qt DLL runtime dependency.
# Requires a statically-compiled Qt installation (see BUILD.md for instructions).
option(USE_STATIC_QT "Link Qt statically (no Qt DLL runtime dependency)" OFF)

if(USE_QT)
    message(STATUS "Building Qt version (cross-platform)")
    
    # Find Qt
    if(USE_QT6)
        find_package(Qt6 REQUIRED COMPONENTS Core Widgets Charts Network)
        set(QT_LIBRARIES Qt6::Core Qt6::Widgets Qt6::Charts Qt6::Network)
    else()
        find_package(Qt5 REQUIRED COMPONENTS Core Widgets Network)
        set(QT_LIBRARIES Qt5::Core Qt5::Widgets Qt5::Network)
        message(STATUS "Qt5 detected - Polling graph feature disabled (requires Qt6 Charts)")
    endif()
    
    # Qt sources
    set(SOURCES
        src/ui/main.cpp
        src/ui/mainwindow.cpp
        src/ui/mainwindow_slots.cpp
        src/ui/dialogs/devicedialog.cpp
        src/ui/dialogs/alarmdialog.cpp
        src/backend/src/backend_stubs.c
        src/backend/src/alarm_manager.c
        src/backend/src/modbus.c
        src/backend/src/platform.c
        src/ui/resources/resources.qrc
    )
    
    set(HEADERS
        src/ui/mainwindow.h
        src/ui/dialogs/devicedialog.h
        src/ui/dialogs/alarmdialog.h
        src/ui/widgets/pollinggraphwidget.h
    )
    
    include_directories(
        ${CMAKE_SOURCE_DIR}/src/backend/include
        ${CMAKE_SOURCE_DIR}/src/ui
        ${CMAKE_SOURCE_DIR}/src/ui/dialogs
        ${CMAKE_SOURCE_DIR}/src/ui/widgets
    )
    
    # Executable
    add_executable(modbus_master ${SOURCES} ${HEADERS})
    
    # Static Qt: import platform plugins and link runtimes statically
    # This must come BEFORE target_link_libraries so plugin objects are resolved.
    if(USE_STATIC_QT)
        if(USE_QT6)
            qt_import_plugins(modbus_master
                INCLUDE Qt6::QWindowsIntegrationPlugin
                INCLUDE Qt6::QWindowsVistaStylePlugin
                INCLUDE Qt6::QSvgIconPlugin
            )
        else()
            # Qt5 static plugins are pulled in via Q_IMPORT_PLUGIN in main.cpp;
            # add the compile definition so the stubs are included.
            target_compile_definitions(modbus_master PRIVATE QT_STATICPLUGIN)
        endif()
    endif()
    
    # Link Qt libraries
    target_link_libraries(modbus_master
        ${QT_LIBRARIES}
    )
    
    # Linux-specific libraries
    if(UNIX AND NOT APPLE)
        target_link_libraries(modbus_master pthread m)
        if(USE_STATIC_QT)
            # Full static binary on Linux
            target_link_options(modbus_master PRIVATE -static)
        endif()
    endif()
    
    # Windows-specific libraries
    if(WIN32)
        target_link_libraries(modbus_master ws2_32)
        if(USE_STATIC_QT)
            # Additional Windows system libraries required by a static Qt build
            target_link_libraries(modbus_master
                imm32 dwmapi uxtheme gdi32 comdlg32
                ole32 oleaut32 uuid winmm winspool
                advapi32 shell32 netapi32 shlwapi version
                opengl32
            )
            # Link MinGW C/C++ runtimes into the binary (no libgcc_*.dll / libstdc++*.dll)
            if(MINGW)
                target_link_options(modbus_master PRIVATE
                    -static-libgcc
                    -static-libstdc++
                    -static
                )
            endif()
        endif()
    endif()
    
    # Copy stylesheet files to build directory
    configure_file(${CMAKE_SOURCE_DIR}/src/ui/resources/macos_style.qss ${CMAKE_BINARY_DIR}/macos_style.qss COPYONLY)
    configure_file(${CMAKE_SOURCE_DIR}/src/ui/resources/macos_style_dark.qss ${CMAKE_BINARY_DIR}/macos_style_dark.qss COPYONLY)
    
else()
    # Legacy GTK3 build (Linux only) or Win32 build (Windows only)
    message(STATUS "Building legacy version")
    
    if(WIN32)
        # Windows Win32 native build
        set(SOURCES
            src/windows/main.c
            src/windows/modbus.c
            src/windows/modbus_tcp.c
            src/windows/modbus_rtu.c
            src/windows/packet_monitor.c
            src/windows/data_logger.c
            src/windows/device_manager.c
            src/windows/smtp_client.c
            src/windows/config.c
            src/windows/app_logger.c
            src/common/platform.c
        )
        
        include_directories(
            ${CMAKE_SOURCE_DIR}/src/common
            ${CMAKE_SOURCE_DIR}/src/windows
        )
        
        add_executable(modbus_master ${SOURCES})
        target_link_libraries(modbus_master 
            ws2_32 
            comctl32
            advapi32
        )
    else()
        # Linux GTK3 build
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
        pkg_check_modules(GLIB REQUIRED glib-2.0)
        
        set(SOURCES
            src/linux/main.c
            src/linux/app_logger.c
            src/linux/config.c
            src/linux/data_logger.c
            src/linux/device_manager.c
            src/linux/packet_monitor.c
            src/linux/smtp_client.c
            src/common/platform.c
            src/backend/src/modbus.c
        )
        
        include_directories(
            ${GTK3_INCLUDE_DIRS}
            ${CMAKE_SOURCE_DIR}/src/common
            ${CMAKE_SOURCE_DIR}/src/linux
            ${CMAKE_SOURCE_DIR}/src/backend/include
        )
        link_directories(${GTK3_LIBRARY_DIRS})
        add_definitions(${GTK3_CFLAGS_OTHER})
        
        add_executable(modbus_master ${SOURCES})
        target_link_libraries(modbus_master
            ${GTK3_LIBRARIES}
            ${GLIB_LIBRARIES}
            pthread
            m
        )
    endif()
endif()

# Strip symbols for smaller binary (Release builds on Unix)
if(UNIX AND CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(TARGET modbus_master POST_BUILD
        COMMAND strip $<TARGET_FILE:modbus_master>
        COMMENT "Stripping symbols for smaller binary")
endif()

# Installation
install(TARGETS modbus_master DESTINATION bin)

# Print build info
message(STATUS "=========================================")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "CXX Compiler: ${CMAKE_CXX_COMPILER}")
if(USE_QT)
    if(USE_QT6)
        message(STATUS "Qt version: ${Qt6_VERSION}")
    else()
        message(STATUS "Qt version: ${Qt5_VERSION}")
    endif()
else()
    if(NOT WIN32)
        message(STATUS "GTK3 version: ${GTK3_VERSION}")
    endif()
endif()
message(STATUS "=========================================")
