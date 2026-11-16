#define _GNU_SOURCE
#include "modhen_config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static void cfg_trim(char *s) {
    if (!s) return;
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
}

void modhen_config_defaults(ModhenConfig *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->native_orbx        = 1;
    cfg->legacy_prx         = 0;
    cfg->legacy_prx_name[0] = '\0';
    cfg->heap_probe         = 1;
    cfg->heap_adjuster      = 0; /* Default: 100% Read-Only Safety Probe */
    cfg->heap_size_mb       = 1024;

    cfg->menu_position      = 0;
    cfg->menu_scale         = 1;
    cfg->theme_accent       = 0;
    cfg->sound_enabled      = 1;
    cfg->keep_steering_open = 0;
    cfg->nav_stick_enabled  = 0;
    cfg->speedo_mode        = 0;
    cfg->fp_fov_idx         = 0;
    cfg->fp_lock            = 0;
    cfg->fp_clip_fix        = 0;
    cfg->cfg_version        = 4;

    cfg->bind_open_mod      = 0x00000800u; /* R1 */
    cfg->bind_open_btn      = 0x00000040u; /* D-Pad Down */
    cfg->bind_accept        = 0x00004000u; /* Cross */
    cfg->bind_cancel        = 0x00002000u; /* Circle */
    cfg->bind_tab_prev      = 0x00000400u; /* L1 */
    cfg->bind_tab_next      = 0x00000800u; /* R1 */
    cfg->bind_horn_boost    = 0u;          /* Unbound by default */
    cfg->bind_noclip        = 0u;
    cfg->bind_quick_repair  = 0u;
    cfg->bind_quick_heal    = 0u;
    cfg->bind_super_brake   = 0u;
}

static int is_single_bit_u32(uint32_t v) {
    return v != 0u && (v & (v - 1u)) == 0u;
}

/* Repair colliding/broken keybinds and keep First Person lock non-persistent */
static int sanitize_menu_config(ModhenConfig *cfg) {
    int dirty = 0;
    const uint32_t DPAD = 0x000000F0u;

    if (cfg->fp_lock) {
        cfg->fp_lock = 0;
        dirty = 1;
    }
    if (cfg->menu_position < 0 || cfg->menu_position > 2)  { cfg->menu_position = 0; dirty = 1; }
    if (cfg->menu_scale    < 0 || cfg->menu_scale    > 2)  { cfg->menu_scale    = 1; dirty = 1; }
    if (cfg->theme_accent  < 0 || cfg->theme_accent  > 5)  { cfg->theme_accent  = 0; dirty = 1; }
    if (cfg->speedo_mode   < 0 || cfg->speedo_mode   > 2)  { cfg->speedo_mode   = 0; dirty = 1; }
    if (cfg->fp_fov_idx    < 0 || cfg->fp_fov_idx   > 10)  { cfg->fp_fov_idx    = 0; dirty = 1; }

    if (!is_single_bit_u32(cfg->bind_accept) || (cfg->bind_accept & DPAD)) {
        cfg->bind_accept = 0x00004000u; /* Cross */
        dirty = 1;
    }
    if (!is_single_bit_u32(cfg->bind_cancel) || (cfg->bind_cancel & DPAD) ||
        cfg->bind_cancel == cfg->bind_accept) {
        cfg->bind_cancel = (cfg->bind_accept != 0x00002000u) ? 0x00002000u : 0x00001000u;
        dirty = 1;
    }
    if (!is_single_bit_u32(cfg->bind_tab_prev) || (cfg->bind_tab_prev & DPAD) ||
        cfg->bind_tab_prev == cfg->bind_accept || cfg->bind_tab_prev == cfg->bind_cancel) {
        cfg->bind_tab_prev = 0x00000400u; /* L1 */
        dirty = 1;
    }
    if (!is_single_bit_u32(cfg->bind_tab_next) || (cfg->bind_tab_next & DPAD) ||
        cfg->bind_tab_next == cfg->bind_accept || cfg->bind_tab_next == cfg->bind_cancel ||
        cfg->bind_tab_next == cfg->bind_tab_prev) {
        cfg->bind_tab_next = (cfg->bind_tab_prev != 0x00000800u) ? 0x00000800u : 0x00000400u;
        dirty = 1;
    }

    if (!is_single_bit_u32(cfg->bind_open_btn)) {
        cfg->bind_open_mod = 0x00000800u;
        cfg->bind_open_btn = 0x00000040u;
        dirty = 1;
    }
    if (cfg->bind_open_mod && !is_single_bit_u32(cfg->bind_open_mod)) {
        cfg->bind_open_mod = 0x00000800u;
        dirty = 1;
    }
    if (cfg->bind_open_mod == cfg->bind_open_btn) {
        cfg->bind_open_mod = 0u;
        dirty = 1;
    }

    /* Block invalid open combos like D-Pad+Cross or bare D-Pad/Accept/Cancel */
    int bad_open = 0;
    if (cfg->bind_open_btn == cfg->bind_accept || cfg->bind_open_btn == cfg->bind_cancel) {
        bad_open = 1;
    } else if (cfg->bind_open_mod == 0u) {
        if ((cfg->bind_open_btn & DPAD) ||
            cfg->bind_open_btn == cfg->bind_tab_prev ||
            cfg->bind_open_btn == cfg->bind_tab_next) {
            bad_open = 1;
        }
    } else {
        if ((cfg->bind_open_mod & DPAD) ||
            cfg->bind_open_mod == cfg->bind_accept ||
            cfg->bind_open_mod == cfg->bind_cancel) {
            bad_open = 1;
        }
    }
    if (bad_open) {
        cfg->bind_open_mod = 0x00000800u; /* R1 */
        cfg->bind_open_btn = 0x00000040u; /* D-Pad Down */
        dirty = 1;
    }

    uint32_t nav_forbidden = DPAD | cfg->bind_accept | cfg->bind_cancel;
    if (cfg->bind_horn_boost   && (!is_single_bit_u32(cfg->bind_horn_boost)   || (cfg->bind_horn_boost   & nav_forbidden))) { cfg->bind_horn_boost   = 0u; dirty = 1; }
    if (cfg->bind_noclip       && (!is_single_bit_u32(cfg->bind_noclip)       || (cfg->bind_noclip       & nav_forbidden))) { cfg->bind_noclip       = 0u; dirty = 1; }
    if (cfg->bind_quick_repair && (!is_single_bit_u32(cfg->bind_quick_repair) || (cfg->bind_quick_repair & nav_forbidden))) { cfg->bind_quick_repair = 0u; dirty = 1; }
    if (cfg->bind_quick_heal   && (!is_single_bit_u32(cfg->bind_quick_heal)   || (cfg->bind_quick_heal   & nav_forbidden))) { cfg->bind_quick_heal   = 0u; dirty = 1; }
    if (cfg->bind_super_brake  && (!is_single_bit_u32(cfg->bind_super_brake)  || (cfg->bind_super_brake  & nav_forbidden))) { cfg->bind_super_brake  = 0u; dirty = 1; }

    return dirty;
}

static uint32_t cfg_parse_u32(const char *val) {
    if (!val || !val[0]) return 0u;
    return (uint32_t)strtoul(val, NULL, 0);
}

int modhen_config_load(ModhenConfig *cfg) {
    if (!cfg) return -1;
    modhen_config_defaults(cfg);

    FILE *f = fopen(MODHEN_CONFIG_PATH, "r");
    if (!f) {
        modhen_config_save(cfg);
        return 0;
    }

    int loaded_version = 1;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        cfg_trim(line);
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        cfg_trim(key);
        cfg_trim(val);

        if (strcasecmp(key, "cfg_version") == 0) {
            int v = atoi(val);
            if (v > 0) loaded_version = v;
        } else if (strcasecmp(key, "native_orbx") == 0) {
            cfg->native_orbx = (strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0) ? 1 : 0;
        } else if (strcasecmp(key, "legacy_prx") == 0) {
            cfg->legacy_prx = (strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0) ? 1 : 0;
        } else if (strcasecmp(key, "legacy_prx_name") == 0) {
            snprintf(cfg->legacy_prx_name, sizeof(cfg->legacy_prx_name), "%s", val);
        } else if (strcasecmp(key, "heap_probe") == 0) {
            cfg->heap_probe = (strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0) ? 1 : 0;
        } else if (strcasecmp(key, "heap_adjuster") == 0) {
            cfg->heap_adjuster = (strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0) ? 1 : 0;
        } else if (strcasecmp(key, "heap_size_mb") == 0) {
            int mb = atoi(val);
            if (mb > 0) cfg->heap_size_mb = mb;
        } else if (strcasecmp(key, "menu_position") == 0) {
            cfg->menu_position = atoi(val);
        } else if (strcasecmp(key, "menu_scale") == 0) {
            cfg->menu_scale = atoi(val);
        } else if (strcasecmp(key, "theme_accent") == 0) {
            cfg->theme_accent = atoi(val);
        } else if (strcasecmp(key, "sound_enabled") == 0) {
            cfg->sound_enabled = atoi(val) ? 1 : 0;
        } else if (strcasecmp(key, "keep_steering_open") == 0) {
            cfg->keep_steering_open = atoi(val) ? 1 : 0;
        } else if (strcasecmp(key, "nav_stick_enabled") == 0) {
            cfg->nav_stick_enabled = atoi(val) ? 1 : 0;
        } else if (strcasecmp(key, "speedo_mode") == 0) {
            cfg->speedo_mode = atoi(val);
        } else if (strcasecmp(key, "fp_fov_idx") == 0) {
            cfg->fp_fov_idx = atoi(val);
        } else if (strcasecmp(key, "fp_lock") == 0) {
            cfg->fp_lock = atoi(val) ? 1 : 0;
        } else if (strcasecmp(key, "fp_clip_fix") == 0) {
            cfg->fp_clip_fix = atoi(val) ? 1 : 0;
        } else if (strcasecmp(key, "bind_open_mod") == 0) {
            cfg->bind_open_mod = cfg_parse_u32(val);
        } else if (strcasecmp(key, "bind_open_btn") == 0) {
            uint32_t b = cfg_parse_u32(val);
            if (b != 0u) cfg->bind_open_btn = b;
        } else if (strcasecmp(key, "bind_accept") == 0) {
            uint32_t b = cfg_parse_u32(val);
            if (b != 0u) cfg->bind_accept = b;
        } else if (strcasecmp(key, "bind_cancel") == 0) {
            uint32_t b = cfg_parse_u32(val);
            if (b != 0u) cfg->bind_cancel = b;
        } else if (strcasecmp(key, "bind_tab_prev") == 0) {
            uint32_t b = cfg_parse_u32(val);
            if (b != 0u) cfg->bind_tab_prev = b;
        } else if (strcasecmp(key, "bind_tab_next") == 0) {
            uint32_t b = cfg_parse_u32(val);
            if (b != 0u) cfg->bind_tab_next = b;
        } else if (strcasecmp(key, "bind_horn_boost") == 0) {
            cfg->bind_horn_boost = cfg_parse_u32(val);
        } else if (strcasecmp(key, "bind_noclip") == 0) {
            cfg->bind_noclip = cfg_parse_u32(val);
        } else if (strcasecmp(key, "bind_quick_repair") == 0) {
            cfg->bind_quick_repair = cfg_parse_u32(val);
        } else if (strcasecmp(key, "bind_quick_heal") == 0) {
            cfg->bind_quick_heal = cfg_parse_u32(val);
        } else if (strcasecmp(key, "bind_super_brake") == 0) {
            cfg->bind_super_brake = cfg_parse_u32(val);
        }
    }
    fclose(f);
    if (loaded_version < 3) {
        /* Migrate pre-v3 configs off broken nav binds and sticky FP/camera settings */
        cfg->bind_open_mod     = 0x00000800u; /* R1 */
        cfg->bind_open_btn     = 0x00000040u; /* D-Pad Down */
        cfg->bind_accept       = 0x00004000u; /* Cross */
        cfg->bind_cancel       = 0x00002000u; /* Circle */
        cfg->bind_tab_prev     = 0x00000400u; /* L1 */
        cfg->bind_tab_next     = 0x00000800u; /* R1 */
    }
    /* Never preload runtime/camera/gameplay mod settings from disk across boots */
    int had_preloaded = (cfg->keep_steering_open || cfg->nav_stick_enabled ||
                         cfg->speedo_mode || cfg->fp_fov_idx ||
                         cfg->fp_lock || cfg->fp_clip_fix) ? 1 : 0;
    cfg->keep_steering_open = 0;
    cfg->nav_stick_enabled  = 0;
    cfg->speedo_mode        = 0;
    cfg->fp_fov_idx         = 0;
    cfg->fp_lock            = 0;
    cfg->fp_clip_fix        = 0;
    cfg->cfg_version        = 4;
    if (sanitize_menu_config(cfg) || loaded_version < 4 || had_preloaded) {
        modhen_config_save(cfg);
    }
    return 0;
}

int modhen_config_save(const ModhenConfig *cfg) {
    if (!cfg) return -1;
    mkdir("/data/modHEN", 0777);

    FILE *f = fopen(MODHEN_CONFIG_PATH, "w");
    if (!f) return -1;

    fprintf(f, "[ENGINE]\n");
    fprintf(f, "native_orbx=%d\n", cfg->native_orbx ? 1 : 0);
    fprintf(f, "legacy_prx=%d\n", cfg->legacy_prx ? 1 : 0);
    fprintf(f, "legacy_prx_name=%s\n", cfg->legacy_prx_name);
    fprintf(f, "\n[HEAP]\n");
    fprintf(f, "heap_probe=%d\n", cfg->heap_probe ? 1 : 0);
    fprintf(f, "heap_adjuster=%d\n", cfg->heap_adjuster ? 1 : 0);
    fprintf(f, "heap_size_mb=%d\n", cfg->heap_size_mb > 0 ? cfg->heap_size_mb : 1024);
    fprintf(f, "\n[MENU]\n");
    fprintf(f, "cfg_version=%d\n", cfg->cfg_version >= 4 ? cfg->cfg_version : 4);
    fprintf(f, "menu_position=%d\n", cfg->menu_position);
    fprintf(f, "menu_scale=%d\n", cfg->menu_scale);
    fprintf(f, "theme_accent=%d\n", cfg->theme_accent);
    fprintf(f, "sound_enabled=%d\n", cfg->sound_enabled ? 1 : 0);
    fprintf(f, "keep_steering_open=0\n");
    fprintf(f, "nav_stick_enabled=0\n");
    fprintf(f, "speedo_mode=0\n");
    fprintf(f, "fp_fov_idx=0\n");
    fprintf(f, "fp_lock=0\n");
    fprintf(f, "fp_clip_fix=0\n");
    fprintf(f, "\n[KEYBINDS]\n");
    fprintf(f, "bind_open_mod=0x%08x\n", cfg->bind_open_mod);
    fprintf(f, "bind_open_btn=0x%08x\n", cfg->bind_open_btn);
    fprintf(f, "bind_accept=0x%08x\n", cfg->bind_accept);
    fprintf(f, "bind_cancel=0x%08x\n", cfg->bind_cancel);
    fprintf(f, "bind_tab_prev=0x%08x\n", cfg->bind_tab_prev);
    fprintf(f, "bind_tab_next=0x%08x\n", cfg->bind_tab_next);
    fprintf(f, "bind_horn_boost=0x%08x\n", cfg->bind_horn_boost);
    fprintf(f, "bind_noclip=0x%08x\n", cfg->bind_noclip);
    fprintf(f, "bind_quick_repair=0x%08x\n", cfg->bind_quick_repair);
    fprintf(f, "bind_quick_heal=0x%08x\n", cfg->bind_quick_heal);
    fprintf(f, "bind_super_brake=0x%08x\n", cfg->bind_super_brake);
    fclose(f);
    chmod(MODHEN_CONFIG_PATH, 0777);
    return 0;
}

