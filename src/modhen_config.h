#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define MODHEN_CONFIG_PATH "/data/modHEN/config.ini"

typedef struct {
    int  native_orbx;          /* 1 = ModHEN Orbx Native enabled (default) */
    int  legacy_prx;           /* 0 = Disabled by default (PS4 orbx.prx crashes on PS5 XOM) */
    char legacy_prx_name[128]; /* Specific filename in /data/modHEN/plugins/ (optional) */
    int  heap_probe;           /* 1 = RAGE heap allocator diagnostic probe enabled (default: 1) */
    int  heap_adjuster;        /* 0 = Read-only probe mode, 1 = Enable patch (default: 0) */
    int  heap_size_mb;         /* Target heap size in MB (e.g. 1024, default: 1024) */

    /* [MENU] UX, Theme & POV/HUD defaults */
    int  menu_position;        /* 0 = Left, 1 = Right, 2 = Center */
    int  menu_scale;           /* 0 = Compact, 1 = Normal, 2 = Large */
    int  theme_accent;         /* 0..5 accent theme */
    int  sound_enabled;        /* 1 = On, 0 = Off */
    int  keep_steering_open;   /* 1 = Keep steer/throttle active when menu open */
    int  nav_stick_enabled;    /* 1 = Analog stick mirrors D-Pad inside menu (default 0 = D-Pad Only) */
    int  speedo_mode;          /* 0 = Off, 1 = MPH, 2 = KPH */
    int  fp_fov_idx;           /* 0..8 first-person FOV preset */
    int  fp_lock;              /* 0/1 lock first-person view */
    int  fp_clip_fix;          /* 0/1 near-clip 0.01m weapon fix */
    int  cfg_version;          /* Schema version (2 = D-Pad-only nav default migration) */

    /* [KEYBINDS] Hardware ScePad button bitmasks */
    uint32_t bind_open_mod;    /* Default: R1 (0x0800), 0 for single button */
    uint32_t bind_open_btn;    /* Default: D-Pad Down (0x0040) */
    uint32_t bind_accept;      /* Default: Cross (0x4000) */
    uint32_t bind_cancel;      /* Default: Circle (0x2000) */
    uint32_t bind_tab_prev;    /* Default: L1 (0x0400) */
    uint32_t bind_tab_next;    /* Default: R1 (0x0800) */
    uint32_t bind_horn_boost;  /* Default: L3 (0x0002) */
    uint32_t bind_noclip;      /* Default: 0 (Unbound) */
    uint32_t bind_quick_repair;/* Default: 0 (Unbound) */
    uint32_t bind_quick_heal;  /* Default: 0 (Unbound) */
    uint32_t bind_super_brake; /* Default: 0 (Unbound) */
} ModhenConfig;

void modhen_config_defaults(ModhenConfig *cfg);
int  modhen_config_load(ModhenConfig *cfg);
int  modhen_config_save(const ModhenConfig *cfg);

#ifdef __cplusplus
}
#endif

