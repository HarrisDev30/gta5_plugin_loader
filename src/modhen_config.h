#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define MODHEN_CONFIG_PATH "/data/modHEN/config.ini"

typedef struct {
    int  native_orbx;          /* 1 = ModHEN Orbx Native enabled (default) */
    int  legacy_prx;           /* 0 = Disabled by default (PS4 orbx.prx crashes on PS5 XOM) */
    char legacy_prx_name[128]; /* Specific filename in /data/modHEN/plugins/ (optional) */
} ModhenConfig;

void modhen_config_defaults(ModhenConfig *cfg);
int  modhen_config_load(ModhenConfig *cfg);
int  modhen_config_save(const ModhenConfig *cfg);

#ifdef __cplusplus
}
#endif

