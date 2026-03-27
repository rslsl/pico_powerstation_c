# Battery Efficiency And Learning Plan

## Why the current number is misleading

The current lifetime screen used:

`energy_out_wh / energy_in_wh`

This is not a true efficiency metric for a battery pack because:

- lifetime `energy_in` and `energy_out` are not paired closed cycles
- the pack can start from non-zero SOC
- part of the history can include only charge or only discharge windows
- charger, converter, battery, and load losses are mixed together

Because of that, values above `100%` can appear even when sensors work correctly.

## What should be measured separately

Use separate metrics instead of one mixed number:

1. `Coulombic efficiency`
   `eta_q = Ah_out / Ah_in`

2. `Battery energy efficiency`
   `eta_wh_bat = Wh_out_pack / Wh_in_pack`

3. `System delivery efficiency`
   `eta_sys = Wh_load / Wh_out_pack`

4. `Charge path efficiency`
   `eta_chg = Wh_in_pack / Wh_from_charger`

With the current hardware, the firmware already measures pack charge/discharge energy well enough for `eta_q` and `eta_wh_bat`.
For true end-to-end powerstation efficiency, add output-side metering per major rail.

## Recommended SOC / efficiency model

Do not rely on voltage-only SOC. Use a hybrid model:

- `EKF + coulomb counting` as the fast estimator
- `OCV correction` only after idle settling
- `segment learning` to refine usable capacity and efficiency

### Segment model

For each cell, split `3.30V .. 4.20V` into fixed bins.

Recommended start:

- 12 bins, width `75mV`
- store data per direction:
  - charge
  - discharge
- keep both Ah and Wh statistics per bin

Suggested learned record per bin:

```c
typedef struct {
    float q_in_ah;
    float q_out_ah;
    float e_in_wh;
    float e_out_wh;
    float avg_temp_c;
    float avg_current_a;
    uint16_t samples;
    float confidence;
} SegStat;
```

### What is updated in every bin

On every logic tick:

- determine the active cell-voltage bin using average cell voltage
- integrate:
  - `dAh = I * dt / 3600`
  - `dWh = Vpack * I * dt / 3600`
- accumulate separately for charge and discharge

### How to compute learned segment efficiency

For a bin with enough confidence:

- `eta_q_bin = q_out_ah / q_in_ah`
- `eta_wh_bin = e_out_wh / e_in_wh`

Clamp learning to sane limits, for example:

- `eta_q_bin`: `0.97 .. 1.00`
- `eta_wh_bin`: `0.80 .. 0.99`

## When learning is trustworthy

Only update learned tables when all conditions are true:

- pack and cell measurements are fresh
- current is above noise floor
- no critical alarms
- no brownout
- temperature is in a normal band
- no charge/discharge direction flapping

Increase confidence faster when:

- the segment was crossed slowly
- current was stable
- temperature was close to nominal

Reduce confidence when:

- current ripple is high
- the pack is hot or cold
- a segment is seen rarely

## Cycle-level learning

At the end of each meaningful discharge window:

- detect `DoD`
- if `DoD >= 15%`, update measured capacity
- if the cycle also contains matching charge data, update round-trip energy efficiency

Recommended stored cycle summary:

```c
typedef struct {
    float soc_start;
    float soc_end;
    float ah_in;
    float ah_out;
    float wh_in;
    float wh_out;
    float temp_avg_c;
    float current_avg_a;
    float r0_mohm;
} CycleSummary;
```

Use EWMA updates, not hard replacement:

- `new = old * (1 - alpha) + sample * alpha`
- use small `alpha` like `0.03 .. 0.10`
- make `alpha` depend on confidence

## Capacity learning

The pack should learn three capacity layers:

1. `Nominal capacity`
   Fixed from settings.

2. `Measured usable capacity`
   Learned from real cycles.

3. `Segment usable capacity map`
   Learned usable charge in each voltage region.

This helps a lot near the flat part of the Li-ion curve where voltage alone is weak.

## Temperature and current compensation

Every learned value should be normalized against:

- temperature bucket
  - `<0C`
  - `0..10C`
  - `10..25C`
  - `25..40C`
  - `>40C`
- current bucket
  - light
  - nominal
  - heavy

If memory is tight, do not store a full 2D table first.
Store one base value and a small correction factor per temp/current bucket.

## Suggested firmware roadmap

### Phase 1

- keep EKF as the main SOC estimator
- add segment tables for charge/discharge Ah/Wh
- expose:
  - `battery energy efficiency`
  - `coulombic efficiency`
  - `confidence`

### Phase 2

- pair charge and discharge windows into closed cycle summaries
- learn bin-level efficiency and usable capacity
- use learned bin weights in remaining-energy estimation

### Phase 3

- add output-side metering
- compute real converter/output efficiency per rail
- show separate values for:
  - battery
  - DC outputs
  - full station

## Relay state improvements

The firmware should persist not the temporary relay GPIO state, but the user-desired state.

That is the correct model because:

- safety logic can force outputs off
- thermal logic can override the fan
- boot policy can temporarily isolate loads

So the restore flow should be:

1. load saved desired outputs
2. apply boot policy
3. apply safety restrictions
4. restore allowed user outputs

## UI recommendations

Show these values separately:

- `BAT Q EFF`
- `BAT WH EFF`
- `ENERGY BALANCE`
- `LEARNING CONF`
- `SOH Q`
- `SOH R`

And for debug/service pages:

- learned segment map
- confidence by segment
- last trusted cycle summary
- temp/current bucket currently active

## Practical next step in this codebase

Best first implementation after the current UI/relay work:

1. add a small persistent `SegStat[12][2]` table
2. update it in `bat_update_bms()`
3. save it with the existing A/B flash backend
4. use it to refine `remaining_wh`
5. render confidence and learned efficiency on the history screens
