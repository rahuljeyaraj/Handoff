# The product image: M2 gave it the phone half, M14 the body link.
#
# handoff_ble is the only app link that differs from every other image: it
# carries BTstack, the cyw43 Bluetooth firmware and the flash record store.
# See firmware/CMakeLists.txt for why that is its own library rather than part
# of handoff_lib — the short version is 92 KB on apps/blink.

# The M14 two-board bench drives a sync pulse on GP15. On the product board
# GP15 is the push button, so the pulse is opt-in and the button is disabled
# when it is on: cmake -DHANDOFF_BENCH_SYNC=ON for the bench image.
option(HANDOFF_BENCH_SYNC "GP15 is the M14 sync pulse, not the button" OFF)

add_executable(handoff
    handoff.c
    wear.c
    led.c
    motor.c
)

target_link_libraries(handoff
    pico_stdlib
    hardware_pwm
    handoff_lib
    handoff_ble
)

if (HANDOFF_BENCH_SYNC)
    target_compile_definitions(handoff PRIVATE HANDOFF_BENCH_SYNC=1)
endif()

pico_enable_stdio_usb(handoff 1)
pico_enable_stdio_uart(handoff 0)

handoff_add_app(handoff)
