# -------------------------------------------------
# Project created by QtCreator 2010-03-21T20:44:17
# -------------------------------------------------
QT += xml
QT -= gui

macx {
    QMAKE_CFLAGS_X86_64 += -mmacosx-version-min=10.7
    QMAKE_CXXFLAGS_X86_64 = $$QMAKE_CFLAGS_X86_64
}

win32 {
    QMAKE_LFLAGS += -static
    #QMAKE_CXXFLAGS += -static-libgcc
}

TARGET = uavobjgenerator
CONFIG += console
CONFIG -= app_bundle
TEMPLATE = app
SOURCES += main.cpp \
    uavobjectparser.cpp \
    generators/generator_io.cpp \
    generators/java/uavobjectgeneratorjava.cpp \
    generators/flight/uavobjectgeneratorflight.cpp \
    generators/gcs/uavobjectgeneratorgcs.cpp \
    generators/matlab/uavobjectgeneratormatlab.cpp \
    generators/python/uavobjectgeneratorpython.cpp \
    generators/wireshark/uavobjectgeneratorwireshark.cpp \
    generators/generator_common.cpp
HEADERS += uavobjectparser.h \
    generators/generator_io.h \
    generators/java/uavobjectgeneratorjava.h \
    generators/gcs/uavobjectgeneratorgcs.h \
    generators/matlab/uavobjectgeneratormatlab.h \
    generators/python/uavobjectgeneratorpython.h \
    generators/wireshark/uavobjectgeneratorwireshark.h \
    generators/generator_common.h

# On Windows, copying the dlls into the same directory assures the uavobjgenerator.exe
# will find them at run time even if Qt isn't on the path for some reason
win32:CONFIG(debug, debug|release) {
    QtCored4.commands = $(COPY_FILE) $$[QT_INSTALL_BINS]\\QtCored4.dll debug
    QtCored4.target = debug/QtCored4.dll

    QtXmld4.commands = $(COPY_FILE) $$[QT_INSTALL_BINS]\\QtXmld4.dll debug
    QtXmld4.target = debug/QtXmld4.dll

    MinGWdw2.commands = $(COPY_FILE) $$[QT_INSTALL_BINS]\\libgcc_s_dw2-1.dll debug
    MinGWdw2.target = debug/libgcc_s_dw2-1.dll

    MinGWwm.commands = $(COPY_FILE) $$[QT_INSTALL_BINS]\\mingwm10.dll debug
    MinGWwm.target = debug/mingwm10.dll

#    MinGWsjlj.commands = $(COPY_FILE) $$[QT_INSTALL_BINS]\\libgcc_s_sjlj-1.dll debug
#    MinGWsjlj.target = debug/libgcc_s_sjlj-1.dll

    QMAKE_EXTRA_TARGETS += QtCored4 QtXmld4 MinGWdw2 MinGWwm
    PRE_TARGETDEPS += debug/QtCored4.dll debug/QtXmld4.dll
    PRE_TARGETDEPS += debug/libgcc_s_dw2-1.dll debug/mingwm10.dll
} else win32:CONFIG(release, debug|release) {
    QtCore4.commands = $(COPY_FILE) $$[QT_INSTALL_BINS]\\QtCore4.dll debug
    QtCore4.target = debug/QtCore4.dll
    QtXml4.commands = $(COPY_FILE) $$[QT_INSTALL_BINS]\\QtXml4.dll debug
    QtXml4.target = debug/QtXml4.dll
    QMAKE_EXTRA_TARGETS += QtCore4 QtXml4
    PRE_TARGETDEPS += debug/QtCore4.dll debug/QtXml4.dll
}

