cmake_minimum_required(VERSION 3.28)
project(iphone_usb_camera VERSION 0.1.0 LANGUAGES CXX)

find_package(libobs CONFIG REQUIRED)
add_library(iphone-usb-camera MODULE src/plugin.cpp src/usbmux.cpp)
target_compile_features(iphone-usb-camera PRIVATE cxx_std_17)
target_compile_definitions(iphone-usb-camera PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(iphone-usb-camera PRIVATE OBS::libobs ws2_32 windowscodecs ole32)
set_target_properties(iphone-usb-camera PROPERTIES PREFIX "" OUTPUT_NAME "iphone-usb-camera")
