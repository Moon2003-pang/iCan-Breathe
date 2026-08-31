QT += widgets network

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    healthreportdialog.cpp \
    main.cpp \
    realtimechart.cpp \
    widget.cpp \
    widget_ui.cpp \
    widget_comm.cpp \
    widget_alert.cpp \
    widget_data.cpp

HEADERS += \
    healthreportdialog.h \
    realtimechart.h \
    widget.h

FORMS += \
    widget.ui

android {
    ANDROID_PACKAGE_SOURCE_DIR = $$PWD/android
}

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
