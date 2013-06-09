# macosx.mk - 7/6/2013
#
# Goals:
#   Configure an environment that will allow Taulabs GCS and firmware to be built
#   on a Mac OSX system. The environment will support the current versions of Qt SDK
#   and the ARM toolchain installed to either their respective default installation
#   locations, the Taulabs/tools directory, or made available on the system path.
#   
# Requirements:
#   Qt 5.0.2 SDK
#   Qt 4.8.4 Library
#   Git
$(info Importing macosx.mk)

QT_SPEC=macx-g++

UAVOBJGENERATOR="$(BUILD_DIR)/ground/uavobjgenerator/uavobjgenerator"