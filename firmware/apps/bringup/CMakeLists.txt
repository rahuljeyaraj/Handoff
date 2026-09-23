# PCB bring-up: one pin, one state, read with a meter. docs/hardware-bringup.md.
# The radio is started for one reason only: GP29 is the CYW43 SPI clock as
# well as VSYS/3, and the divider reads nothing while that chip is off.

add_executable(bringup
    bringup.c
)

target_link_libraries(bringup
    pico_stdlib
    hardware_adc
    handoff_lib
    pico_cyw43_arch_none
)

pico_enable_stdio_usb(bringup 1)
pico_enable_stdio_uart(bringup 0)

handoff_add_app(bringup)
