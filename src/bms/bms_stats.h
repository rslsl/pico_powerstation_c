#pragma once
// ============================================================
// bms/bms_stats.h — Lifetime statistics
//
// v4.0 fixes:
//   P1.6  — session_wh_out/in correctly tracked
//   P1.6  — runtime_h updated on save
//   P3.14 — flash via flash_nvm (A/B slots, CRC)
// ============================================================
#include <stdint.h>
#include <stdbool.h>

#define STATS_MAGIC     0x53544154u
#define STATS_VERSION   3u

// Flash payload — A/B + CRC added by flash_nvm
typedef struct __attribute__((packed)) {
    uint16_t version;

    uint32_t boot_count;
    float    efc_total;
    float    energy_in_wh;       // P1.5: correct Wh (V*I*dt)
    float    energy_out_wh;
    float    runtime_h;          // P1.6: now actually updated

    float    temp_min_c;
    float    temp_max_c;
    float    temp_acc_c;
    uint32_t temp_samples;

    float    peak_current_a;
    float    peak_power_w;

    uint32_t total_alarm_events;
    uint32_t total_ocp_events;
    uint32_t total_temp_events;

    float    soh_initial;
    float    soh_last;

    float    session_energy_out_wh; // P1.6: filled on each save
    float    session_energy_in_wh;
    float    session_peak_a;

    // Persistent predictor priors
    float    avg_discharge_wh_per_h;
    float    avg_discharge_duration_h;
    uint16_t discharge_session_count;
    float    peukert_calibrated;

    uint8_t  _pad[145];
} StatsFlash;
_Static_assert(sizeof(StatsFlash) <= 4096 - 16,
               "StatsFlash too large for A/B sector");

typedef struct {
    StatsFlash flash;

    // P1.6: running session accumulators (not yet in flash)
    float  session_wh_out;
    float  session_wh_in;
    float  session_peak_a;
    float  session_peak_w;

    // NVM A/B state
    uint32_t _nvm_seq;
    uint8_t  _nvm_slot;

    bool   initialized;
    bool   dirty;
} BmsStats;

void  stats_init(BmsStats *s);
void  stats_save(BmsStats *s);

// P1.5/P1.6: now takes separate dis_wh_total and chg_wh_total (not mixed)
void  stats_update(BmsStats *s,
                   float dis_wh_total,
                   float chg_wh_total,
                   float efc,
                   float temp_c,
                   float current_a,
                   float power_w,
                   float soh,
                   uint32_t alarm_flags_new);

float stats_efficiency_pct(const BmsStats *s);
float stats_avg_temp_c    (const BmsStats *s);
float stats_session_hours (void);

void  stats_record_discharge_session(BmsStats *s,
                                     float session_wh,
                                     float session_h,
                                     float session_ah,
                                     float dod_frac,
                                     float q_measured_ah,
                                     float v_nominal);
float stats_predictor_baseline_power_w(const BmsStats *s);
float stats_predictor_peukert        (const BmsStats *s);
