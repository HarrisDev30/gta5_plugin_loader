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
    cfg->native_orbx = 1;
    cfg->legacy_prx  = 0;
    cfg->legacy_prx_name[0] = '\0';
}

int modhen_config_load(ModhenConfig *cfg) {
    if (!cfg) return -1;
    modhen_config_defaults(cfg);

    FILE *f = fopen(MODHEN_CONFIG_PATH, "r");
    if (!f) {
        modhen_config_save(cfg);
        return 0;
    }

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

        if (strcasecmp(key, "native_orbx") == 0) {
            cfg->native_orbx = (strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0) ? 1 : 0;
        } else if (strcasecmp(key, "legacy_prx") == 0) {
            cfg->legacy_prx = (strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0) ? 1 : 0;
        } else if (strcasecmp(key, "legacy_prx_name") == 0) {
            snprintf(cfg->legacy_prx_name, sizeof(cfg->legacy_prx_name), "%s", val);
        }
    }
    fclose(f);
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
    fclose(f);
    chmod(MODHEN_CONFIG_PATH, 0777);
    return 0;
}

