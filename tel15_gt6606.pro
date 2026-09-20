# TEL-15 渠道测流 on GT6606 — Qt 全屏（业务拷自 TEL-15-渠道测流/code）
# 板级适配在 tel15/Sysinit.c、tel15/Tel15App.c，不编译 bus 里的 Sysinit.c。
# 原 TEL-15 工程不修改。

QT       += core gui widgets
TARGET    = tel15_gt6606
TEMPLATE  = app
CONFIG   += c++11
CONFIG   -= app_bundle

DEFINES += BOARD_GT6606 BOARD_WIDTH=1024 BOARD_HEIGHT=600
DEFINES += _POSIX_C_SOURCE=200809L

TEL15_ROOT = $$PWD/tel15_bus

INCLUDEPATH += \
    $$PWD/tel15 \
    $$PWD/tel15_ui \
    $$TEL15_ROOT/Include \
    $$TEL15_ROOT/libota/include

SOURCES += \
    tel15/Sysinit.c \
    tel15/Tel15App.c \
    tel15_ui/tel15_main.cpp \
    tel15_ui/tel15_mainwindow.cpp \
    tel15_ui/section_view.cpp \
    tel15_ui/board_buzzer.cpp \
    tel15_ui/board_display.cpp \
    tel15_ui/button_beep_filter.cpp

HEADERS += \
    tel15/Tel15App.h \
    tel15_ui/tel15_mainwindow.h \
    tel15_ui/section_view.h \
    tel15_ui/board_buzzer.h \
    tel15_ui/board_display.h \
    tel15_ui/button_beep_filter.h

SOURCES += \
    $$TEL15_ROOT/Source/AMPSTechMotor.c \
    $$TEL15_ROOT/Source/Command_proc.c \
    $$TEL15_ROOT/Source/CRC.c \
    $$TEL15_ROOT/Source/DC_MotorCmu.c \
    $$TEL15_ROOT/Source/Encoder.c \
    $$TEL15_ROOT/Source/FirmwareVersion.c \
    $$TEL15_ROOT/Source/LinkList.c \
    $$TEL15_ROOT/Source/ListManager.c \
    $$TEL15_ROOT/Source/log.c \
    $$TEL15_ROOT/Source/ModbusQueue.c \
    $$TEL15_ROOT/Source/OtaUpgrade.c \
    $$TEL15_ROOT/Source/Relay.c \
    $$TEL15_ROOT/Source/ringfifo.c \
    $$TEL15_ROOT/Source/Sensor.c \
    $$TEL15_ROOT/Source/SL651_2014.c \
    $$TEL15_ROOT/Source/SocketLink.c \
    $$TEL15_ROOT/Source/TaskQueue.c \
    $$TEL15_ROOT/Source/threadpool.c \
    $$TEL15_ROOT/Source/Timertask.c \
    $$TEL15_ROOT/Source/timer_manager.c \
    $$TEL15_ROOT/Source/UartProcess.c \
    $$TEL15_ROOT/Source/UplinkOffline.c \
    $$TEL15_ROOT/Source/WebSeriver.c \
    $$TEL15_ROOT/Source/ZigbeeDataProc.c \
    $$TEL15_ROOT/libota/src/ota.c \
    $$TEL15_ROOT/libota/src/ota_pkg.c \
    $$TEL15_ROOT/libota/src/ota_linux_rcs.c \
    $$TEL15_ROOT/libota/src/sha256.c

QMAKE_CFLAGS += -std=c11 -Wall
LIBS += -lpthread -lm -lrt
