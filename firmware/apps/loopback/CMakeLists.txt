# M5 — the whole link inside one chip

add_executable(loopback
    loopback.c
)

target_link_libraries(loopback
    pico_stdlib
    handoff_lib
)

pico_enable_stdio_usb(loopback 1)
pico_enable_stdio_uart(loopback 0)

handoff_add_app(loopback)
