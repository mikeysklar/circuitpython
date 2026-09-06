// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2015 Glenn Ruben Bakke
//
// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "shared/runtime/interrupt_char.h"
#include "py/mpconfig.h"
#include "supervisor/shared/tick.h"

#include <zephyr/kernel.h>

// CIRCUITPY-CHANGE: the native emitter (py/emitglue.c) writes machine code into
// data RAM, then on cores with an instruction cache flushes the D-cache and
// invalidates the I-cache so the CPU fetches the new code. Cortex-M33 parts like
// the nRF54L need this. Use Zephyr's cache API: always declared, order-independent,
// and a no-op when the cache is disabled.
#if defined(CONFIG_CPU_CORTEX_M)
#include <zephyr/cache.h>
#define MP_HAL_CLEAN_DCACHE(addr, size) sys_cache_data_flush_range((void *)(addr), (size_t)(size))
#define MP_HAL_INVALIDATE_ICACHE() sys_cache_instr_invd_all()
#endif

#define mp_hal_ticks_ms()       ((mp_uint_t)supervisor_ticks_ms32())

static inline void mp_hal_delay_us(mp_uint_t us) {
    k_busy_wait((uint32_t)us);
}

bool mp_hal_stdin_any(void);
