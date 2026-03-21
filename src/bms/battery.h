#pragma once
// ============================================================
// bms/battery.h — Central BMS State + Coordinator
//
// v4.0 fixes:
//   P0.1  — Battery holds PowerControl* for real bat_emergency_off()
//   P1.4  — cycle detection: _was_discharging flag
//   P1.5  — BatSnapshot now includes dis_wh_total/chg_wh_total
// ============================================================
#include <stdint.h>
#include <stdbool.h>
#include "bms_ekf.h"
#include "bms_soh.h"
#include "../drivers/ina226.h"
#include "../drivers/ina3221.h"
#include "../drivers/lm75a.h"

// Forward declaration (battery.h included before power_control.h sometimes)
typedef struct PowerControl PowerControl;

typedef enum { TEMP_OK=0, TEMP_WARN=1, TEMP_CRIT=2 } TempStatus;

typedef struct {
    // Sensor references
    INA226   *ina_dis;
    INA226   *ina_chg;
    INA3221  *ina3221;
    LM75A    *lm75a_bat;
    LM75A    *lm75a_inv;

    // P0.1: reference to PowerControl for real emergency off
    PowerControl *pwr;

    // Measured (every SENSOR_MS)
    float    voltage, v_chg_bus, i_dis, i_chg, i_net, power_w;
    float    temp_bat, temp_inv;
    float    v_b1, v_b2, v_b3, delta_mv;

    // Calculated (every LOGIC_MS)
    float    soc, soc_std, remaining_wh, r0_mohm;
    float    soh, efc;
    int      time_min;
    float    pred_confidence;

    // States
    bool       is_charging, is_idle;
    TempStatus temp_bat_status, temp_inv_status;

    // I2C fault counter
    uint8_t  i2c_fail_count;

    // FMEA-02: per-group measurement validity mask + age
    #define MEAS_VALID_PACK   (1u << 0)  // INA226 dis — voltage + i_dis
    #define MEAS_VALID_CHG    (1u << 1)  // INA226 chg — i_chg
    #define MEAS_VALID_CELLS  (1u << 2)  // INA3221 — v_b1/v_b2/v_b3/delta_mv
    #define MEAS_VALID_TBAT   (1u << 3)  // LM75A battery temp
    #define MEAS_VALID_TINV   (1u << 4)  // LM75A inverter temp
    uint8_t  meas_valid;                  // bitmask; 0 = stale/failed
    uint32_t meas_ts_ms[5];               // timestamp of last valid read (ms)
    #define MEAS_STALE_MS    600          // data older than this is stale

    // FMEA-15: inverter thermal sensor mandatory
    bool     inv_temp_sensor_ok;          // set by bat_init if lm75a_inv init OK

    // OCV / HPPC internal
    uint32_t _t_idle_ms;
    float    _v_prev, _i_prev;

    // P1.4: cycle boundary detection
    bool     _was_discharging;   // true if i_net > discharge threshold last tick

    // BMS algorithms
    BmsEkf   ekf;
    BmsSoh   soh_est;

    uint32_t _t_last_logic_ms;
} Battery;

// P0.1: pwr parameter added to bat_init
void bat_init(Battery *bat,
              INA226 *ina_dis, INA226 *ina_chg,
              INA3221 *ina3221,
              LM75A *lm75a_bat, LM75A *lm75a_inv,
              PowerControl *pwr);   // ← P0.1

void bat_read_sensors(Battery *bat);
void bat_update_bms  (Battery *bat, float dt_s);
void bat_save        (Battery *bat);
void bat_emergency_off(Battery *bat);   // P0.1: now real implementation
void bat_apply_settings(Battery *bat, float capacity_ah);

// FMEA-02: true only if measurement group bit is valid and not stale
bool bat_meas_fresh(const Battery *bat, uint8_t group_bit);

// ── Thread-safe snapshot ──────────────────────────────────────
// Copied by Core0 under spinlock. Core1 reads ONLY from this.
typedef struct {
    float    voltage, v_chg_bus, i_dis, i_chg, i_net, power_w;
    float    temp_bat, temp_inv;
    float    v_b1, v_b2, v_b3, delta_mv;
    float    soc, soc_std, remaining_wh, r0_mohm, soh, efc;
    bool     is_charging, is_idle;
    int      time_min;
    float    pred_confidence;
    uint8_t  temp_bat_status, temp_inv_status;
    // P1.5: expose correct energy totals
    float    dis_wh_total;   // total discharge energy (Wh)
    float    chg_wh_total;   // total charge energy (Wh)
    float    dis_ah_total;
    float    chg_ah_total;
    // FMEA-02: sensor validity (bitmask, same as Battery.meas_valid)
    uint8_t  meas_valid;
    // FMEA-15
    bool     inv_temp_sensor_ok;
} BatSnapshot;

void bat_snapshot(const Battery *bat, BatSnapshot *out);
