# M13 -- half-duplex turnaround on one board

add_executable(turnaround
    turnaround.c
)

target_link_libraries(turnaround
    pico_stdlib
    handoff_lib
)

pico_enable_stdio_usb(turnaround 1)
pico_enable_stdio_uart(turnaround 0)

handoff_add_app(turnaround)
