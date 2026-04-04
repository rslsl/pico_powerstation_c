#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../bms/battery.h"
#include "../bms/bms_logger.h"
#include "../bms/bms_stats.h"

typedef struct {
    float last_saved_soc;
    float last_saved_soh;
    uint32_t boot_ms;
    uint32_t soc_sample_ms;
    uint32_t soc_stable_since_ms;
    float soc_sample;
} SaveManager;

void save_manager_init(SaveManager *mgr, const Battery *bat, uint32_t boot_ms);
void save_manager_update_guard(SaveManager *mgr, const Battery *bat, uint32_t ms_now);
bool save_manager_should_save(const SaveManager *mgr, const Battery *bat, uint32_t ms_now);
void save_manager_commit(SaveManager *mgr,
                         Battery *bat,
                         BmsLogger *logger,
                         BmsStats *stats,
                         uint32_t ms_now);
