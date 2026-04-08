#include "session_manager.h"

#include "runtime_policy.h"
#include "system_settings.h"

#include <stdio.h>
#include <string.h>

void session_manager_init(SessionManager *mgr) {
    if (!mgr) return;
    memset(mgr, 0, sizeof(*mgr));
}

void session_manager_step(SessionManager *mgr,
                          Battery *bat,
                          const BatSnapshot *snap,
                          BmsLogger *logger,
                          BmsStats *stats,
                          uint32_t ms_now) {
    bool discharge_enter;
    bool discharge_hold;
    bool discharge_active;

    if (!mgr || !bat || !snap || !logger || !stats) return;

    if (!mgr->prev_charge_active && snap->is_charging) {
        mgr->charge_start_wh = bat->soh_est.chg_wh_total;
        log_charge_start(logger, snap->display_soc_pct, snap->voltage, snap->i_chg);
    } else if (mgr->prev_charge_active && !snap->is_charging) {
        float session_wh = bat->soh_est.chg_wh_total - mgr->charge_start_wh;
        if (session_wh < 0.0f) session_wh = 0.0f;
        log_charge_end(logger, snap->display_soc_pct, session_wh);
        stats_inc_charge_session(stats, session_wh);
    }
    mgr->prev_charge_active = snap->is_charging;

    discharge_enter = runtime_policy_discharge_enter(snap->is_charging, snap->i_net, snap->power_w);
    discharge_hold = runtime_policy_discharge_hold(snap->is_charging, snap->i_net, snap->power_w);
    discharge_active = mgr->prev_discharge_active ? discharge_hold : discharge_enter;

    if (!mgr->prev_discharge_active && discharge_active) {
        mgr->discharge_start_wh = bat->soh_est.dis_wh_total;
        mgr->discharge_start_ah = bat->soh_est.dis_ah_total;
        mgr->discharge_start_soc = snap->display_soc_pct / 100.0f;
        mgr->discharge_active_ms = ms_now;
        bat_cycle_begin(bat);
        log_discharge_start(logger, snap->display_soc_pct, snap->voltage, snap->i_net);
        printf("[CYCLE] enter i=%.2fA P=%.1fW\n", snap->i_net, snap->power_w);
    } else if (mgr->prev_discharge_active && !discharge_active) {
        float session_wh = bat->soh_est.dis_wh_total - mgr->discharge_start_wh;
        float session_ah = bat->soh_est.dis_ah_total - mgr->discharge_start_ah;
        uint32_t discharge_dur_ms = (mgr->discharge_active_ms > 0u && ms_now > mgr->discharge_active_ms)
                                  ? (ms_now - mgr->discharge_active_ms)
                                  : 0u;
        float session_h = (float)discharge_dur_ms / 3600000.0f;
        float dod_frac = mgr->discharge_start_soc - (snap->display_soc_pct / 100.0f);

        if (session_wh < 0.0f) session_wh = 0.0f;
        if (session_ah < 0.0f) session_ah = 0.0f;
        if (dod_frac < 0.0f) dod_frac = 0.0f;

        if (discharge_dur_ms >= RUNTIME_DISCHARGE_MIN_MS) {
            printf("[CYCLE] end dur=%lus DoD=%.0f%%\n",
                   (unsigned long)(discharge_dur_ms / 1000u),
                   dod_frac * 100.0f);
            bat_cycle_end(bat);
            log_discharge_end(logger, snap->display_soc_pct, session_wh, session_h);
            stats_record_discharge_session(stats,
                                           session_wh,
                                           session_h,
                                           session_ah,
                                           dod_frac,
                                           bat->soh_est.q_measured_ah,
                                           settings_get()->pack_nominal_v);
            bat_seed_predictor(bat,
                               stats_predictor_baseline_power_w(stats),
                               stats_predictor_peukert(stats));
        }

        mgr->discharge_active_ms = 0u;
    }

    mgr->prev_discharge_active = discharge_active;
}
