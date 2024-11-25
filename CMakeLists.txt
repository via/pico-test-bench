set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
cmake_minimum_required(VERSION 3.13)
include(pico-sdk/pico_sdk_init.cmake)

project(pico-logic-analyzer)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/nanopb/extra)
find_package(Nanopb REQUIRED)
nanopb_generate_cpp(TARGET test-bench-interfaces test-bench-interfaces.proto)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

add_executable(
    pico-logic-analyzer
    main.c
    logic.c
    trigger.c
    spsc.c
    nanocobs/cobs.c
)

target_include_directories(pico-logic-analyzer PRIVATE nanocobs)

target_compile_options(pico-logic-analyzer PRIVATE
  -Wall -Wextra)
pico_generate_pio_header(
    pico-logic-analyzer 
    ${CMAKE_CURRENT_LIST_DIR}/capture.pio
)

pico_generate_pio_header(
    pico-logic-analyzer
    ${CMAKE_CURRENT_LIST_DIR}/triggergen.pio
)

pico_generate_pio_header(
    pico-logic-analyzer
    ${CMAKE_CURRENT_LIST_DIR}/spi_slave.pio
)

target_link_libraries(
  pico-logic-analyzer 
  test-bench-interfaces
  pico_stdlib
  pico_multicore
  hardware_pio
  hardware_dma
  hardware_spi
)

pico_enable_stdio_usb(pico-logic-analyzer 1)
pico_enable_stdio_uart(pico-logic-analyzer 0)
pico_add_extra_outputs(pico-logic-analyzer)


