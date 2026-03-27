#include "bms_seg.h"

#include "flash_nvm.h"
#include "../config.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define SEG_OFF_A FLASH_SEGSTAT_OFFSET
#define SEG_OFF_B FLASH_SEGSTAT_OFFSET_B
#define SEG_CELL_V_MIN BAT_CELL_MIN_V
#define SEG_CELL_V_MAX BAT_CELL_MAX_V
#define SEG_EFF_DEFAULT_PCT 94.0f
#define SEG_EFF_MIN_PCT     78.0f
#define SEG_BIN_PAIR_CONF_WH 12.0f
#define SEG_DIST_CONF_TOTAL_WH 120.0f

static float _clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static bool _finitef(float v) {
    return !isnan(v) && !isinf(v);
}

static float _bin_conf(const SegStat *chg, const SegStat *dis) {
    float paired_wh = fminf(chg->wh, dis->wh);
    return _clampf(paired_wh / SEG_BIN_PAIR_CONF_WH, 0.0f, 1.0f);
}

static float _total_wh_dir(const BmsSeg *s, SegDirection dir) {
    float total = 0.0f;
    for (int i = 0; i < SEG_BIN_COUNT; ++i) {
        total += s->bin[i][dir].wh;
    }
    return total;
}

static float _total_ah_dir(const BmsSeg *s, SegDirection dir) {
    float total = 0.0f;
    for (int i = 0; i < SEG_BIN_COUNT; ++i) {
        total += s->bin[i][dir].ah;
    }
    return total;
}

void seg_init(BmsSeg *s) {
    memset(s, 0, sizeof(*s));
}

void seg_load(BmsSeg *s) {
    SegFlash f;
    uint32_t seq = 0;
    uint8_t slot = 0;

    if (!s) return;

    if (nvm_ab_load(SEG_OFF_A, SEG_OFF_B,
                    SEG_MAGIC,
                    &f, sizeof(f),
                    &seq, &slot) &&
        f.version == SEG_VERSION) {
        memcpy(s->bin, f.bin, sizeof(s->bin));
        s->_nvm_seq = seq;
        s->_nvm_slot = slot;
        s->dirty = false;
        printf("[SEG] loaded slot=%u seq=%lu eff=%.1f%% conf=%.2f\n",
               (unsigned)slot, (unsigned long)seq,
               seg_wh_eff_pct(s), seg_confidence(s));
        return;
    }

    memset(s->bin, 0, sizeof(s->bin));
    s->_nvm_seq = 0u;
    s->_nvm_slot = 0u;
    s->dirty = false;
    printf("[SEG] no valid learned segments, defaults\n");
}

bool seg_save(BmsSeg *s) {
    SegFlash f;

    if (!s || !s->dirty) return false;

    memset(&f, 0, sizeof(f));
    f.version = SEG_VERSION;
    memcpy(f.bin, s->bin, sizeof(f.bin));

    if (!nvm_ab_save(SEG_OFF_A, SEG_OFF_B,
                     SEG_MAGIC,
                     &s->_nvm_seq, &s->_nvm_slot,
                     &f, sizeof(f))) {
        printf("[SEG] save FAILED\n");
        return false;
    }

    s->dirty = false;
    printf("[SEG] saved slot=%u seq=%lu eff=%.1f%% conf=%.2f\n",
           (unsigned)s->_nvm_slot, (unsigned long)s->_nvm_seq,
           seg_wh_eff_pct(s), seg_confidence(s));
    return true;
}

int seg_bin_from_cell_v(float cell_v) {
    float frac;
    int idx;

    if (!_finitef(cell_v)) return -1;
    if (cell_v <= SEG_CELL_V_MIN) return 0;
    if (cell_v >= SEG_CELL_V_MAX) return SEG_BIN_COUNT - 1;

    frac = (cell_v - SEG_CELL_V_MIN) / (SEG_CELL_V_MAX - SEG_CELL_V_MIN);
    idx = (int)(frac * (float)SEG_BIN_COUNT);
    if (idx < 0) idx = 0;
    if (idx >= SEG_BIN_COUNT) idx = SEG_BIN_COUNT - 1;
    return idx;
}

void seg_update(BmsSeg *s, float cell_v, SegDirection dir,
                float current_a, float pack_v, float dt_s) {
    int bin;
    SegStat *dst;
    float dah;
    float dwh;

    if (!s) return;
    if (!(dt_s > 0.0f) || !_finitef(dt_s)) return;
    if (!(current_a > 0.05f) || !_finitef(current_a)) return;
    if (!(pack_v > 1.0f) || !_finitef(pack_v)) return;

    bin = seg_bin_from_cell_v(cell_v);
    if (bin < 0 || dir >= SEG_DIR_COUNT) return;

    dah = current_a * dt_s / 3600.0f;
    dwh = pack_v * current_a * dt_s / 3600.0f;
    dst = &s->bin[bin][dir];

    dst->ah += dah;
    dst->wh += dwh;
    if (dst->samples < UINT16_MAX) dst->samples++;
    s->dirty = true;
}

float seg_remaining_frac(const BmsSeg *s, float cell_v, float fallback_soc_frac) {
    float total_wh = _total_wh_dir(s, SEG_DIR_DISCHARGE);
    int bin = seg_bin_from_cell_v(cell_v);
    float learned_frac = 0.0f;
    float frac_in_bin;
    float lo;
    float hi;
    float w_bin;
    float conf;

    fallback_soc_frac = _clampf(fallback_soc_frac, 0.0f, 1.0f);
    if (!s || bin < 0 || total_wh < 10.0f) return fallback_soc_frac;

    for (int i = 0; i < bin; ++i) {
        learned_frac += s->bin[i][SEG_DIR_DISCHARGE].wh / total_wh;
    }

    lo = SEG_CELL_V_MIN + ((SEG_CELL_V_MAX - SEG_CELL_V_MIN) * (float)bin / (float)SEG_BIN_COUNT);
    hi = SEG_CELL_V_MIN + ((SEG_CELL_V_MAX - SEG_CELL_V_MIN) * (float)(bin + 1) / (float)SEG_BIN_COUNT);
    frac_in_bin = (hi > lo) ? _clampf((cell_v - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
    w_bin = s->bin[bin][SEG_DIR_DISCHARGE].wh / total_wh;
    learned_frac += w_bin * frac_in_bin;

    conf = _clampf(total_wh / SEG_DIST_CONF_TOTAL_WH, 0.0f, 1.0f);
    return _clampf(fallback_soc_frac * (1.0f - conf) + learned_frac * conf, 0.0f, 1.0f);
}

float seg_average_pack_voltage(const BmsSeg *s, float fallback_pack_v) {
    float total_wh;
    float total_ah;
    float learned_v;
    float conf;

    if (!s) return fallback_pack_v;
    total_wh = _total_wh_dir(s, SEG_DIR_DISCHARGE);
    total_ah = _total_ah_dir(s, SEG_DIR_DISCHARGE);
    if (!(total_wh > 5.0f) || !(total_ah > 0.5f)) return fallback_pack_v;

    learned_v = total_wh / total_ah;
    learned_v = _clampf(learned_v, 9.0f, 12.8f);
    conf = _clampf(total_wh / SEG_DIST_CONF_TOTAL_WH, 0.0f, 1.0f);
    return fallback_pack_v * (1.0f - conf) + learned_v * conf;
}

float seg_wh_eff_pct(const BmsSeg *s) {
    float weighted = 0.0f;
    float weight_sum = 0.0f;
    float global_conf;

    if (!s) return SEG_EFF_DEFAULT_PCT;

    for (int i = 0; i < SEG_BIN_COUNT; ++i) {
        const SegStat *chg = &s->bin[i][SEG_DIR_CHARGE];
        const SegStat *dis = &s->bin[i][SEG_DIR_DISCHARGE];
        float conf = _bin_conf(chg, dis);
        if (conf > 0.01f && chg->wh > 0.5f && dis->wh > 0.5f) {
            float eta = _clampf((dis->wh / chg->wh) * 100.0f, SEG_EFF_MIN_PCT, 100.0f);
            float w = fminf(chg->wh, dis->wh) * conf;
            weighted += eta * w;
            weight_sum += w;
        }
    }

    if (weight_sum <= 0.1f) return SEG_EFF_DEFAULT_PCT;

    global_conf = seg_confidence(s);
    return SEG_EFF_DEFAULT_PCT * (1.0f - global_conf) + (weighted / weight_sum) * global_conf;
}

float seg_confidence(const BmsSeg *s) {
    float acc = 0.0f;

    if (!s) return 0.0f;
    for (int i = 0; i < SEG_BIN_COUNT; ++i) {
        acc += _bin_conf(&s->bin[i][SEG_DIR_CHARGE], &s->bin[i][SEG_DIR_DISCHARGE]);
    }
    return _clampf(acc / (float)SEG_BIN_COUNT, 0.0f, 1.0f);
}
