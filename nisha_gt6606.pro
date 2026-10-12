# 通辽泥沙升降 on GT6606 — 业务来自 code-tongliao 副本，Qt 替代 LoRa 串口屏。
# 原 TES-91/code/code-tongliao 不修改。

QT       += core gui widgets
TARGET    = nisha_gt6606
TEMPLATE  = app
CONFIG   += c++11
CONFIG   -= app_bundle

DEFINES += BOARD_GT6606 BOARD_WIDTH=1024 BOARD_HEIGHT=600
DEFINES += _POSIX_C_SOURCE=200809L

NISHA_ROOT = $$PWD/nisha_bus

INCLUDEPATH += \
    $$PWD/nisha \
    $$PWD/nisha_ui \
    $$NISHA_ROOT/Include \
    $$NISHA_ROOT/libota/include

SOURCES += \
    nisha/NishaApp.c \
    nisha_ui/nisha_main.cpp \
    nisha_ui/nisha_mainwindow.cpp \
    nisha_ui/depth_profile_view.cpp \
    nisha_ui/board_buzzer.cpp \
    nisha_ui/board_display.cpp \
    nisha_ui/button_beep_filter.cpp \
    nisha_ui/screen_idle_filter.cpp

HEADERS += \
    nisha/NishaApp.h \
    nisha_ui/nisha_mainwindow.h \
    nisha_ui/depth_profile_view.h \
    nisha_ui/board_buzzer.h \
    nisha_ui/board_display.h \
    nisha_ui/button_beep_filter.h \
    nisha_ui/screen_idle_filter.h

SOURCES += \
    $$NISHA_ROOT/Source/config.c \
    $$NISHA_ROOT/Source/CRC.c \
    $$NISHA_ROOT/Source/crc_modbus.c \
    $$NISHA_ROOT/Source/data_proc.c \
    $$NISHA_ROOT/Source/dc_motor.c \
    $$NISHA_ROOT/Source/depth_sensor.c \
    $$NISHA_ROOT/Source/encoder.c \
    $$NISHA_ROOT/Source/FirmwareVersion.c \
    $$NISHA_ROOT/Source/hour_frame.c \
    $$NISHA_ROOT/Source/linklist.c \
    $$NISHA_ROOT/Source/log.c \
    $$NISHA_ROOT/Source/lora_screen.c \
    $$NISHA_ROOT/Source/nishatask.c \
    $$NISHA_ROOT/Source/OtaUpgrade.c \
    $$NISHA_ROOT/Source/record_data.c \
    $$NISHA_ROOT/Source/relay_module.c \
    $$NISHA_ROOT/Source/sediment_sensor.c \
    $$NISHA_ROOT/Source/SL651_2014.c \
    $$NISHA_ROOT/Source/SocketLink.c \
    $$NISHA_ROOT/Source/sysutil.c \
    $$NISHA_ROOT/Source/task.c \
    $$NISHA_ROOT/Source/taskqueue.c \
    $$NISHA_ROOT/Source/timertask.c \
    $$NISHA_ROOT/Source/uart.c \
    $$NISHA_ROOT/Source/UplinkOffline.c \
    $$NISHA_ROOT/Source/watch_dog.c \
    $$NISHA_ROOT/Source/zigbee_com.c \
    $$NISHA_ROOT/libota/src/ota.c \
    $$NISHA_ROOT/libota/src/ota_pkg.c \
    $$NISHA_ROOT/libota/src/ota_linux_rcs.c \
    $$NISHA_ROOT/libota/src/sha256.c

QMAKE_CFLAGS += -std=c11 -Wall
LIBS += -lpthread -lm -lrt
