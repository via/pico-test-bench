set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)

project(pico-logic-analyzer)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

add_executable(
    pico-logic-analyzer
    main.c
    logic.c
    trigger.c
    spsc.c
)
target_compile_options(pico-logic-analyzer PRIVATE
  -Wall -Wextra)
pico_generate_pio_header(
    pico-logic-analyzer 
    ${CMAKE_CURRENT_LIST_DIR}/capture.pio
)

pico_generate_pio_header(
    pico-logic-analyzer
    ${CMAKE_CURRENT_LIST_DIR}/edgedetect.pio
)

pico_generate_pio_header(
    pico-logic-analyzer
    ${CMAKE_CURRENT_LIST_DIR}/triggergen.pio
)

target_link_libraries(
  pico-logic-analyzer 
  pico_stdlib
  hardware_pio
  hardware_dma
)

pico_enable_stdio_usb(pico-logic-analyzer 1)
pico_enable_stdio_uart(pico-logic-analyzer 0)
pico_add_extra_outputs(pico-logic-analyzer)


