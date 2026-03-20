// ============================================================
// bms/bms_predictor.c - Runtime remaining-time predictor
// ============================================================
#include "bms_predictor.h"
#include <math.h>
#include <string.h>

#define MIN_POWER_W      1.0f
#define MAX_HOURS        200.0f
#define EMA_ALPHA        0.05f
#define PRED_SAMPLE_DT_S 0.1f

static inline float _clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float _ema_alpha_for_samples(int samples) {
    if (samples < 10) return 0.50f;
    if (samples < 30) return 0.20f;
    return EMA_ALPHA;
}

static float _temp_capacity_factor(float temp_c) {
    if (!isfinite(temp_c)) return 1.0f;
    if (temp_c >= 25.0f) return 1.0f;
    if (temp_c >= 10.0f) return 0.95f + (temp_c - 10.0f) * (0.05f / 15.0f);
    if (temp_c >= 0.0f)  return 0.80f + temp_c * (0.15f / 10.0f);
    return 0.80f;
}

static int _tm(float energy_wh, float power_w) {
    if (power_w < MIN_POWER_W || energy_wh <= 0.0f) {
        return (int)(MAX_HOURS * 60.0f);
    }
    float h = energy_wh / power_w;
    if (h > MAX_HOURS) return (int)(MAX_HOURS * 60.0f);
    return (int)(h * 60.0f);
}

static int _regression(Predictor *p, float e_now) {
    int n = p->_reg_n;
    if (n < PRED_REG_SIZE / 3) return 0;

    int start = (p->_reg_ptr - n + PRED_REG_SIZE) % PRED_REG_SIZE;
    float tm = 0.0f;
    float em = 0.0f;
    for (int k = 0; k < n; k++) {
        int i = (start + k) % PRED_REG_SIZE;
        tm += p->_reg_t[i];
        em += p->_reg_e[i];
    }
    tm /= (float)n;
    em /= (float)n;

    float stt = 0.0f;
    float ste = 0.0f;
    for (int k = 0; k < n; k++) {
        int i = (start + k) % PRED_REG_SIZE;
        float dt = p->_reg_t[i] - tm;
        stt += dt * dt;
        ste += dt * (p->_reg_e[i] - em);
    }

    if (fabsf(stt) < 1e-9f) return 0;

    float slope = ste / stt;
    if (!isfinite(slope) || slope >= -1e-6f) return (int)(MAX_HOURS * 60.0f);

    float t_zero = tm - em / slope;
    float remain_s = t_zero - p->_t_elapsed;
    if (!isfinite(remain_s) || remain_s < 0.0f) return 0;

    return (int)_clamp(remain_s / 60.0f, 0.0f, MAX_HOURS * 60.0f);
}

void pred_init(Predictor *p, float q_nominal_ah, float v_nominal) {
    memset(p, 0, sizeof(*p));
    p->q_nominal_ah = q_nominal_ah;
    p->v_nominal = v_nominal;
    p->_peukert_n = PRED_PEUKERT_DEFAULT;
}

void pred_seed(Predictor *p, float baseline_power_w, float peukert_n) {
    if (baseline_power_w > MIN_POWER_W && isfinite(baseline_power_w)) {
        p->_baseline_power_w = baseline_power_w;
    }
    if (peukert_n >= 1.0f && peukert_n <= 1.15f && isfinite(peukert_n)) {
        p->_peukert_n = peukert_n;
    }
}

int pred_update(Predictor *p,
                float soc,
                float i_pack,
                float power_w,
                float energy_remain_wh,
                float temp_c,
                float soh,
                float r0_ohm) {
    (void)soc;
    (void)soh;

    float current_a = fabsf(i_pack);
    float effective_power_w = fabsf(power_w);
    if (!isfinite(effective_power_w)) effective_power_w = 0.0f;
    if (isfinite(r0_ohm) && r0_ohm > 0.0f && current_a > 0.05f) {
        effective_power_w += current_a * current_a * r0_ohm;
    }

    float effective_energy_wh = energy_remain_wh * _temp_capacity_factor(temp_c);
    if (!isfinite(effective_energy_wh) || effective_energy_wh <= 0.0f) {
        p->minutes_best = 0;
        p->confidence = 0.0f;
        return 0;
    }

    if (effective_power_w > MIN_POWER_W) {
        if (p->_ema_n == 0) {
            p->_p_ema = effective_power_w;
        } else {
            float alpha = _ema_alpha_for_samples(p->_ema_n);
            p->_p_ema = (1.0f - alpha) * p->_p_ema + alpha * effective_power_w;
        }
        p->_ema_n++;
        if (p->_warmup_samples < UINT16_MAX) p->_warmup_samples++;

        p->_p_win[p->_win_ptr] = effective_power_w;
        p->_win_ptr = (p->_win_ptr + 1) % PRED_WIN_SIZE;
        if (p->_win_n < PRED_WIN_SIZE) p->_win_n++;
    }

    float p_win = 0.0f;
    if (p->_win_n > 0) {
        for (int i = 0; i < p->_win_n; i++) p_win += p->_p_win[i];
        p_win /= (float)p->_win_n;
    }

    p->_reg_t[p->_reg_ptr] = p->_t_elapsed;
    p->_reg_e[p->_reg_ptr] = effective_energy_wh;
    p->_reg_ptr = (p->_reg_ptr + 1) % PRED_REG_SIZE;
    if (p->_reg_n < PRED_REG_SIZE) p->_reg_n++;
    p->_t_elapsed += PRED_SAMPLE_DT_S;

    float pk = 1.0f;
    if (current_a > 0.1f) {
        float exp_v = _clamp(p->_peukert_n - 1.0f, 0.0f, 0.2f);
        pk = powf(PRED_I_NOMINAL_A / fmaxf(current_a, 0.05f), exp_v);
        pk = _clamp(pk, 0.70f, 1.05f);
    }

    float ema_power = (p->_p_ema > MIN_POWER_W) ? p->_p_ema : p->_baseline_power_w;
    p->minutes_ema = (ema_power > MIN_POWER_W) ? _tm(effective_energy_wh, ema_power * pk) : 0;
    p->minutes_wind = (p_win > MIN_POWER_W) ? _tm(effective_energy_wh, p_win * pk) : 0;
    p->minutes_reg = _regression(p, effective_energy_wh);

    if (p->_warmup_samples < PRED_WARMUP_SAMPLES) {
        p->minutes_best = 0;
        p->confidence = 0.0f;
        return 0;
    }

    float sum_w = 0.0f;
    float sum_t = 0.0f;
    int methods = 0;
    if (p->minutes_ema > 0)  { sum_t += p->minutes_ema  * 0.35f; sum_w += 0.35f; methods++; }
    if (p->minutes_wind > 0) { sum_t += p->minutes_wind * 0.35f; sum_w += 0.35f; methods++; }
    if (p->minutes_reg > 0)  { sum_t += p->minutes_reg  * 0.30f; sum_w += 0.30f; methods++; }

    if (sum_w <= 0.0f || methods == 0) {
        p->minutes_best = 0;
        p->confidence = 0.0f;
        return 0;
    }

    p->minutes_best = (int)(sum_t / sum_w);

    float t_avg = (float)p->minutes_best;
    float var = 0.0f;
    if (p->minutes_ema > 0)  var += (p->minutes_ema - t_avg) * (p->minutes_ema - t_avg);
    if (p->minutes_wind > 0) var += (p->minutes_wind - t_avg) * (p->minutes_wind - t_avg);
    if (p->minutes_reg > 0)  var += (p->minutes_reg - t_avg) * (p->minutes_reg - t_avg);
    var /= (float)methods;

    float std = sqrtf(fmaxf(var, 0.0f));
    float base_conf = _clamp(1.0f - std / (t_avg + 1.0f), 0.1f, 0.99f);
    float sample_factor = _clamp((float)(p->_ema_n + p->_win_n + p->_reg_n) / 45.0f, 0.15f, 1.0f);
    p->confidence = _clamp(base_conf * sample_factor, 0.12f, 0.99f);

    return p->minutes_best;
}
