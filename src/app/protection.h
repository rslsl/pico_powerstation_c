#pragma once
// ============================================================
// app/protection.h — BMS Protection Logic
//
// v4.0 fixes:
//   P2.9  — hysteresis (separate set/clear thresholds)
//           debounce counters (N consecutive samples before activate)
//           latched faults: critical faults require explicit prot_reset_latch()
// ============================================================
#include <stdint.h>
#include <stdbool.h>
#include "power_control.h"
#include "../bms/battery.h"

// ── Alarm bit flags ──────────────────────────────────────────
#define ALARM_SOC_WARN      (1u <<  0)
#define ALARM_SOC_CUT       (1u <<  1)
#define ALARM_VBAT_WARN     (1u <<  2)
#define ALARM_VBAT_CUT      (1u <<  3)
#define ALARM_CELL_WARN     (1u <<  4)
#define ALARM_CELL_CUT      (1u <<  5)
#define ALARM_DELTA_WARN    (1u <<  6)
#define ALARM_DELTA_CUT     (1u <<  7)
#define ALARM_OCP_WARN      (1u <<  8)
#define ALARM_OCP_CUT       (1u <<  9)
#define ALARM_TEMP_WARN     (1u << 10)
#define ALARM_TEMP_CUT      (1u << 11)
#define ALARM_INV_WARN      (1u << 12)
#define ALARM_INV_CUT       (1u << 13)
#define ALARM_I2C_FAULT     (1u << 14)  // latched I2C fault
#define ALARM_CELL_OVP      (1u << 15)
#define ALARM_COLD_CHARGE   (1u << 16)
#define ALARM_TEMP_BUZZ     (1u << 17)
#define ALARM_TEMP_SAFE     (1u << 18)
#define ALARM_INV_SAFE      (1u << 19)

#define ALARM_ANY_CRIT  (ALARM_SOC_CUT|ALARM_VBAT_CUT|ALARM_CELL_CUT| \
                         ALARM_DELTA_CUT|ALARM_OCP_CUT|ALARM_TEMP_CUT| \
                         ALARM_TEMP_SAFE| \
                         ALARM_INV_SAFE| \
                         ALARM_INV_CUT|ALARM_I2C_FAULT)

// ── Debounce: how many consecutive samples above threshold before alarm ──
#define PROT_DEBOUNCE_WARN  2    // 2 × LOGIC_MS = 200 ms
#define PROT_DEBOUNCE_CUT   3    // 3 × LOGIC_MS = 300 ms
#define PROT_CLEAR_DEBOUNCE 5    // must be clear for 5 samples to deactivate WARN

// ── Hysteresis clear thresholds (alarm clears above these values) ────────
// Prevents flapping at the boundary.
#define SOC_WARN_CLR_PCT    (SOC_WARN_PCT   + 3.0f)
#define SOC_CUT_CLR_PCT     (SOC_CUTOFF_PCT + 2.0f)
#define VBAT_WARN_CLR_V     (VBAT_WARN_V   + 0.3f)
#define VBAT_CUT_CLR_V      (VBAT_CUT_V    + 0.2f)
#define CELL_WARN_CLR_V     (CELL_WARN_V   + 0.05f)
#define CELL_CUT_CLR_V      (CELL_CUT_V    + 0.03f)
#define DELTA_WARN_CLR_MV   (DELTA_WARN_MV - 15.0f)
#define DELTA_CUT_CLR_MV    (DELTA_CUT_MV  - 20.0f)
#define IDIS_WARN_CLR_A     (IDIS_WARN_A   - 3.0f)
#define IDIS_CUT_CLR_A      (IDIS_CUT_A    - 5.0f)
#define TEMP_BAT_WARN_CLR_C (TEMP_BAT_WARN_C - 3.0f)
#define TEMP_BAT_BUZZ_CLR_C (TEMP_BAT_BUZZ_C - 3.0f)
#define TEMP_BAT_SAFE_CLR_C (TEMP_BAT_SAFE_C - 5.0f)
#define TEMP_BAT_CUT_CLR_C  (TEMP_BAT_CUT_C  - 5.0f)
#define TEMP_BAT_CHARGE_CLR_C (TEMP_BAT_CHARGE_MIN_C + 3.0f)
#define TEMP_INV_WARN_CLR_C (TEMP_INV_WARN_C - 3.0f)
#define TEMP_INV_SAFE_CLR_C (TEMP_INV_SAFE_C - 5.0f)
#define TEMP_INV_CUT_CLR_C  (TEMP_INV_CUT_C  - 5.0f)
#define CELL_OVP_CLR_V      (BAT_CELL_OVP_V - 0.05f)

// ── Number of alarm bits tracked for debounce ─────────────────
#define PROT_NUM_ALARMS  20

typedef struct {
    PowerControl *pwr;

    uint32_t  alarms;         // current active alarm flags
    uint32_t  alarms_prev;    // previous tick (for edge detection)
    uint32_t  latched;        // P2.9: latched critical faults
                              // Must call prot_reset_latch() to clear
    uint32_t  alarm_count;

    // P2.9: debounce counters (index = alarm bit position 0..19)
    uint8_t   set_count[PROT_NUM_ALARMS];    // consecutive samples above threshold
    uint8_t   clear_count[PROT_NUM_ALARMS];  // consecutive samples below clear threshold
} Protection;

void     prot_init(Protection *prot, PowerControl *pwr);
void     prot_check(Protection *prot, const Battery *bat);
bool     prot_has_critical(const Protection *prot);
bool     prot_has_warning(const Protection *prot);
uint32_t prot_alarms(const Protection *prot);

// P2.9: manually clear latched faults (e.g. after operator acknowledges)
// Also requires the triggering condition to be gone.
void     prot_reset_latch(Protection *prot, uint32_t alarm_bits);
uint32_t prot_resettable_latch_bits(const Protection *prot, uint32_t alarm_bits);

void     prot_set_i2c_fault(Protection *prot, bool active);
