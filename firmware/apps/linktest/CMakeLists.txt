# M6 — one-way link, role fixed at boot

add_executable(linktest
    linktest.c
)

target_link_libraries(linktest
    pico_stdlib
    handoff_lib
    pico_cyw43_arch_none
)

pico_enable_stdio_usb(linktest 1)
pico_enable_stdio_uart(linktest 0)

handoff_add_app(linktest)

# The same app with the role compiled in as TRANSMITTER, for a board whose
# GP14 header is not strapped (M13's far end). Flash whichever fits the bench.
add_executable(linktest_tx
    linktest.c
)
target_compile_definitions(linktest_tx PRIVATE LINKTEST_ROLE=1)
target_link_libraries(linktest_tx
    pico_stdlib
    handoff_lib
    pico_cyw43_arch_none
)
pico_enable_stdio_usb(linktest_tx 1)
pico_enable_stdio_uart(linktest_tx 0)
handoff_add_app(linktest_tx)
