# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(signal_quality_test)

if(CONFIG_IDF_TARGET_ESP32P4 OR CONFIG_IDF_TARGET_ESP32S31)
    # Enable USB-IF electrical test requests (including TEST_PACKET) in TinyUSB.
    idf_build_set_property(COMPILE_DEFINITIONS "CFG_TUD_TEST_MODE=1" APPEND)
endif()
