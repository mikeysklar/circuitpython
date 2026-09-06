USB_VID = 0x239A
USB_PID = 0x813E
USB_PRODUCT = "Metro RP2040"
USB_MANUFACTURER = "Adafruit"

CHIP_VARIANT = RP2040
CHIP_FAMILY = rp2

EXTERNAL_FLASH_DEVICES = "GD25Q64C,W25Q64JVxQ,W25Q128JV"

CIRCUITPY_SDIOIO = 1

# Loader-only native: run host-compiled native/viper .mpy without the on-board
# emitter. See docs/loader-only-samd.md in mikeysklar/turbo.
CIRCUITPY_LOAD_NATIVE = 1
