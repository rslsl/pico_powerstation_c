#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SEG_VERSION 1u
#define SEG_MAGIC   0x53454731u
#define SEG_BIN_COUNT 12

typedef enum {
    SEG_DIR_CHARGE = 0,
    SEG_DIR_DISCHARGE = 1,
    SEG_DIR_COUNT
} SegDirection;

typedef struct __attribute__((packed)) {
    float    ah;
    float    wh;
    uint16_t samples;
    uint16_t _pad;
} SegStat;

typedef struct __attribute__((packed)) {
    uint16_t version;
    SegStat  bin[SEG_BIN_COUNT][SEG_DIR_COUNT];
    uint8_t  _pad[3788];
} SegFlash;
_Static_assert(sizeof(SegFlash) <= 4096 - 16, "SegFlash payload too large");

typedef struct {
    SegStat  bin[SEG_BIN_COUNT][SEG_DIR_COUNT];
    uint32_t _nvm_seq;
    uint8_t  _nvm_slot;
    bool     dirty;
} BmsSeg;

void  seg_init(BmsSeg *s);
void  seg_load(BmsSeg *s);
bool  seg_save(BmsSeg *s);

int   seg_bin_from_cell_v(float cell_v);
void  seg_update(BmsSeg *s, float cell_v, SegDirection dir,
                 float current_a, float pack_v, float dt_s);

float seg_remaining_frac(const BmsSeg *s, float cell_v, float fallback_soc_frac);
float seg_average_pack_voltage(const BmsSeg *s, float fallback_pack_v);
float seg_wh_eff_pct(const BmsSeg *s);
float seg_confidence(const BmsSeg *s);
