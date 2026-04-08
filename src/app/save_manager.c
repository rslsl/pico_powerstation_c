#include "save_manager.h"

#include "../config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static inline bool _finitef_save(float v) {
    return !isnan(v) && !isinf(v);
}

static bool _save_runtime_data_valid(const Battery *bat) {
    if (!bat) return false;
    if (bat->voltage < VBAT_BROWNOUT_V) return false;
    if (!bat_meas_fresh(bat, MEAS_VALID_PACK)) return false;
    if (!bat_meas_fresh(bat, MEAS_VALID_CELLS)) return false;
    if (!_finitef_save(bat->voltage) || bat->voltage < VBAT_BROWNOUT_V || bat->voltage > 15.0f) return false;
    if (!_finitef_save(bat->soc) || bat->soc < 0.0f || bat->soc > 100.0f) return false;
    if (!_finitef_save(bat->soh) || bat->soh < 0.0f || bat->soh > 100.0f) return false;
    if (!_finitef_save(bat->soc_std) || bat->soc_std < 0.0f || bat->soc_std > SAVE_SOC_STD_MAX_PCT) return false;
    if (!_finitef_save(bat->r0_mohm) ||
        bat->r0_mohm < (EKF_R0_MIN * 1000.0f) ||
        bat->r0_mohm > (EKF_R0_MAX * 1000.0f)) return false;
    return true;
}

#define SAVE_SKIP_LOG_INTERVAL_MS  600000u  /* 10 minutes */

static uint16_t _save_reason_to_code(const char *reason) {
    if (!reason) return LOG_REASON_NONE;
    if (strcmp(reason, "startup holdoff") == 0) return LOG_REASON_STARTUP_HOLDOFF;
    if (strcmp(reason, "soc settling") == 0)    return LOG_REASON_SOC_SETTLING;
    if (strcmp(reason, "pack stale") == 0)      return LOG_REASON_PACK_STALE;
    if (strcmp(reason, "cells stale") == 0)     return LOG_REASON_CELLS_STALE;
    if (strcmp(reason, "brownout") == 0)        return LOG_REASON_BROWNOUT;
    if (strcmp(reason, "soc invalid") == 0)     return LOG_REASON_INVALID_SOC;
    if (strcmp(reason, "soh invalid") == 0)     return LOG_REASON_INVALID_SOH;
    if (strcmp(reason, "r0 invalid") == 0)      return LOG_REASON_INVALID_R0;
    if (strcmp(reason, "soc unstable") == 0)    return LOG_REASON_SOC_UNSTABLE;
    return LOG_REASON_NONE;
}

static const char *_save_reject_reason(const SaveManager *mgr,
                                       const Battery *bat,
                                       uint32_t ms_now) {
    if (!mgr || !bat) return "manager";
    if (bat->voltage < VBAT_BROWNOUT_V) return "brownout";
    if (!bat_meas_fresh(bat, MEAS_VALID_PACK)) return "pack stale";
    if (!bat_meas_fresh(bat, MEAS_VALID_CELLS)) return "cells stale";
    if (!_finitef_save(bat->soc) || bat->soc < 0.0f || bat->soc > 100.0f) return "soc invalid";
    if (!_finitef_save(bat->soh) || bat->soh < 0.0f || bat->soh > 100.0f) return "soh invalid";
    if (!_finitef_save(bat->soc_std) || bat->soc_std < 0.0f || bat->soc_std > SAVE_SOC_STD_MAX_PCT) return "soc unstable";
    if (!_finitef_save(bat->r0_mohm) ||
        bat->r0_mohm < (EKF_R0_MIN * 1000.0f) ||
        bat->r0_mohm > (EKF_R0_MAX * 1000.0f)) return "r0 invalid";
    if ((ms_now - mgr->boot_ms) < SAVE_STARTUP_HOLDOFF_MS) return "startup holdoff";
    if (mgr->soc_stable_since_ms == 0u) return "soc settling";
    if ((ms_now - mgr->soc_stable_since_ms) < SAVE_SOC_SETTLE_MS) return "soc settling";
    return NULL;
}

void save_manager_init(SaveManager *mgr, const Battery *bat, uint32_t boot_ms) {
    if (!mgr) return;

    memset(mgr, 0, sizeof(*mgr));
    mgr->boot_ms = boot_ms;
    if (bat) {
        mgr->last_saved_soc = bat->soc;
        mgr->last_saved_soh = bat->soh;
        mgr->soc_sample = _finitef_save(bat->soc) ? bat->soc : 0.0f;
    } else {
        mgr->last_saved_soc = -999.0f;
        mgr->last_saved_soh = -999.0f;
    }
    mgr->soc_sample_ms = boot_ms;
    mgr->soc_stable_since_ms = 0u;
}

void save_manager_update_guard(SaveManager *mgr, const Battery *bat, uint32_t ms_now) {
    if (!mgr || !bat) return;

    if (!_save_runtime_data_valid(bat)) {
        mgr->soc_sample_ms = ms_now;
        mgr->soc_sample = _finitef_save(bat->soc) ? bat->soc : 0.0f;
        mgr->soc_stable_since_ms = 0u;
        return;
    }

    if (mgr->soc_sample_ms == 0u) {
        mgr->soc_sample_ms = ms_now;
        mgr->soc_sample = bat->soc;
        mgr->soc_stable_since_ms = ms_now;
        return;
    }

    {
        uint32_t dt_ms = ms_now - mgr->soc_sample_ms;
        float dt_s;
        float dsoc;
        float max_step;

        if (dt_ms < SAVE_SOC_SETTLE_SAMPLE_MS) return;

        dt_s = (float)dt_ms * 0.001f;
        dsoc = fabsf(bat->soc - mgr->soc_sample);
        max_step = SAVE_SOC_SETTLE_RATE_PCT_S * dt_s;

        if (dsoc <= max_step) {
            if (mgr->soc_stable_since_ms == 0u) {
                mgr->soc_stable_since_ms = ms_now;
            }
        } else {
            mgr->soc_stable_since_ms = 0u;
        }

        mgr->soc_sample_ms = ms_now;
        mgr->soc_sample = bat->soc;
    }
}

bool save_manager_should_save(const SaveManager *mgr, const Battery *bat, uint32_t ms_now) {
    if (!mgr || !bat) return false;
    if (_save_reject_reason(mgr, bat, ms_now) != NULL) return false;

    return fabsf(bat->soc - mgr->last_saved_soc) >= SAVE_SOC_DELTA_PCT ||
           fabsf(bat->soh - mgr->last_saved_soh) / 100.0f >= SAVE_SOH_DELTA;
}

void save_manager_commit(SaveManager *mgr,
                         Battery *bat,
                         BmsLogger *logger,
                         BmsStats *stats,
                         uint32_t ms_now) {
    const char *reject;
    bool bat_saved;
    bool stats_saved;

    if (!mgr || !bat || !logger || !stats) return;

    reject = _save_reject_reason(mgr, bat, ms_now);
    if (reject) {
        uint16_t code = _save_reason_to_code(reject);
        bool reason_changed = (code != mgr->last_skip_reason);
        bool interval_passed = (ms_now - mgr->last_skip_log_ms) >= SAVE_SKIP_LOG_INTERVAL_MS;
        if (reason_changed || interval_passed) {
            log_save_skip(logger, code, bat->soc, bat->voltage);
            stats_inc_save_skip(stats);
            mgr->last_skip_reason = code;
            mgr->last_skip_log_ms = ms_now;
        }
        printf("[SAVE] skipped: %s SOC=%.1f%% std=%.2f V=%.2fV valid=0x%02X uptime=%lus\n",
               reject, bat->soc, bat->soc_std, bat->voltage,
               bat->meas_valid, (unsigned long)(ms_now / 1000u));
        return;
    }

    bat_saved = bat_save(bat);
    log_flush_header(logger);
    stats_saved = stats_save(stats);

    if (bat_saved) {
        mgr->last_saved_soc = bat->soc;
        mgr->last_saved_soh = bat->soh;
    }

    if (bat_saved || stats_saved) {
        log_save_ok(logger, bat->soc, bat->voltage);
        printf("[SAVE] SOC=%.1f%% SOH=%.1f%% bat=%s stats=%s\n",
               bat->soc, bat->soh,
               bat_saved ? "ok" : "skip",
               stats_saved ? "ok" : "skip");
    } else {
        printf("[SAVE] skipped: no valid payloads\n");
    }
}
