# M2 today (phone link), M14 eventually (the real thing).
#
# handoff_ble is the only app link that differs from every other image: it
# carries BTstack, the cyw43 Bluetooth firmware and the flash record store.
# See firmware/CMakeLists.txt for why that is its own library rather than part
# of handoff_lib — the short version is 92 KB on apps/blink.

add_executable(handoff
    handoff.c
    motor.c
)

target_link_libraries(handoff
    pico_stdlib
    handoff_lib
    handoff_ble
)

pico_enable_stdio_usb(handoff 1)
pico_enable_stdio_uart(handoff 0)

handoff_add_app(handoff)
