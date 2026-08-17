# Doppler on GT6606 — self-contained (copied from original Doppler/code).
# Board overrides live in ../doppler/Sysinit.c and ../doppler/DopplerApp.c (not here).
# Original Doppler tree is NOT modified.

QT       += core gui widgets
TARGET    = doppler_gt6606
TEMPLATE  = app
CONFIG   += c++11
CONFIG   -= app_bundle

DEFINES += BOARD_GT6606 BOARD_WIDTH=1024 BOARD_HEIGHT=600
DEFINES += _POSIX_C_SOURCE=200809L

DOPPLER_ROOT = $$PWD/doppler_bus

INCLUDEPATH += \
    $$PWD/doppler \
    $$PWD/doppler_ui \
    $$DOPPLER_ROOT/Include \
    $$DOPPLER_ROOT/libota/include

SOURCES += \
    doppler/Sysinit.c \
    doppler/DopplerApp.c \
    doppler_ui/doppler_main.cpp \
    doppler_ui/doppler_mainwindow.cpp \
    doppler_ui/section_view.cpp \
    doppler_ui/board_buzzer.cpp \
    doppler_ui/board_display.cpp \
    doppler_ui/button_beep_filter.cpp

HEADERS += \
    doppler/DopplerApp.h \
    doppler_ui/doppler_mainwindow.h \
    doppler_ui/section_view.h \
    doppler_ui/board_buzzer.h \
    doppler_ui/board_display.h \
    doppler_ui/button_beep_filter.h

SOURCES += \
    $$DOPPLER_ROOT/Source/Command_proc.c \
    $$DOPPLER_ROOT/Source/CRC.c \
    $$DOPPLER_ROOT/Source/Dam5213.c \
    $$DOPPLER_ROOT/Source/FirmwareVersion.c \
    $$DOPPLER_ROOT/Source/HydrologyRound.c \
    $$DOPPLER_ROOT/Source/ListManager.c \
    $$DOPPLER_ROOT/Source/log.c \
    $$DOPPLER_ROOT/Source/ModbusQueue.c \
    $$DOPPLER_ROOT/Source/OtaUpgrade.c \
    $$DOPPLER_ROOT/Source/ReportAck.c \
    $$DOPPLER_ROOT/Source/ringfifo.c \
    $$DOPPLER_ROOT/Source/SectionParam.c \
    $$DOPPLER_ROOT/Source/Sensor.c \
    $$DOPPLER_ROOT/Source/SL651_2014.c \
    $$DOPPLER_ROOT/Source/SocketLink.c \
    $$DOPPLER_ROOT/Source/Timertask.c \
    $$DOPPLER_ROOT/Source/timer_manager.c \
    $$DOPPLER_ROOT/Source/UartProcess.c \
    $$DOPPLER_ROOT/Source/UplinkOffline.c \
    $$DOPPLER_ROOT/libota/src/ota.c \
    $$DOPPLER_ROOT/libota/src/ota_pkg.c \
    $$DOPPLER_ROOT/libota/src/ota_linux_rcs.c \
    $$DOPPLER_ROOT/libota/src/sha256.c

QMAKE_CFLAGS += -std=c11 -Wall
LIBS += -lpthread -lm -lrt
