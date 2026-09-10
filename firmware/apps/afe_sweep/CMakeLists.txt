# M7 — analogue front-end characterisation

add_executable(afe_sweep
    afe_sweep.c
)

target_link_libraries(afe_sweep
    pico_stdlib
    handoff_lib
)

pico_enable_stdio_usb(afe_sweep 1)
pico_enable_stdio_uart(afe_sweep 0)

handoff_add_app(afe_sweep)
