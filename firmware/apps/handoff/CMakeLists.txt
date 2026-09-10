# M14 — the real thing

add_executable(handoff
    handoff.c
)

target_link_libraries(handoff
    pico_stdlib
    handoff_lib
)

pico_enable_stdio_usb(handoff 1)
pico_enable_stdio_uart(handoff 0)

handoff_add_app(handoff)
