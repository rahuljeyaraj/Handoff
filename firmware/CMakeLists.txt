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

    lib/record/compact.c
    lib/record/vcard.c
    lib/record/frag.c
    lib/record/store.c

    lib/proto/carousel.c
    lib/proto/elect.c
    lib/proto/link_sm.c

    lib/hal_pico/hal_pico.c
    lib/hal_pico/pio_carrier.c
    lib/hal_pico/adc_ring.c
    lib/hal_pico/ipc.c
    lib/hal_pico/ble.c
    lib/hal_pico/flash.c
    lib/hal_pico/tlm_usb.c
    lib/hal_pico/tlm_ble.c
)

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

# One flashable image per milestone. See each app's own header comment for what
# it proves and — as the development plan insists — what it cannot.
add_subdirectory(apps/blink)      # M0  done
add_subdirectory(apps/txgen)      # M3
add_subdirectory(apps/adcbench)   # M4
add_subdirectory(apps/loopback)   # M5
add_subdirectory(apps/linktest)   # M6
add_subdirectory(apps/afe_sweep)  # M7
add_subdirectory(apps/handoff)    # M14
