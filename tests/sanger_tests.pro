QT += core gui quick quickcontrols2 testlib dbus
CONFIG += c++20 testcase
TARGET = sanger_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/assembly.h ../src/backend.h ../src/filepicker.h ../src/portalfilepicker.h \
           ../src/sequence.h ../src/theme.h ../src/trace.h ../src/translator.h fixtures.h
SOURCES += sanger_tests.cpp ../src/assembly.cpp ../src/backend.cpp ../src/portalfilepicker.cpp \
           ../src/sequence.cpp ../src/theme.cpp ../src/trace.cpp ../src/translator.cpp
