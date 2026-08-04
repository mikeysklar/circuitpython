// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2017 Dan Halbert for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include "py/runtime.h"

#include "common-hal/microcontroller/Processor.h"
#include "shared-bindings/microcontroller/Processor.h"

#include "shared-bindings/microcontroller/ResetReason.h"

#include <sys/types.h>
#include <zephyr/drivers/hwinfo.h>

#if defined(CONFIG_SOC_FAMILY_SILABS_SIWX91X)
#include <zephyr/net/net_if.h>
#endif


float common_hal_mcu_processor_get_temperature(void) {
    return 0.0;
}

extern uint32_t SystemCoreClock;
uint32_t common_hal_mcu_processor_get_frequency(void) {
    #ifdef __ARM__
    return SystemCoreClock;
    #else
    return CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC;
    #endif
}

float common_hal_mcu_processor_get_voltage(void) {
    return 3.3f;
}

void common_hal_mcu_processor_get_uid(uint8_t raw_id[]) {
    ssize_t len = hwinfo_get_device_id(raw_id, COMMON_HAL_MCU_PROCESSOR_UID_LENGTH);
    if (len < 0) {
        len = 0;
    }
    #if defined(CONFIG_SOC_FAMILY_SILABS_SIWX91X)
    // SiWx91x has no hwinfo driver, and none is worth writing: the efuse
    // identity region (0x020..0x02F of the array at TA_EFUSE_IO_BASE_ADDR) is
    // unprogrammed on this silicon -- verified on two dies, 2026-08-04. The
    // MACs live only in the flash-resident config space ("efusecopy",
    // 0x04000560), and Simplicity Commander's "Unique ID" is defined as the
    // WiFi MAC zero-extended to 8 bytes. Match that: the wifi net_if link
    // address is set from the same store at driver init, before the radio is
    // enabled, so it is available whenever this can be called.
    if (len == 0) {
        struct net_if *iface = net_if_get_first_wifi();
        struct net_linkaddr *addr = (iface != NULL) ? net_if_get_link_addr(iface) : NULL;
        if (addr != NULL && addr->len == 6 && COMMON_HAL_MCU_PROCESSOR_UID_LENGTH >= 8) {
            raw_id[0] = 0;
            raw_id[1] = 0;
            memcpy(&raw_id[2], addr->addr, 6);
            len = 8;
        }
    }
    #endif
    if (len < COMMON_HAL_MCU_PROCESSOR_UID_LENGTH) {
        memset(raw_id + len, 0, COMMON_HAL_MCU_PROCESSOR_UID_LENGTH - len);
    }
}

mcu_reset_reason_t common_hal_mcu_processor_get_reset_reason(void) {
    mcu_reset_reason_t r = MCU_RESET_REASON_UNKNOWN;
    return r;
}
