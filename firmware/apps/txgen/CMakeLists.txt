# M3 — carrier generation and PIO self-measurement

add_executable(txgen
    txgen.c
)

target_link_libraries(txgen
    pico_stdlib
    handoff_lib
)

pico_enable_stdio_usb(txgen 1)
pico_enable_stdio_uart(txgen 0)

handoff_add_app(txgen)
