# Step 0 of bring-up: prove the board, the toolchain and the USB console.

add_executable(blink
    blink.c
)

target_link_libraries(blink
    pico_stdlib
    pico_cyw43_arch_none   # LED lives on the CYW43439, so we need the driver
    handoff_lib            # not used yet; keeps config.h on the include path
)

# Console over USB CDC. The UART pins stay free — GP0/GP1 are wanted later,
# and docs 10.5 streams raw ADC buffers over this same USB link.
pico_enable_stdio_usb(blink 1)
pico_enable_stdio_uart(blink 0)

handoff_add_app(blink)   # .uf2, .hex, .bin, .map, .dis, all into build/
