#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define MODHEN_CONFIG_PATH "/data/modHEN/config.ini"

typedef struct {
    int  native_orbx;          /* 1 = ModHEN Orbx Native enabled (default) */
    int  legacy_prx;           /* 0 = Disabled by default (PS4 orbx.prx crashes on PS5 XOM) */
    char legacy_prx_name[128]; /* Specific filename in /data/modHEN/plugins/ (optional) */
    int  heap_probe;           /* 1 = RAGE heap allocator diagnostic probe enabled (default: 1) */
    int  heap_adjuster;        /* 0 = Read-only probe mode, 1 = Enable patch (default: 0) */
    int  heap_size_mb;         /* Target heap size in MB (e.g. 1024, default: 1024) */
} ModhenConfig;

void modhen_config_defaults(ModhenConfig *cfg);
int  modhen_config_load(ModhenConfig *cfg);
int  modhen_config_save(const ModhenConfig *cfg);

#ifdef __cplusplus
}
#endif

