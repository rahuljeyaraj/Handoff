# Handoff firmware.
#
# handoff_lib is the whole of lib/ as one static library. It is split into two
# halves that CMake keeps honest:
#
#   the portable half   dsp, link, record, proto — no SDK headers, ever
#   the binding half    hal_pico — SDK everything
#
# The portable half is ALSO compiled by scripts/test.py with the host compiler,
# and that is what enforces architecture §3.2. If someone reaches for
# hardware/adc.h inside lib/dsp, the host build fails immediately and CI says so
# before the change is merged. The rule is not a convention here; it is a build.

add_library(handoff_lib STATIC
    lib/dsp/goertzel.c
    lib/dsp/sync.c
    lib/dsp/carrier.c

    lib/link/crc.c
    lib/link/manchester.c
    lib/link/frame.c
    lib/link/chunk.c

    lib/record/compact.c
    lib/record/vcard.c
    lib/record/frag.c
    lib/record/store.c

    lib/proto/carousel.c
    lib/proto/beacon.c
    lib/proto/link_sm.c

    lib/hal_pico/hal_pico.c
    lib/hal_pico/pio_carrier.c
    lib/hal_pico/adc_ring.c
    lib/hal_pico/ipc.c
    lib/hal_pico/tlm_usb.c
)

# lib/hal_pico/pio_carrier.pio -> pio_carrier.pio.h, by the SDK's pioasm. The
# state machine programs M3 needs are assembled at build time; none of the
# instruction encodings are written by hand.
pico_generate_pio_header(handoff_lib
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/hal_pico/pio_carrier.pio)

target_include_directories(handoff_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/hal
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/dsp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/link
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/record
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/proto
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/hal_pico
)

# Only the hal_pico half needs these, but CMake links per target, and the
# portable half is stopped from using them by the host build rather than by
# anything here.
target_link_libraries(handoff_lib
    pico_stdlib
    hardware_adc
    hardware_dma
    hardware_pio
    hardware_flash
    pico_multicore
)

# The phone half, M2. Three files out of lib/hal_pico that share one
# dependency: BTstack.
#
# They are a SEPARATE library, and not because the source tree has a seam here
# — architecture 4 puts all three under hal_pico with everything else. It is
# because CYW43_ENABLE_BLUETOOTH has to be PUBLIC for the cyw43 driver
# compiled into each executable to agree with it, and PUBLIC on handoff_lib
# means every app carries the combined WiFi+Bluetooth firmware blob. Measured:
# it put 92 KB onto apps/blink, which is M0's image and has no radio in it at
# all. Worse, it would put a Bluetooth stack inside apps/adcbench, whose whole
# job at M4 is to measure how much of core 1 is left.
#
# flash.c is here rather than in handoff_lib for a second, better reason: its
# sector offset is defined against PICO_FLASH_BANK_STORAGE_OFFSET, so the
# record store and the bond store are checked against each other at compile
# time. See the static_asserts in flash.c.
add_library(handoff_ble STATIC
    lib/hal_pico/ble.c
    lib/hal_pico/flash.c
    lib/hal_pico/tlm_ble.c
    lib/hal_pico/power.c    # reads GP29 through the CYW43 driver, hence here
)

target_link_libraries(handoff_ble
    handoff_lib
    pico_stdlib
    pico_flash
    pico_unique_id

    # pico_btstack_flash_bank is what makes the phone bond survive a power
    # cycle, which is an M2 exit criterion and not an optional extra.
    pico_btstack_ble
    pico_btstack_cyw43
    pico_btstack_flash_bank
    pico_cyw43_arch_none
)

# CYW43439 is one die doing both radios; the driver needs telling that the
# Bluetooth half is wanted, and every translation unit that sees the driver
# has to be told the same thing.
target_compile_definitions(handoff_ble PUBLIC
    CYW43_ENABLE_BLUETOOTH=1
)

# lib/hal_pico/ble_service.gatt -> ble_service.h, by BTstack's compile_gatt.py.
# The attribute handles ble.c uses come from there; none are written by hand.
pico_btstack_make_gatt_header(handoff_ble PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/hal_pico/ble_service.gatt)

# One flashable image per milestone. See each app's own header comment for what
# it proves and — as the development plan insists — what it cannot.
add_subdirectory(apps/blink)      # M0  done
add_subdirectory(apps/txgen)      # M3
add_subdirectory(apps/adcbench)   # M4
add_subdirectory(apps/loopback)   # M5
add_subdirectory(apps/linktest)   # M6
add_subdirectory(apps/afe_sweep)  # M7
add_subdirectory(apps/handoff)    # M14
