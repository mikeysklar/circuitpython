USB_VID = 0x239A
USB_PID = 0x802A
USB_PRODUCT = "Feather nRF52840 Express"
USB_MANUFACTURER = "Adafruit Industries LLC"

MCU_CHIP = nrf52840

QSPI_FLASH_FILESYSTEM = 1
EXTERNAL_FLASH_DEVICES = "GD25Q16C, W25Q16JVxQ"

# Loader-only native: run host-compiled native/viper .mpy without the on-board
# emitter. See docs/loader-only-samd.md in mikeysklar/turbo.
CIRCUITPY_LOAD_NATIVE = 1
