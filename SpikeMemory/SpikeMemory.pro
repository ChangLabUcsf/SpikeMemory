QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets printsupport

win32: LIBS += -lws2_32 #-lopengl32 -lgdi32
win32-g++: QMAKE_CXXFLAGS += -Wa,-mbig-obj

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    Biquad.cpp \
    Comm.cpp \
    NetClient.cpp \
    SglxApi.cpp \
    SglxCppClient.cpp \
    Socket.cpp \
    main.cpp \
    controlwindow.cpp \
    qcustomplot.cpp \
    rasterwindow.cpp \
    spikemap.cpp \
    spikevm.cpp \
    waveformwindow.cpp

HEADERS += \
    Biquad.h \
    Comm.h \
    NetClient.h \
    SglxApi.h \
    SglxCppClient.h \
    Socket.h \
    controlwindow.h \
    qcustomplot.h \
    rasterwindow.h \
    spikemap.h \
    spikevm.h \
    waveformwindow.h

FORMS += \
    controlwindow.ui \
    rasterwindow.ui \
    spikemap.ui \
    waveformwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
