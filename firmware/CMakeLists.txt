# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Greeting name on the dashboard. Override with:
#   idf.py -DRLCD_GREETING_NAME="YourName" build
set(RLCD_GREETING_NAME "" CACHE STRING "Greeting name for dashboard header")

# BLE device name. The default "QwenToken" is expanded at runtime to
# QwenToken_XXXX, where XXXX is a 4-character base36 suffix derived from the
# chip BT MAC. Override with a full name only when you need a fixed custom name.
set(RLCD_BLE_DEVICE_NAME "QwenToken" CACHE STRING "BLE advertising name")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(qwen_token_monitor_ble)
