# M6 — one-way link, role fixed at boot

add_executable(linktest
    linktest.c
)

target_link_libraries(linktest
    pico_stdlib
    handoff_lib
)

pico_enable_stdio_usb(linktest 1)
pico_enable_stdio_uart(linktest 0)

handoff_add_app(linktest)
