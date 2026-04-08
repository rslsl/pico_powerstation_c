#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../bms/battery.h"
#include "../bms/bms_logger.h"
#include "../bms/bms_stats.h"

typedef struct {
    bool prev_charge_active;
    bool prev_discharge_active;
    float charge_start_wh;
    float discharge_start_wh;
    float discharge_start_ah;
    float discharge_start_soc;
    uint32_t discharge_active_ms;
} SessionManager;

void session_manager_init(SessionManager *mgr);
void session_manager_step(SessionManager *mgr,
                          Battery *bat,
                          const BatSnapshot *snap,
                          BmsLogger *logger,
                          BmsStats *stats,
                          uint32_t ms_now);
