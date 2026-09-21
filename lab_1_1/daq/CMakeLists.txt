# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

# Pico W: the onboard LED hangs off the CYW43439, not off an RP2040 GPIO, so the
# board type has to be set before pico_sdk_import.cmake is included - the SDK
# caches it the moment that file runs.
set(PICO_BOARD pico_w CACHE STRING "Board type")
set(PROGRAM_NAME daq)

include(pico_sdk_import.cmake)

project(${PROGRAM_NAME} C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Local copies of the two drivers, so you can tune them (IMU output data rate,
# SDIO baud rate) without disturbing lab_1_1/imu_example or lab_1_1/sd_card_example.
add_subdirectory(sd_card_driver sd_card_driver_build)
add_subdirectory(icm20948 icm20948_build)

add_executable(${PROGRAM_NAME}
    config/hw_config.c
    main.c
)

# Core0 runs FatFs, core1 only reads the IMU. Both stacks live in the RP2040's
# dedicated 4 kB scratch banks (core0 in SCRATCH_Y, core1 in SCRATCH_X), so 0x1000
# is the ceiling - ask for more and the link fails with "region overflowed".
# That is why the f_mkfs work buffer and the sample block buffer are static.
target_compile_definitions(${PROGRAM_NAME} PRIVATE
    PICO_STACK_SIZE=0x1000
    PICO_CORE1_STACK_SIZE=0x1000
)

target_compile_options(${PROGRAM_NAME} PRIVATE
    -Wall
    -Wextra
    -Wshadow
    # Both stacks are capped at 0x1000 above, so a frame that eats half of one is
    # already a bug. Same flag the sibling lab_1_1 examples build with.
    -Wstack-usage=2048
)

set_property(TARGET ${PROGRAM_NAME} APPEND_STRING PROPERTY LINK_FLAGS
    "-Wl,--print-memory-usage"
)

pico_set_program_name(${PROGRAM_NAME} "${PROGRAM_NAME}")
pico_set_program_version(${PROGRAM_NAME} "0.1")

# USB stdio only. It also registers the reset interface, so `stty -f <port> 1200`
# from macOS reboots the board into BOOTSEL - no button press needed.
pico_enable_stdio_usb(${PROGRAM_NAME} 1)
pico_enable_stdio_uart(${PROGRAM_NAME} 0)

target_include_directories(${PROGRAM_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/icm20948
)

target_link_libraries(${PROGRAM_NAME}
    pico_stdlib
    hardware_i2c
    pico_multicore
    pico_util
    sd_custom_driver
    hardware_clocks
    hardware_adc
    hardware_pio
    pico_cyw43_arch_none
    icm20948
)

pico_add_extra_outputs(${PROGRAM_NAME})
