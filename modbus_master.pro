QT       += core gui widgets
CONFIG   += c++17

# QtCharts is only available in Qt6
greaterThan(QT_MAJOR_VERSION, 5) {
    QT += charts
    message("Qt6 detected - Polling graph feature enabled")
} else {
    message("Qt5 detected - Polling graph feature disabled (requires Qt6)")
}

TARGET = modbus_master
TEMPLATE = app

# Compiler flags
QMAKE_CXXFLAGS += -O3
QMAKE_CFLAGS += -O3

# Common Qt sources
SOURCES += \
    src/qt/main.cpp \
    src/qt/mainwindow.cpp \
    src/qt/mainwindow_slots.cpp \
    src/qt/devicedialog.cpp \
    src/qt/alarmdialog.cpp \
    src/common/platform.c

# Platform-specific backend sources
unix {
    SOURCES += \
        src/linux/app_logger.c \
        src/linux/config.c \
        src/linux/data_logger.c \
        src/linux/device_manager.c \
        src/linux/packet_monitor.c \
        src/linux/smtp_client.c
}

win32 {
    SOURCES += \
        src/qt/backend_stubs.c \
        src/qt/alarm_manager.c \
        src/qt/modbus.c
}

HEADERS += \
    src/qt/mainwindow.h \
    src/qt/devicedialog.h \
    src/qt/alarmdialog.h \
    src/qt/pollinggraphwidget.h \
    src/common/app_logger.h \
    src/common/config.h \
    src/common/data_logger.h \
    src/common/device_manager.h \
    src/common/modbus.h \
    src/common/packet_monitor.h \
    src/common/platform.h \
    src/common/smtp_client.h

INCLUDEPATH += \
    src/common \
    src/qt

# System libraries
unix {
    LIBS += -lpthread -lm
}

win32 {
    # Custom modbus.c used — no external libmodbus required
    LIBS += -lws2_32 -ladvapi32
    # For MSVC
    msvc {
        LIBS += -L$$PWD/lib
    }
    # For MinGW
    mingw {
        LIBS += -L$$PWD/lib
    }
}

# Default rules for deployment
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# Stylesheet files
DISTFILES += \
    src/qt/macos_style.qss \
    src/qt/macos_style_dark.qss

# Copy stylesheets to build directory
stylefiles.files = src/qt/macos_style.qss src/qt/macos_style_dark.qss
stylefiles.path = $$OUT_PWD
INSTALLS += stylefiles
