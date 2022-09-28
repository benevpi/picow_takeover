
cmake_minimum_required(VERSION 3.12)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

project(picow_takeover C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.3.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.3.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

# Initialize the SDK
pico_sdk_init()

add_executable(picow_takeover 
        picow_takeover.c
        dhcpserver/dhcpserver.c
        dnsserver/dnsserver.c
        )
		
target_include_directories(picow_takeover  PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts
        ${CMAKE_CURRENT_LIST_DIR}/dhcpserver
        ${CMAKE_CURRENT_LIST_DIR}/dnsserver
        )
		
target_link_libraries(picow_takeover 
        pico_cyw43_arch_lwip_poll
        pico_stdlib
		hardware_adc
        )
if ("${WIFI_SSID}" STREQUAL "")
       message("Wifi SSID not defined, so will work in AP mode")
else()
	message("Wifi SSID defined, so will work in station mode")
	target_compile_definitions(picow_takeover  PRIVATE
        WIFI_SSID=\"${WIFI_SSID}\"
        WIFI_PASSWORD=\"${WIFI_PASSWORD}\"
        )		
endif()

		
pico_enable_stdio_usb(picow_takeover  1)
pico_enable_stdio_uart(picow_takeover  0)

pico_add_extra_outputs(picow_takeover )

# Some test code for the dns server
add_subdirectory(dnsserver)
