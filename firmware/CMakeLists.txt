cmake_minimum_required(VERSION 3.20)

# ──────────────────────────── Toolchain ────────────────────────────
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_SIZE arm-none-eabi-size)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ──────────────────────────── Project ────────────────────────────
project(asset-tracker C ASM)

# ──────────────────────────── MCU Configuration ────────────────────────────
set(MCU_FAMILY STM32L4xx)
set(MCU_DEVICE STM32L4R5xx)
set(MCU_LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/STM32L4R5ZITx_FLASH.ld)

# CPU flags for Cortex-M4 with FPU
set(CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")

# ──────────────────────────── Compiler Flags ────────────────────────────
set(CMAKE_C_FLAGS "${CPU_FLAGS} -Wall -Wextra -Werror -ffunction-sections -fdata-sections -fno-common")
set(CMAKE_C_FLAGS_DEBUG "-Og -g3 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-Os -DNDEBUG")

# ──────────────────────────── HAL Driver Path ────────────────────────────
# Set this to your STM32CubeL4 installation path
set(HAL_DRIVER_PATH "" CACHE PATH "Path to STM32CubeL4 HAL drivers")

if(NOT HAL_DRIVER_PATH)
    message(WARNING "HAL_DRIVER_PATH not set. Set it to your STM32CubeL4 installation.")
    message(WARNING "Example: cmake -DHAL_DRIVER_PATH=/path/to/STM32CubeL4/Drivers ..")
endif()

# ──────────────────────────── Source Files ────────────────────────────
set(FIRMWARE_SOURCES
    # Startup & Main
    startup_stm32l4r5xx.s
    src/main.c

    # Power management
    src/power/power_manager.c
    src/power/sleep.c
    src/power/peripheral_switch.c

    # GNSS
    src/gnss/gnss_driver.c
    src/gnss/ubx_parser.c
    src/gnss/ephemeris_cache.c

    # Kalman filter
    src/kalman/kalman_filter.c
    src/kalman/asset_tracker_kf.c
    src/kalman/eta_predictor.c

    # Geofence
    src/geofence/geofence_engine.c
    src/geofence/point_in_polygon.c
    src/geofence/geofence_storage.c

    # Communications
    src/comms/nbiot_driver.c
    src/comms/packet.c
    src/comms/downlink.c
    src/comms/satellite.c

    # Sensors
    src/sensors/accelerometer.c
    src/sensors/temperature.c
)

# HAL sources (add as needed from your STM32CubeL4 installation)
set(HAL_SOURCES "")
if(HAL_DRIVER_PATH)
    file(GLOB HAL_SOURCES
        "${HAL_DRIVER_PATH}/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal.c"
        "${HAL_DRIVER_PATH}/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_cortex.c"
        "${HAL_DRIVER_PATH}/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_rcc.c"
        "${HAL_DRIVER_PATH}/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_rcc_ex.c"
        "${HAL_DRIVER_PATH}/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_gpio.c"
        "${HAL_DRIVER_PATH}/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_uart.c"
        "${HAL_DRIVER_PATH}/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_spi.c"
        "${HAL_DRIVER_PATH}/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_adc.c"
        "${HAL_DRIVER_PATH}/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_adc_ex.c"
        "${HAL_DRIVER_PATH}/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_rtc.c"
        "${HAL_DRIVER_PATH}/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_rtc_ex.c"
        "${HAL_DRIVER_PATH}/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_pwr.c"
        "${HAL_DRIVER_PATH}/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_pwr_ex.c"
        "${HAL_DRIVER_PATH}/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_flash.c"
        "${HAL_DRIVER_PATH}/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_flash_ex.c"
    )
endif()

# ──────────────────────────── Executable ────────────────────────────
add_executable(${PROJECT_NAME}.elf
    ${FIRMWARE_SOURCES}
    ${HAL_SOURCES}
)

# Include directories
target_include_directories(${PROJECT_NAME}.elf PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/config
)

if(HAL_DRIVER_PATH)
    target_include_directories(${PROJECT_NAME}.elf PRIVATE
        ${HAL_DRIVER_PATH}/STM32L4xx_HAL_Driver/Inc
        ${HAL_DRIVER_PATH}/CMSIS/Device/ST/STM32L4xx/Include
        ${HAL_DRIVER_PATH}/CMSIS/Include
    )
endif()

# Definitions
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE
    -D${MCU_DEVICE}
    -DUSE_HAL_DRIVER
)

# Linker flags
if(EXISTS ${MCU_LINKER_SCRIPT})
    target_link_options(${PROJECT_NAME}.elf PRIVATE
        -T${MCU_LINKER_SCRIPT}
        ${CPU_FLAGS}
        -Wl,--gc-sections
        -Wl,--print-memory-usage
        -specs=nano.specs
        -lc -lm -lnosys
    )
endif()

# ──────────────────────────── Post-Build ────────────────────────────
# Generate .hex and .bin files
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}.elf>
    COMMENT "Generating .hex and .bin files, printing size info"
)

# ──────────────────────────── Tests (Host Build) ────────────────────
# Unit tests can be built and run on the host machine
option(BUILD_TESTS "Build unit tests for host" OFF)

if(BUILD_TESTS)
    # Override cross-compilation settings for host tests
    enable_testing()

    add_executable(test_kalman
        tests/test_kalman.c
        src/kalman/kalman_filter.c
        src/kalman/asset_tracker_kf.c
        src/kalman/eta_predictor.c
    )
    target_include_directories(test_kalman PRIVATE src config)
    target_link_libraries(test_kalman m)
    add_test(NAME KalmanFilterTest COMMAND test_kalman)

    add_executable(test_geofence
        tests/test_geofence.c
        src/geofence/point_in_polygon.c
        src/geofence/geofence_engine.c
        src/geofence/geofence_storage.c
    )
    target_include_directories(test_geofence PRIVATE src config)
    target_link_libraries(test_geofence m)
    add_test(NAME GeofenceTest COMMAND test_geofence)

    add_executable(test_ubx_parser
        tests/test_ubx_parser.c
        src/gnss/ubx_parser.c
    )
    target_include_directories(test_ubx_parser PRIVATE src config)
    add_test(NAME UbxParserTest COMMAND test_ubx_parser)

    add_executable(test_packet
        tests/test_packet.c
        src/comms/packet.c
    )
    target_include_directories(test_packet PRIVATE src config)
    add_test(NAME PacketTest COMMAND test_packet)
endif()
