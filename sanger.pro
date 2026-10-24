QT += core gui qml quick quickcontrols2 dbus

CONFIG += c++20 release
TARGET = sanger
TEMPLATE = app

HEADERS += \
    src/alignmentview.h \
    src/assembly.h \
    src/backend.h \
    src/basecolors.h \
    src/cli.h \
    src/filepicker.h \
    src/portalfilepicker.h \
    src/sequence.h \
    src/theme.h \
    src/trace.h \
    src/traceview.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/alignmentview.cpp \
    src/assembly.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/portalfilepicker.cpp \
    src/sequence.cpp \
    src/theme.cpp \
    src/trace.cpp \
    src/traceview.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
