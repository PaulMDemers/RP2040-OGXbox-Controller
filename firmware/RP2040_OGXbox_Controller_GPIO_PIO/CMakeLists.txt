cmake_minimum_required(VERSION 3.13)

include(${CMAKE_CURRENT_LIST_DIR}/../../third_party/Pico-PIO-USB/examples/pico_sdk_import.cmake)

project(rp2040_ogxbox_controller_gpio_pio C CXX ASM)

pico_sdk_init()

set(PICO_PIO_USB_DIR "${CMAKE_CURRENT_LIST_DIR}/../../third_party/Pico-PIO-USB")
add_subdirectory(${PICO_PIO_USB_DIR} pico_pio_usb)

add_executable(rp2040_ogxbox_controller_gpio_pio
  main.c
  newlib_locks.c
)

target_compile_definitions(rp2040_ogxbox_controller_gpio_pio PRIVATE
  OGXB_PIO_USB_DP_PIN=2
  OGXB_ENABLE_IDLE_SCRIPT=0
)

target_compile_options(rp2040_ogxbox_controller_gpio_pio PRIVATE
  -Wall
  -Wextra
)

target_link_libraries(rp2040_ogxbox_controller_gpio_pio PRIVATE
  pico_stdlib
  pico_multicore
  pico_sync
  pico_pio_usb
)

pico_enable_stdio_usb(rp2040_ogxbox_controller_gpio_pio 1)
pico_enable_stdio_uart(rp2040_ogxbox_controller_gpio_pio 0)
pico_add_extra_outputs(rp2040_ogxbox_controller_gpio_pio)
