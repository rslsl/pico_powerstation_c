#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "../config.h"

#define SOH_VERSION    4
#define SOH_MAGIC      0xB15E5014u
#define SOH_R0_NOMINAL BAT_R0_NOMINAL_OHM
#define SOH_R0_MIN     0.001f
#define SOH_R0_MAX     0.100f

typedef struct __attribute__((packed)) {
    uint16_t version;
    float    efc;
    uint32_t cycle_count;
    float    q_measured_ah;
    float    r0_ref_ohm;
    float    soh;
    float    soh_q;
    float    soh_r;
    float    dis_wh_total;
    float    dis_ah_total;
    float    chg_wh_total;
    float    chg_ah_total;
    uint16_t r0_update_count;
    uint8_t  _pad[184];
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
    float    soh_confidence;
    float    soh_q_confidence;
    float    soh_r_confidence;

    float    dis_wh_total;
    float    dis_ah_total;
    float    chg_wh_total;
    float    chg_ah_total;

    float    wh_total;

    float    _ah_dis_session;
    float    _wh_dis_session;
    float    _ah_chg_session;
    float    _wh_chg_session;
    float    _soc_dis_start_frac;

    uint32_t _nvm_seq;
    uint8_t  _nvm_slot;
    uint16_t r0_update_count;
    bool     migration_pending;
} BmsSoh;

void  soh_init(BmsSoh *s, float q_nominal_ah, float r0_nominal_ohm);
void  soh_load(BmsSoh *s);
bool  soh_save(const BmsSoh *s);

void  soh_update_discharge(BmsSoh *s, float i_a, float v, float dt_s);
void  soh_update_charge   (BmsSoh *s, float i_a, float v, float dt_s);
void  soh_on_cycle_end(BmsSoh *s, float r0_current, float soc_end_frac);

void  soh_calc(BmsSoh *s);
int   soh_rul_cycles(const BmsSoh *s);
