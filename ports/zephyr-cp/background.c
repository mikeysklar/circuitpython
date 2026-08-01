// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2017 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include "background.h"

#include "py/runtime.h"
#include "supervisor/port.h"

#include <zephyr/kernel.h>

#if CIRCUITPY_BLEIO
#include "common-hal/_bleio/Adapter.h"
#endif

void port_start_background_tick(void) {
}

void port_finish_background_tick(void) {
}

void port_background_tick(void) {
    // No, ticks. We use Zephyr threads instead.
}

void port_background_task(void) {
    #if CIRCUITPY_BLEIO
    // Resume advertising after a disconnect.  Deferred to here because the
    // Zephyr disconnect callback runs in the BT thread, where bt_le_adv_start()
    // must not be called.
    bleio_background();
    #endif

    // Make sure time advances in the simulator.
    #if defined(CONFIG_ARCH_POSIX)
    k_busy_wait(100);
    #endif
}
