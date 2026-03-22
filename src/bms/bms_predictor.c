// ============================================================
// bms/bms_predictor.c - Runtime remaining-time predictor
// ============================================================
#include "bms_predictor.h"
#include <math.h>
#include <string.h>

#define MIN_POWER_W               1.0f
#define MAX_HOURS                 200.0f
#define PRED_FAST_TAU_S           1.5f
#define PRED_SLOW_UP_TAU_S        6.0f
#define PRED_SLOW_DOWN_TAU_S      24.0f

static inline float _clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static int _median3i(int a, int b, int c) {
    if (a > b) {
        int t = a; a = b; b = t;
    }
    if (b > c) {
        int t = b; b = c; c = t;
    }
    if (a > b) {
        int t = a; a = b; b = t;
    }
    return b;
}

static float _alpha_from_tau(float dt_s, float tau_s) {
    if (!(dt_s > 0.0f) || !(tau_s > 0.0f)) return 1.0f;
    return _clamp(dt_s / (tau_s + dt_s), 0.02f, 1.0f);
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
        return 0;
    }
    float h = energy_wh / power_w;
    if (h > MAX_HOURS) return (int)(MAX_HOURS * 60.0f);
    return (int)(h * 60.0f);
}

static void _window_stats(const Predictor *p, float *mean_out, float *std_out, float *max_out) {
    float mean = 0.0f;
    float max_v = 0.0f;
    if (p->_win_n <= 0) {
        *mean_out = 0.0f;
        *std_out = 0.0f;
        *max_out = 0.0f;
        return;
    }

    for (int i = 0; i < p->_win_n; ++i) {
        float v = p->_p_win[i];
        mean += v;
        if (v > max_v) max_v = v;
    }
    mean /= (float)p->_win_n;

    float var = 0.0f;
    for (int i = 0; i < p->_win_n; ++i) {
        float d = p->_p_win[i] - mean;
        var += d * d;
    }
    var /= (float)p->_win_n;

    *mean_out = mean;
    *std_out = sqrtf(fmaxf(var, 0.0f));
    *max_out = max_v;
}

static int _regression(Predictor *p) {
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
    if (!isfinite(slope) || slope >= -1e-4f) return 0;

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

void pred_reset_runtime(Predictor *p) {
    float q_nominal_ah = p->q_nominal_ah;
    float v_nominal = p->v_nominal;
    float peukert_n = p->_peukert_n;
    float baseline_power_w = p->_baseline_power_w;

    memset(p, 0, sizeof(*p));
    p->q_nominal_ah = q_nominal_ah;
    p->v_nominal = v_nominal;
    p->_peukert_n = peukert_n;
    p->_baseline_power_w = baseline_power_w;
}

int pred_update(Predictor *p,
                float soc,
                float i_pack,
                float power_w,
                float energy_remain_wh,
                float temp_c,
                float soh,
                float r0_ohm,
                float dt_s) {
    (void)soc;
    (void)soh;
    if (!(dt_s > 0.0f) || !isfinite(dt_s)) dt_s = 0.1f;

    float current_a = fabsf(i_pack);
    float effective_power_w = fabsf(power_w);
    if (!isfinite(effective_power_w)) effective_power_w = 0.0f;
    if (isfinite(r0_ohm) && r0_ohm > 0.0f && current_a > 0.05f) {
        effective_power_w += current_a * current_a * r0_ohm;
    }

    float load_capacity_factor = 1.0f;
    if (current_a > 0.1f) {
        float exp_v = _clamp(p->_peukert_n - 1.0f, 0.0f, 0.2f);
        load_capacity_factor = powf(PRED_I_NOMINAL_A / fmaxf(current_a, 0.05f), exp_v);
        load_capacity_factor = _clamp(load_capacity_factor, 0.65f, 1.0f);
    }

    float effective_energy_wh = energy_remain_wh *
                                _temp_capacity_factor(temp_c) *
                                load_capacity_factor;
    p->usable_wh = effective_energy_wh;
    if (!isfinite(effective_energy_wh) || effective_energy_wh <= 0.0f) {
        p->_minutes_smoothed = 0.0f;
        p->minutes_raw = 0;
        p->minutes_display = 0;
        p->minutes_best = 0;
        p->confidence = 0.0f;
        return 0;
    }

    if (effective_power_w > MIN_POWER_W) {
        if (p->_warmup_s <= 0.0f) {
            p->_p_ema = effective_power_w;
            p->_p_fast = effective_power_w;
            p->_p_slow = effective_power_w;
        } else {
            p->_p_ema += PRED_EMA_ALPHA * (effective_power_w - p->_p_ema);
            float alpha_fast = _alpha_from_tau(dt_s, PRED_FAST_TAU_S);
            float alpha_slow = _alpha_from_tau(
                dt_s,
                (effective_power_w > p->_p_slow) ? PRED_SLOW_UP_TAU_S : PRED_SLOW_DOWN_TAU_S);
            p->_p_fast += alpha_fast * (effective_power_w - p->_p_fast);
            p->_p_slow += alpha_slow * (effective_power_w - p->_p_slow);
        }
        p->_warmup_s += dt_s;

        p->_p_win[p->_win_ptr] = effective_power_w;
        p->_win_ptr = (p->_win_ptr + 1) % PRED_WIN_SIZE;
        if (p->_win_n < PRED_WIN_SIZE) p->_win_n++;

        p->_reg_t[p->_reg_ptr] = p->_t_elapsed;
        p->_reg_e[p->_reg_ptr] = effective_energy_wh;
        p->_reg_ptr = (p->_reg_ptr + 1) % PRED_REG_SIZE;
        if (p->_reg_n < PRED_REG_SIZE) p->_reg_n++;
        p->_t_elapsed += dt_s;
    }

    float p_mean = 0.0f;
    float p_std = 0.0f;
    float p_max = 0.0f;
    _window_stats(p, &p_mean, &p_std, &p_max);

    float p_window = _clamp(p_mean + 0.65f * p_std, p_mean, p_max);
    float p_floor = (p->_baseline_power_w > MIN_POWER_W)
        ? fmaxf(p->_baseline_power_w * 0.90f, MIN_POWER_W)
        : MIN_POWER_W;
    float p_cons = fmaxf(fmaxf(p->_p_fast, p->_p_slow), p_window);
    p_cons = fmaxf(p_cons, p_floor);

    p->power_fast_w = p->_p_fast;
    p->power_slow_w = p->_p_slow;
    p->power_window_w = p_window;
    p->power_cons_w = p_cons;

    p->minutes_ema = _tm(effective_energy_wh, fmaxf(p->_p_ema, p_floor));
    p->minutes_fast = _tm(effective_energy_wh, fmaxf(p->_p_fast, p_floor));
    p->minutes_slow = _tm(effective_energy_wh, fmaxf(p->_p_slow, p_floor));
    p->minutes_wind = _tm(effective_energy_wh, fmaxf(p_window, p_floor));
    p->minutes_reg = _regression(p);

    int minutes_model = _tm(effective_energy_wh, p_cons);
    int minutes_reg = (p->minutes_reg > 0) ? p->minutes_reg : minutes_model;
    int minutes_wind = (p->minutes_wind > 0) ? p->minutes_wind : minutes_model;
    int minutes_ema = (p->minutes_ema > 0) ? p->minutes_ema : minutes_model;
    p->minutes_raw = _median3i(minutes_ema, minutes_wind, minutes_reg);
    if (minutes_model > 0) {
        p->minutes_raw = (p->minutes_raw > 0)
            ? (int)fminf((float)p->minutes_raw, (float)minutes_model)
            : minutes_model;
    }

    if (p->_minutes_smoothed <= 0.0f) {
        p->_minutes_smoothed = (float)p->minutes_raw;
    } else {
        p->_minutes_smoothed += PRED_OUTPUT_EMA_ALPHA *
                                ((float)p->minutes_raw - p->_minutes_smoothed);
    }

    if (p->minutes_display <= 0) {
        p->minutes_display = (int)(p->_minutes_smoothed + 0.5f);
    } else {
        float display = (float)p->minutes_display;
        float step_scale = _clamp(dt_s, 0.10f, 1.0f);
        float up_step = 2.0f * step_scale;
        float down_step = 5.0f * step_scale;
        float target = p->_minutes_smoothed;
        if (target > display + up_step) {
            display += up_step;
        } else if (target < display - down_step) {
            display -= down_step;
        } else {
            display = target;
        }
        p->minutes_display = (int)(_clamp(display, 0.0f, MAX_HOURS * 60.0f) + 0.5f);
    }

    float instability = p_cons > MIN_POWER_W ? _clamp(p_std / p_cons, 0.0f, 1.0f) : 1.0f;
    float warmup_factor = _clamp(p->_warmup_s / 18.0f, 0.0f, 1.0f);
    float reg_factor = (p->minutes_reg > 0) ? 1.0f : 0.85f;
    float base_conf = _clamp(1.0f - instability, 0.12f, 0.98f);
    p->confidence = _clamp(base_conf * warmup_factor * reg_factor, 0.0f, 0.99f);

    p->minutes_best = p->minutes_display;
    return p->minutes_display;
}
