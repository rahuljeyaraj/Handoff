# M4 — 500 ksps DMA ring and the Goertzel timing budget

add_executable(adcbench
    adcbench.c
)

target_link_libraries(adcbench
    pico_stdlib
    handoff_lib
)

pico_enable_stdio_usb(adcbench 1)
pico_enable_stdio_uart(adcbench 0)

handoff_add_app(adcbench)
