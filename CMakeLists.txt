cmake_minimum_required(VERSION 3.16)
project(send_epaper CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# NFC backend selection: RCS380 (default) or LIBNFC
option(NFC_BACKEND_LIBNFC "Use libnfc backend (for PN532, ACR122U, etc.)" OFF)

# Find dependencies via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(LZO2 REQUIRED lzo2)

# Library source files (excluding main.cpp)
set(LIB_SOURCES
    src/nfc_eink.cpp
    src/protocol.cpp
    src/image.cpp
    src/dither.cpp
)

# Backend-specific sources and dependencies
if(NFC_BACKEND_LIBNFC)
    message(STATUS "NFC Backend: libnfc")
    pkg_check_modules(LIBNFC REQUIRED libnfc)
    list(APPEND LIB_SOURCES src/transport_libnfc.cpp)
    set(BACKEND_INCLUDE_DIRS ${LIBNFC_INCLUDE_DIRS})
    set(BACKEND_LIBRARY_DIRS ${LIBNFC_LIBRARY_DIRS})
    set(BACKEND_LIBRARIES    ${LIBNFC_LIBRARIES})
    set(BACKEND_DEFINE       NFC_BACKEND_LIBNFC)
else()
    message(STATUS "NFC Backend: RC-S380 (libusb)")
    pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
    list(APPEND LIB_SOURCES src/transport_rcs380.cpp)
    set(BACKEND_INCLUDE_DIRS ${LIBUSB_INCLUDE_DIRS})
    set(BACKEND_LIBRARY_DIRS ${LIBUSB_LIBRARY_DIRS})
    set(BACKEND_LIBRARIES    ${LIBUSB_LIBRARIES})
    set(BACKEND_DEFINE       NFC_BACKEND_RCS380)
endif()

# Define the static library
add_library(NfcEink STATIC ${LIB_SOURCES})

target_include_directories(NfcEink PUBLIC
    include
)

target_include_directories(NfcEink PRIVATE
    src
    third_party
    /opt/homebrew/include
    ${BACKEND_INCLUDE_DIRS}
)

# Important: Library directories must be set for the link step
link_directories(
    ${LZO2_LIBRARY_DIRS}
    ${BACKEND_LIBRARY_DIRS}
)

target_link_libraries(NfcEink PUBLIC
    ${LZO2_LIBRARIES}
    ${BACKEND_LIBRARIES}
)

target_compile_definitions(NfcEink PRIVATE ${BACKEND_DEFINE})
target_compile_options(NfcEink PRIVATE -Wall -Wextra)

# Define the main executable
add_executable(send_epaper main.cpp)

target_link_libraries(send_epaper PRIVATE NfcEink)
target_compile_options(send_epaper PRIVATE -Wall -Wextra)
