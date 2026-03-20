#pragma once
// ============================================================
// bms/bms_soh.h — State of Health estimator
//
// v4.0 fixes:
//   P0.2  — flash write now via flash_nvm (multicore lockout)
//   P1.5  — Wh correctly as V*I*dt/3600; separate Ah/Wh tracking
//   P1.4  — soh_on_cycle_end() now called from battery.c on
//            discharge→idle/charge transition
//   P2.11 — CRC32 + A/B alternating slots for SOH flash record
// ============================================================
#include <stdint.h>
#include <stdbool.h>

#define SOH_VERSION   3
#define SOH_MAGIC     0xB15E5014u
#define SOH_R0_NOMINAL 0.008f    // 8 mΩ nominal for 3S2P 34Ah pack
#define SOH_R0_MIN     0.001f
#define SOH_R0_MAX     0.100f

// ── Flash payload (pure data, no magic/seq/crc — NVM adds those) ─
// P2.11: CRC + A/B done by flash_nvm; SohFlash is just the payload.
typedef struct __attribute__((packed)) {
    uint16_t version;
    float    efc;               // Equivalent Full Cycles
    uint32_t cycle_count;
    float    q_measured_ah;     // measured capacity
    float    r0_ref_ohm;        // reference R0
    float    soh;               // 0.0–1.0
    float    soh_q;
    float    soh_r;
    float    dis_wh_total;      // P1.5: discharge energy (Wh) — V*I*dt
    float    dis_ah_total;      // discharge charge (Ah) — I*dt
    float    chg_wh_total;      // charge energy (Wh)
    float    chg_ah_total;      // charge charge (Ah)
    uint8_t  _pad[186];
} SohFlash;
_Static_assert(sizeof(SohFlash) <= 4096 - 16,
               "SohFlash payload too large for A/B sector");

typedef struct {
    float    efc;
    uint32_t cycle_count;
    float    q_nominal_ah;
    float    q_measured_ah;
    float    r0_nominal_ohm;
    float    r0_ref_ohm;
    float    soh, soh_q, soh_r;

    // P1.5: correct Wh and Ah tracking (separate dis/chg)
    float    dis_wh_total;   // total discharge energy (Wh) correctly as V*I*dt
    float    dis_ah_total;   // total discharge charge (Ah)
    float    chg_wh_total;
    float    chg_ah_total;

    // Kept for backward compat with stats
    float    wh_total;       // alias: dis_wh_total (updated on every write)

    // Session accumulators (reset on soh_on_cycle_end)
    float    _ah_dis_session;
    float    _wh_dis_session;
    float    _ah_chg_session;
    float    _wh_chg_session;
    float    _soc_dis_start_frac;

    // A/B NVM state
    uint32_t _nvm_seq;
    uint8_t  _nvm_slot;
} BmsSoh;

void  soh_init(BmsSoh *s, float q_nominal_ah, float r0_nominal_ohm);
void  soh_load(BmsSoh *s);
void  soh_save(const BmsSoh *s);

// P1.5: now takes voltage for correct Wh calculation
void  soh_update_discharge(BmsSoh *s, float i_a, float v, float dt_s);
void  soh_update_charge   (BmsSoh *s, float i_a, float v, float dt_s);

// P1.4: call on discharge→idle/charge transition
void  soh_on_cycle_end(BmsSoh *s, float r0_current, float soc_end_frac);

void  soh_calc(BmsSoh *s);
int   soh_rul_cycles(const BmsSoh *s);
