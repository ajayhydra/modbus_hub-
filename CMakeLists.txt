cmake_minimum_required(VERSION 3.16)
project(ModbusHub VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)

# Enable Qt MOC, RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Compiler flags
add_compile_options(-O3 -DNDEBUG)

option(USE_QT6 "Use Qt6 instead of Qt5" ON)
# Set ON to produce a fully self-contained binary with no Qt DLL runtime
# dependency. Requires a statically-compiled Qt installation.
option(USE_STATIC_QT "Link Qt statically (no Qt DLL runtime dependency)" OFF)

# Find Qt (Core + Widgets + Network — no Charts, no extra modules)
if(USE_QT6)
    find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
    set(QT_LIBRARIES Qt6::Core Qt6::Widgets Qt6::Network)
else()
    find_package(Qt5 REQUIRED COMPONENTS Core Widgets Network)
    set(QT_LIBRARIES Qt5::Core Qt5::Widgets Qt5::Network)
endif()

set(SOURCES
    src/ui/main.cpp
    src/ui/mainwindow.cpp
    src/ui/mainwindow_slots.cpp
    src/backend/src/modbus.c
    src/backend/src/platform.c
    src/backend/src/config.c
    src/backend/src/app_logger.c
    src/backend/src/data_logger.c
    src/backend/src/packet_monitor.c
    src/ui/resources/resources.qrc
)

# Embed the application icon into the Windows executable
if(WIN32)
    list(APPEND SOURCES src/ui/resources/app_logo.rc)
    # Copy the .ico next to the build artefact so the .rc file can find it
    configure_file(
        ${CMAKE_SOURCE_DIR}/src/ui/resources/icons/app_logo.ico
        ${CMAKE_BINARY_DIR}/app_logo.ico
        COPYONLY)
endif()

set(HEADERS
    src/ui/mainwindow.h
)

include_directories(
    ${CMAKE_SOURCE_DIR}/src/backend/include
    ${CMAKE_SOURCE_DIR}/src/ui
)

add_executable(modbus_master ${SOURCES} ${HEADERS})

# Static Qt: import platform plugins and link runtimes statically.
# Must come BEFORE target_link_libraries so plugin objects are resolved.
if(USE_STATIC_QT)
    if(USE_QT6)
        qt_import_plugins(modbus_master
            INCLUDE Qt6::QWindowsIntegrationPlugin
            INCLUDE Qt6::QWindowsVistaStylePlugin
            INCLUDE Qt6::QSvgIconPlugin
        )
    else()
        # Qt5 static plugins are pulled in via Q_IMPORT_PLUGIN in main.cpp.
        target_compile_definitions(modbus_master PRIVATE QT_STATICPLUGIN)
    endif()
endif()

target_link_libraries(modbus_master ${QT_LIBRARIES})

# Linux-specific libraries
if(UNIX AND NOT APPLE)
    target_link_libraries(modbus_master pthread m)
    if(USE_STATIC_QT)
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
configure_file(${CMAKE_SOURCE_DIR}/src/ui/resources/macos_style.qss      ${CMAKE_BINARY_DIR}/macos_style.qss      COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/src/ui/resources/macos_style_dark.qss ${CMAKE_BINARY_DIR}/macos_style_dark.qss COPYONLY)

# Strip symbols for smaller binary (Release builds on Unix)
if(UNIX AND CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(TARGET modbus_master POST_BUILD
        COMMAND strip $<TARGET_FILE:modbus_master>
        COMMENT "Stripping symbols for smaller binary")
endif()

# Installation
install(TARGETS modbus_master DESTINATION bin)

if(WIN32)
    # Install the application icon so the installer / Start-Menu shortcut can use it
    install(FILES ${CMAKE_SOURCE_DIR}/src/ui/resources/icons/app_logo.ico
            DESTINATION bin)
endif()

# -----------------------------------------------------------------------
# CPack – Windows NSIS installer with Start Menu shortcut
# -----------------------------------------------------------------------
if(WIN32)
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_PACKAGE_NAME        "ModbusHub")
    set(CPACK_PACKAGE_VENDOR      "ModbusHub")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Modbus TCP/RTU master application")
    set(CPACK_PACKAGE_VERSION     "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "ModbusHub")
    set(CPACK_PACKAGE_EXECUTABLES "modbus_master" "ModbusHub")

    set(CPACK_NSIS_MUI_ICON
        "${CMAKE_SOURCE_DIR}/src/ui/resources/icons/app_logo.ico")
    set(CPACK_NSIS_MUI_UNIICON
        "${CMAKE_SOURCE_DIR}/src/ui/resources/icons/app_logo.ico")

    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortcut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\ModbusHub.lnk' \\
            '$INSTDIR\\\\bin\\\\modbus_master.exe' '' \\
            '$INSTDIR\\\\bin\\\\app_logo.ico' 0")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "Delete '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\ModbusHub.lnk'")

    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\app_logo.ico")
    set(CPACK_NSIS_MODIFY_PATH ON)

    include(CPack)
endif()

# Print build info
message(STATUS "=========================================")
message(STATUS "ModbusHub ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
if(USE_QT6)
    message(STATUS "Qt version: ${Qt6_VERSION}")
else()
    message(STATUS "Qt version: ${Qt5_VERSION}")
endif()
message(STATUS "=========================================")
