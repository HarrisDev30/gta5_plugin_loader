#define _GNU_SOURCE
#include "web_server.h"
#include "web_assets.h"
#include "modhen_config.h"
#include "orbx_native_host.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE (128 * 1024)
#define LOG_PATH    "/data/modHEN/loader.log"
#define LIVE_LOG_CAP (64 * 1024)

static int g_server_fd = -1;
static volatile int g_server_running = 0;
static pthread_t g_server_thread;

static int g_game_running = 0;
static pid_t g_game_pid = 0;
static char g_game_title[32] = {0};

static char g_live_log[LIVE_LOG_CAP] = "[modHEN] Daemon initialized. Waiting for game...\n";
static size_t g_live_log_len = 50;
static pthread_mutex_t g_live_log_mutex = PTHREAD_MUTEX_INITIALIZER;

void append_live_log(const char *msg) {
    if (!msg || !*msg) return;
    pthread_mutex_lock(&g_live_log_mutex);
    size_t mlen = strlen(msg);
    if (g_live_log_len + mlen + 1 >= LIVE_LOG_CAP) {
        // Shift buffer to keep most recent logs
        size_t keep = LIVE_LOG_CAP / 2;
        memmove(g_live_log, g_live_log + (g_live_log_len - keep), keep);
        g_live_log_len = keep;
    }
    memcpy(g_live_log + g_live_log_len, msg, mlen);
    g_live_log_len += mlen;
    g_live_log[g_live_log_len] = '\0';
    pthread_mutex_unlock(&g_live_log_mutex);
}

void set_web_server_game_status(int running, pid_t pid, const char *title_id) {
    g_game_running = running;
    g_game_pid = pid;
    if (title_id) {
        snprintf(g_game_title, sizeof(g_game_title), "%s", title_id);
    } else {
        g_game_title[0] = '\0';
    }
}

static int write_all_fd(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    int eagain_retries = 0;
    while (sent < len) {
#ifdef MSG_NOSIGNAL
        ssize_t w = send(fd, p + sent, len - sent, MSG_NOSIGNAL);
#else
        ssize_t w = write(fd, p + sent, len - sent);
#endif
        if (w > 0) {
            sent += (size_t)w;
            eagain_retries = 0;
            continue;
        }
        if (w < 0 && errno == EINTR) {
            continue;
        }
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && eagain_retries < 2) {
            eagain_retries++;
            continue;
        }
        return -1;
    }
    return 0;
}

static void send_http_response(int fd, int status_code, const char *status_text, 
                               const char *content_type, const void *data, size_t len) {
    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Content-Length\r\n"
        "Cache-Control: no-cache, no-store, must-revalidate\r\n"
        "Connection: close\r\n\r\n",
        status_code, status_text, content_type, len);
        
    if (hlen > 0) {
        if (write_all_fd(fd, header, (size_t)hlen) != 0) return;
    }
    if (data && len > 0) {
        write_all_fd(fd, data, len);
    }
}

static void get_query_param(const char *url, const char *key, char *out, size_t max_len) {
    out[0] = '\0';
    char search_key[128];
    snprintf(search_key, sizeof(search_key), "%s=", key);
    
    char *p = strstr(url, search_key);
    if (!p) return;
    p += strlen(search_key);
    
    size_t i = 0;
    while (*p && *p != '&' && *p != ' ' && i < max_len - 1) {
        if (*p == '%' && *(p+1) && *(p+2)) {
            char hex[3] = { *(p+1), *(p+2), 0 };
            out[i++] = (char)strtol(hex, NULL, 16);
            p += 3;
        } else if (*p == '+') {
            out[i++] = ' ';
            p++;
        } else {
            out[i++] = *p++;
        }
    }
    out[i] = '\0';
}

static void handle_api_list(int client_fd, const char *url) {
    char path[512] = {0};
    get_query_param(url, "path", path, sizeof(path));
    if (path[0] == '\0') {
        strcpy(path, "/data/modHEN/plugins");
    }

    // Normalize: strip trailing slash unless path is literally "/"
    size_t plen = strlen(path);
    while (plen > 1 && path[plen - 1] == '/') {
        path[--plen] = '\0';
    }
    
    DIR *dir = opendir(path);
    if (!dir) {
        send_http_response(client_fd, 200, "OK", "application/json", "[]", 2);
        return;
    }
    
    size_t cap = 256 * 1024;
    char *json = malloc(cap);
    if (!json) {
        closedir(dir);
        send_http_response(client_fd, 500, "Internal Error", "text/plain", "OOM", 3);
        return;
    }
    
    strcpy(json, "[");
    size_t len = 1;
    
    struct dirent *ep;
    int first = 1;
    while ((ep = readdir(dir)) != NULL) {
        if (strcmp(ep->d_name, ".") == 0 || strcmp(ep->d_name, "..") == 0) continue;
        
        char full[1024];
        if (strcmp(path, "/") == 0) {
            snprintf(full, sizeof(full), "/%s", ep->d_name);
        } else {
            snprintf(full, sizeof(full), "%s/%s", path, ep->d_name);
        }
        struct stat st;
        int is_dir = 0;
        long long fsize = 0;
        if (stat(full, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
            fsize = (long long)st.st_size;
        } else if (lstat(full, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
            fsize = (long long)st.st_size;
        }
        
        char item[1024];
        int ilen = snprintf(item, sizeof(item),
            "%s{\"name\":\"%s\",\"is_dir\":%s,\"size\":%lld}",
            first ? "" : ",",
            ep->d_name,
            is_dir ? "true" : "false",
            fsize);
            
        if (len + ilen + 2 < cap) {
            strcat(json, item);
            len += ilen;
            first = 0;
        }
    }
    closedir(dir);
    strcat(json, "]");
    len += 1;
    
    send_http_response(client_fd, 200, "OK", "application/json", json, len);
    free(json);
}

static void mkdir_p(const char *dir_path) {
    if (!dir_path || !*dir_path) return;
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", dir_path);
    size_t len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/') tmp[--len] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

static void mkdir_parents_for_file(const char *file_path) {
    if (!file_path) return;
    char parent[512];
    snprintf(parent, sizeof(parent), "%s", file_path);
    char *slash = strrchr(parent, '/');
    if (slash) {
        if (slash == parent) return; // Root "/"
        *slash = '\0';
        mkdir_p(parent);
    }
}

static void handle_api_status(int client_fd) {
    ModhenConfig cfg;
    modhen_config_load(&cfg);
    uint32_t mod_count = 0, fs_calls = 0, fs_redirects = 0;
    int native_active = 0;
    get_orbx_native_stats(&mod_count, &fs_calls, &fs_redirects, &native_active);

    char safe_prx[128] = {0};
    size_t j = 0;
    for (size_t i = 0; cfg.legacy_prx_name[i] != '\0' && j + 1 < sizeof(safe_prx); i++) {
        char c = cfg.legacy_prx_name[i];
        if (c == '"' || c == '\\') continue;
        safe_prx[j++] = c;
    }
    safe_prx[j] = '\0';

    char resp[1024];
    int len = snprintf(resp, sizeof(resp),
        "{\"status\":\"ok\",\"port\":8050,\"game_running\":%s,\"pid\":%d,\"title_id\":\"%s\","
        "\"native_orbx\":%s,\"legacy_prx\":%s,\"legacy_prx_name\":\"%s\","
        "\"native_active\":%s,\"mod_count\":%u,\"fs_calls\":%u,\"fs_redirects\":%u}",
        g_game_running ? "true" : "false",
        (int)g_game_pid,
        g_game_title,
        cfg.native_orbx ? "true" : "false",
        cfg.legacy_prx ? "true" : "false",
        safe_prx,
        native_active ? "true" : "false",
        mod_count, fs_calls, fs_redirects);
    send_http_response(client_fd, 200, "OK", "application/json", resp, len);
}

static void handle_api_config_get(int client_fd) {
    ModhenConfig cfg;
    modhen_config_load(&cfg);
    char resp[512];
    int len = snprintf(resp, sizeof(resp),
        "{\"native_orbx\":%s,\"legacy_prx\":%s,\"legacy_prx_name\":\"%s\","
        "\"heap_probe\":%s,\"heap_adjuster\":%s,\"heap_size_mb\":%d}",
        cfg.native_orbx ? "true" : "false",
        cfg.legacy_prx ? "true" : "false",
        cfg.legacy_prx_name,
        cfg.heap_probe ? "true" : "false",
        cfg.heap_adjuster ? "true" : "false",
        cfg.heap_size_mb);
    send_http_response(client_fd, 200, "OK", "application/json", resp, len);
}

static void handle_api_config_post(int client_fd, const char *url) {
    ModhenConfig cfg;
    modhen_config_load(&cfg);

    char val[128] = {0};
    get_query_param(url, "native_orbx", val, sizeof(val));
    if (val[0] != '\0') {
        cfg.native_orbx = (strcmp(val, "1") == 0 || strcasecmp(val, "true") == 0) ? 1 : 0;
    }

    val[0] = '\0';
    get_query_param(url, "legacy_prx", val, sizeof(val));
    if (val[0] != '\0') {
        cfg.legacy_prx = (strcmp(val, "1") == 0 || strcasecmp(val, "true") == 0) ? 1 : 0;
    }

    val[0] = '\0';
    get_query_param(url, "legacy_prx_name", val, sizeof(val));
    if (strstr(url, "legacy_prx_name=") != NULL) {
        snprintf(cfg.legacy_prx_name, sizeof(cfg.legacy_prx_name), "%s", val);
    }

    val[0] = '\0';
    get_query_param(url, "heap_probe", val, sizeof(val));
    if (val[0] != '\0') {
        cfg.heap_probe = (strcmp(val, "1") == 0 || strcasecmp(val, "true") == 0) ? 1 : 0;
    }

    val[0] = '\0';
    get_query_param(url, "heap_adjuster", val, sizeof(val));
    if (val[0] != '\0') {
        cfg.heap_adjuster = (strcmp(val, "1") == 0 || strcasecmp(val, "true") == 0) ? 1 : 0;
    }

    val[0] = '\0';
    get_query_param(url, "heap_size_mb", val, sizeof(val));
    if (val[0] != '\0') {
        int mb = atoi(val);
        if (mb > 0) cfg.heap_size_mb = mb;
    }

    if (modhen_config_save(&cfg) != 0) {
        send_http_response(client_fd, 500, "Error", "text/plain", "Failed saving config.ini", 23);
        return;
    }
    log_msg("[WEB] Updated Engine Config: native_orbx=%d, legacy_prx=%d, heap_probe=%d, heap_adjuster=%d, heap_size_mb=%d\n",
            cfg.native_orbx, cfg.legacy_prx, cfg.heap_probe, cfg.heap_adjuster, cfg.heap_size_mb);
    handle_api_config_get(client_fd);
}

static void handle_api_menu_get(int client_fd) {
    OrbxMenuState ms;
    memset(&ms, 0, sizeof(ms));
    int ok = (get_orbx_menu_state(&ms) == 0);
    if (!ok) {
        ms.lock_hour = -1;
        ms.cloud_hat_idx = -1;
        ms.cloud_keyframe_idx = -1;
        ms.density_pct = 100u;
        ms.cloud_alpha_pct = 100u;
        ms.gfx_strength_pct = 100u;
        ms.menu_scale = 1u;
    }
    char safe_arg[40] = {0};
    size_t j = 0;
    for (size_t i = 0; i < sizeof(ms.probe_last_arg) && ms.probe_last_arg[i] != '\0' && j + 1 < sizeof(safe_arg); i++) {
        char c = ms.probe_last_arg[i];
        if (c == '"' || c == '\\' || (unsigned char)c < 32) continue;
        safe_arg[j++] = c;
    }
    safe_arg[j] = '\0';

    GnmCrashProbe gprobe = {0};
    uint64_t gnm_flips = 0;
    get_orbx_gnm_probe(&gprobe, &gnm_flips);

    GnmShaderProbe sprobe = {0};
    get_orbx_shader_probe(&sprobe);

    GnmDepthProbe dprobe = {0};
    get_orbx_depth_probe(&dprobe);

    char cars_json[4096];
    size_t cj_len = 0;
    cars_json[0] = '\0';
    uint32_t ac_cnt = ms.addon_car_count;
    if (ac_cnt > ORBX_MAX_ADDON_CARS) ac_cnt = ORBX_MAX_ADDON_CARS;
    for (uint32_t i = 0; i < ac_cnt; i++) {
        if (!ms.addon_cars[i].name[0]) continue;
        char item[128];
        int ilen = snprintf(item, sizeof(item),
                            "%s{\"name\":\"%s\",\"label\":\"%s\",\"hash\":\"0x%08X\"}",
                            (cj_len > 0) ? "," : "",
                            ms.addon_cars[i].name,
                            ms.addon_cars[i].label[0] ? ms.addon_cars[i].label : ms.addon_cars[i].name,
                            ms.addon_cars[i].hash);
        if (cj_len + (size_t)ilen + 1 < sizeof(cars_json)) {
            memcpy(cars_json + cj_len, item, (size_t)ilen);
            cj_len += (size_t)ilen;
            cars_json[cj_len] = '\0';
        }
    }

    char resp[8192];
    int len = snprintf(resp, sizeof(resp),
        "{\"ready\":%s,\"armed\":%u,\"ticks\":%u,\"fps\":%.1f,\"menu_open\":%u,"
        "\"pad_calls\":%u,\"pad_raw\":%u,\"last_btns\":%u,"
        "\"godmode\":%u,\"never_wanted\":%u,\"super_jump\":%u,\"fast_run\":%u,"
        "\"no_ragdoll\":%u,\"invisible\":%u,\"noclip_on\":%u,\"spec_ability_inf\":%u,"
        "\"inf_ammo\":%u,\"explosive_ammo\":%u,\"fire_ammo\":%u,\"explosive_melee\":%u,"
        "\"veh_god\":%u,\"seatbelt_on\":%u,\"horn_boost\":%u,"
        "\"veh_drift_mode\":%u,\"speedo_mode\":%u,"
        "\"fuel_system_on\":%u,\"fuel_level\":%.1f,\"calmer_police_on\":%u,"
        "\"gta6_hud_on\":%u,\"gta6_wheel_on\":%u,"
        "\"density_pct\":%u,\"lock_hour\":%d,\"weather_idx\":%d,\"weather_persist\":%u,"
        "\"clock_paused\":%u,\"time_scale_idx\":%u,\"low_gravity\":%u,\"mobile_radio\":%u,"
        "\"cloud_hat_idx\":%d,\"cloud_keyframe_idx\":%d,\"cloud_alpha_pct\":%u,"
        "\"fp_fov_idx\":%u,\"fp_lock\":%u,\"fp_clip_fix\":%u,"
        "\"afp_mode\":%u,\"afp_fov_idx\":%u,\"afp_headbob\":%u,\"afp_smooth_follow\":%u,"
        "\"afp_recoil\":%u,\"afp_inertia\":%u,\"afp_crouch_lower\":%u,\"afp_ads_zoom\":%u,"
        "\"afp_crosshair\":%u,\"afp_autoturn\":%u,\"afp_clip_fix\":%u,"
        "\"cinematic_cam\":%u,\"cam_anti_shake\":%u,"
        "\"cam_ped_view_mode\":%u,\"cam_veh_view_mode\":%u,"
        "\"menu_position\":%u,\"menu_scale\":%u,\"theme_accent\":%u,"
        "\"keep_steering\":%u,\"nav_stick\":%u,\"sound_enabled\":%u,"
        "\"bind_open_mod\":%u,\"bind_open_btn\":%u,\"bind_accept\":%u,\"bind_cancel\":%u,"
        "\"bind_tab_prev\":%u,\"bind_tab_next\":%u,\"bind_horn_boost\":%u,\"bind_noclip\":%u,"
        "\"bind_quick_repair\":%u,\"bind_quick_heal\":%u,\"bind_super_brake\":%u,"
        "\"gfx_preset_idx\":%u,\"gfx_strength_pct\":%u,\"gfx_film_grain\":%u,\"gfx_postfx_idx\":%u,"
        "\"enb_extra_active\":%u,\"enb_shadow_soft\":%u,\"enb_shadow_scale\":%u,"
        "\"probe_stage\":%u,\"probe_last_native_id\":%u,\"probe_resolved_count\":%u,"
        "\"probe_total_natives\":%u,\"probe_last_arg\":\"%s\","
        "\"gnm_stage\":%u,\"gnm_display_idx\":%u,\"gnm_dcb_count\":%u,\"gnm_dcb_total_kb\":%u,\"gnm_flips\":%llu,"
        "\"shader_loaded\":%u,\"shader_enabled\":%u,\"shader_type\":%u,\"shader_size\":%u,"
        "\"shader_dispatches\":%u,\"shader_err\":%u,\"shader_msg\":\"%s\","
        "\"shader_bloom_thresh\":%.2f,\"shader_bloom_int\":%.2f,\"shader_exposure\":%.2f,"
        "\"shader_contrast\":%.2f,\"shader_saturation\":%.2f,\"shader_vignette\":%.2f,"
        "\"shader_aa\":%.2f,\"shader_sharpness\":%.2f,"
        "\"depth_va\":\"0x%llX\",\"depth_width\":%u,\"depth_height\":%u,\"depth_hits\":%llu,"
        "\"ssao_int\":%.2f,\"ssao_cp\":%.2f,\"ssr_int\":%.2f,\"ssr_dist\":%.1f,"
        "\"addon_car_count\":%u,\"addon_cars\":[%s]}",
        ok ? "true" : "false",
        ms.armed, ms.main_ticks, (double)ms.fps_x10 / 10.0, ms.menu_open,
        ms.pad_calls, ms.pad_raw_seen, ms.prev_btn_mask,
        ms.godmode, ms.never_wanted, ms.super_jump, ms.fast_run,
        ms.no_ragdoll, ms.invisible, ms.noclip_on, ms.spec_ability_inf,
        ms.inf_ammo, ms.explosive_ammo, ms.fire_ammo, ms.explosive_melee,
        ms.veh_god, ms.seatbelt_on, ms.horn_boost,
        ms.veh_drift_mode, ms.speedo_mode,
        ms.fuel_system_on, (double)ms.fuel_level, ms.calmer_police_on,
        ms.gta6_hud_on, ms.gta6_wheel_on,
        ms.density_pct, (int)ms.lock_hour, (int)ms.weather_idx, ms.weather_persist,
        ms.clock_paused, ms.time_scale_idx, ms.low_gravity, ms.mobile_radio,
        (int)ms.cloud_hat_idx, (int)ms.cloud_keyframe_idx, ms.cloud_alpha_pct,
        ms.fp_fov_idx, ms.fp_lock, ms.fp_clip_fix,
        ms.afp_mode, ms.afp_fov_idx, ms.afp_headbob, ms.afp_smooth_follow,
        ms.afp_recoil, ms.afp_inertia, ms.afp_crouch_lower, ms.afp_ads_zoom,
        ms.afp_crosshair, ms.afp_autoturn, ms.afp_clip_fix,
        ms.cinematic_cam, ms.cam_anti_shake,
        ms.cam_ped_view_mode, ms.cam_veh_view_mode,
        ms.menu_position, ms.menu_scale, ms.theme_accent,
        ms.keep_steering, ms.nav_stick, ms.sound_enabled,
        ms.bind_open_mod, ms.bind_open_btn, ms.bind_accept, ms.bind_cancel,
        ms.bind_tab_prev, ms.bind_tab_next, ms.bind_horn_boost, ms.bind_noclip,
        ms.bind_quick_repair, ms.bind_quick_heal, ms.bind_super_brake,
        ms.gfx_preset_idx, ms.gfx_strength_pct ? ms.gfx_strength_pct : 100u,
        ms.gfx_film_grain, ms.gfx_postfx_idx,
        ms.enb_extra_active, ms.enb_shadow_soft, ms.enb_shadow_scale,
        ms.probe_stage, ms.probe_last_native_id, ms.probe_resolved_count,
        (unsigned)ORBX_NAT_COUNT, safe_arg[0] ? safe_arg : "NONE",
        gprobe.stage, gprobe.last_display_index, gprobe.last_dcb_count,
        (gprobe.last_dcb_total_sz / 1024u), (unsigned long long)gnm_flips,
        sprobe.loaded, sprobe.enabled, sprobe.shader_type, sprobe.shader_size,
        sprobe.dispatches, sprobe.error_code, sprobe.last_error_msg,
        sprobe.bloom_threshold, sprobe.bloom_intensity, sprobe.exposure,
        sprobe.contrast, sprobe.saturation, sprobe.vignette,
        sprobe.anti_aliasing, sprobe.sharpness,
        (unsigned long long)dprobe.depth_buffer_va, dprobe.depth_width, dprobe.depth_height, (unsigned long long)dprobe.depth_hits,
        dprobe.ssao_intensity, dprobe.ssao_cp_strength, dprobe.ssr_intensity, dprobe.ssr_distance,
        ac_cnt, cars_json);
    send_http_response(client_fd, 200, "OK", "application/json", resp, len);
}

static void handle_api_menu_post(int client_fd, const char *url) {
    char tog[64] = {0}, val_s[32] = {0}, act_s[32] = {0}, arg_s[32] = {0}, spawn_s[32] = {0};
    char name_s[48] = {0}, save_s[16] = {0}, reload_s[16] = {0};
    get_query_param(url, "toggle", tog, sizeof(tog));
    get_query_param(url, "val", val_s, sizeof(val_s));
    get_query_param(url, "action", act_s, sizeof(act_s));
    get_query_param(url, "arg0", arg_s, sizeof(arg_s));
    get_query_param(url, "spawn", spawn_s, sizeof(spawn_s));
    get_query_param(url, "name", name_s, sizeof(name_s));
    get_query_param(url, "save", save_s, sizeof(save_s));
    get_query_param(url, "reload", reload_s, sizeof(reload_s));

    if (tog[0] != '\0') {
        uint32_t v = (uint32_t)strtol(val_s, NULL, 0);
        if (set_orbx_menu_toggle(tog, v) != 0) {
            send_http_response(client_fd, 400, "Bad Request", "text/plain", "Menu not ready or bad toggle", 28);
            return;
        }
    }
    if (reload_s[0] != '\0') {
        if (reload_orbx_addon_cars() < 0) {
            send_http_response(client_fd, 400, "Bad Request", "text/plain", "Menu not ready", 14);
            return;
        }
    }
    if (name_s[0] != '\0') {
        int save_flag = (strcmp(save_s, "1") == 0 || strcasecmp(save_s, "true") == 0) ? 1 : 0;
        if (spawn_orbx_vehicle_by_name(name_s, save_flag) != 0) {
            send_http_response(client_fd, 400, "Bad Request", "text/plain", "Menu not ready or bad name", 26);
            return;
        }
    } else if (act_s[0] != '\0') {
        uint32_t act = (uint32_t)strtoul(act_s, NULL, 0);
        int32_t  arg0 = (int32_t)strtol(arg_s, NULL, 0);
        uint32_t shash = (uint32_t)strtoul(spawn_s, NULL, 0);
        if (trigger_orbx_menu_action(act, arg0, shash) != 0) {
            send_http_response(client_fd, 400, "Bad Request", "text/plain", "Menu not ready", 14);
            return;
        }
    }
    handle_api_menu_get(client_fd);
}

static void handle_api_log_clear(int client_fd) {
    FILE *lf = fopen(LOG_PATH, "wb");
    if (lf) fclose(lf);

    pthread_mutex_lock(&g_live_log_mutex);
    const char *cleared = "[modHEN] Logs cleared.\n";
    size_t clen = strlen(cleared);
    memcpy(g_live_log, cleared, clen);
    g_live_log_len = clen;
    g_live_log[g_live_log_len] = '\0';
    pthread_mutex_unlock(&g_live_log_mutex);

    send_http_response(client_fd, 200, "OK", "application/json", "{\"status\":\"ok\",\"message\":\"Logs cleared\"}", 39);
}

static void handle_api_shader_get(int client_fd) {
    GnmShaderProbe sp = {0};
    get_orbx_shader_probe(&sp);
    char resp[1024];
    int len = snprintf(resp, sizeof(resp),
        "{\"loaded\":%u,\"enabled\":%u,\"shader_type\":%u,\"size\":%u,"
        "\"dispatches\":%u,\"error_code\":%u,\"msg\":\"%s\",\"file\":\"%s\","
        "\"bloom_threshold\":%.2f,\"bloom_intensity\":%.2f,\"exposure\":%.2f,"
        "\"contrast\":%.2f,\"saturation\":%.2f,\"vignette\":%.2f,"
        "\"anti_aliasing\":%.2f,\"sharpness\":%.2f}",
        sp.loaded, sp.enabled, sp.shader_type, sp.shader_size,
        sp.dispatches, sp.error_code, sp.last_error_msg, sp.shader_file,
        sp.bloom_threshold, sp.bloom_intensity, sp.exposure,
        sp.contrast, sp.saturation, sp.vignette,
        sp.anti_aliasing, sp.sharpness);
    send_http_response(client_fd, 200, "OK", "application/json", resp, len);
}

static void handle_api_shader_post(int client_fd, const char *url) {
    char tog_s[16] = {0}, rel_s[16] = {0};
    char bth_s[16] = {0}, bin_s[16] = {0}, exp_s[16] = {0}, con_s[16] = {0}, sat_s[16] = {0}, vig_s[16] = {0};
    char aa_s[16] = {0}, sharp_s[16] = {0};

    get_query_param(url, "toggle", tog_s, sizeof(tog_s));
    get_query_param(url, "reload", rel_s, sizeof(rel_s));
    get_query_param(url, "bth", bth_s, sizeof(bth_s));
    get_query_param(url, "bin", bin_s, sizeof(bin_s));
    get_query_param(url, "exp", exp_s, sizeof(exp_s));
    get_query_param(url, "con", con_s, sizeof(con_s));
    get_query_param(url, "sat", sat_s, sizeof(sat_s));
    get_query_param(url, "vig", vig_s, sizeof(vig_s));
    get_query_param(url, "aa", aa_s, sizeof(aa_s));
    get_query_param(url, "sharp", sharp_s, sizeof(sharp_s));

    if (rel_s[0] != '\0') {
        reload_orbx_shaders();
    }
    if (tog_s[0] != '\0') {
        uint32_t en = (uint32_t)atoi(tog_s);
        set_orbx_shader_enabled(en);
    }
    if (bth_s[0] != '\0' || bin_s[0] != '\0' || exp_s[0] != '\0' || con_s[0] != '\0' ||
        sat_s[0] != '\0' || vig_s[0] != '\0' || aa_s[0] != '\0' || sharp_s[0] != '\0') {
        char focus_s[16] = {0};
        get_query_param(url, "focus", focus_s, sizeof(focus_s));

        GnmShaderProbe sp = {0};
        get_orbx_shader_probe(&sp);
        float bth = bth_s[0] ? (float)atof(bth_s) : sp.bloom_threshold;
        float bint = bin_s[0] ? (float)atof(bin_s) : sp.bloom_intensity;
        float exp = exp_s[0] ? (float)atof(exp_s) : sp.exposure;
        float con = con_s[0] ? (float)atof(con_s) : sp.contrast;
        float sat = sat_s[0] ? (float)atof(sat_s) : sp.saturation;
        float vig = vig_s[0] ? (float)atof(vig_s) : sp.vignette;
        float aa  = aa_s[0]  ? (float)atof(aa_s)  : sp.anti_aliasing;
        float shp = sharp_s[0]? (float)atof(sharp_s): sp.sharpness;
        set_orbx_shader_tunables(bth, bint, exp, con, sat, vig, aa, shp);

        /* Real-Time PC ENB Post-FX Stacking:
         * Does NOT replace the user's active ENB preset!
         * - Bloom: layers MODHEN_ENB_BLOOM on the extra layer.
         * - Sharpness / AA: drives native SET_NOISINESSOVERIDE film grain / sharpening.
         * - Exposure & Contrast: dynamically tunes the brightness & shadow contrast of the base preset! */
        if (strcmp(focus_s, "bin") == 0 || strcmp(focus_s, "bth") == 0) {
            if (bint > 0.45f) {
                trigger_orbx_menu_action(ORBX_ACT_SET_ENB_EXTRA, 2, 0); /* 2 = MODHEN_ENB_BLOOM */
            } else {
                trigger_orbx_menu_action(ORBX_ACT_SET_ENB_EXTRA, 0, 0);
            }
        } else if (strcmp(focus_s, "aa") == 0 || strcmp(focus_s, "sharp") == 0) {
            uint32_t fg = (uint32_t)(shp * 80.0f);
            set_orbx_menu_toggle("gfx_film_grain", fg);
        } else if (strcmp(focus_s, "exp") == 0 || strcmp(focus_s, "con") == 0) {
            uint32_t pct = (uint32_t)(exp * con * 95.0f);
            if (pct < 10u)  pct = 10u;
            if (pct > 200u) pct = 200u;
            trigger_orbx_menu_action(ORBX_ACT_SET_GFX_STRENGTH, (int32_t)pct, 0);
        } else if (strcmp(focus_s, "sat") == 0) {
            uint32_t pct = (uint32_t)(sat * 90.0f);
            if (pct < 10u)  pct = 10u;
            if (pct > 200u) pct = 200u;
            trigger_orbx_menu_action(ORBX_ACT_SET_GFX_STRENGTH, (int32_t)pct, 0);
        }
    }
    handle_api_shader_get(client_fd);
}

static void handle_api_depth_get(int client_fd) {
    GnmDepthProbe dp = {0};
    GnmCrashProbe gp = {0};
    uint64_t flips = 0;
    get_orbx_depth_probe(&dp);
    get_orbx_gnm_probe(&gp, &flips);
    char resp[768];
    int len = snprintf(resp, sizeof(resp),
        "{\"stage\":%u,\"depth_buffer_va\":\"0x%llX\",\"depth_width\":%u,\"depth_height\":%u,"
        "\"depth_format\":%u,\"depth_samples\":%u,\"depth_hits\":%llu,\"ssao_mode\":%u,"
        "\"ssao_intensity\":%.2f,\"ssao_radius\":%.2f,\"ssao_cp_strength\":%.2f,"
        "\"ssr_intensity\":%.2f,\"ssr_distance\":%.1f,"
        "\"gnm_stage\":%u,\"flips\":%llu,\"last_backbuffer_va\":\"0x%llX\",\"last_dcb_va\":\"0x%llX\",\"last_dcb_sz\":%u}",
        dp.stage, (unsigned long long)dp.depth_buffer_va, dp.depth_width, dp.depth_height,
        dp.depth_format, dp.depth_samples, (unsigned long long)dp.depth_hits, dp.ssao_mode,
        dp.ssao_intensity, dp.ssao_radius, dp.ssao_cp_strength,
        dp.ssr_intensity, dp.ssr_distance,
        gp.stage, (unsigned long long)flips, (unsigned long long)gp.last_backbuffer_va,
        (unsigned long long)gp.last_dcb_va, gp.last_dcb_total_sz);
    send_http_response(client_fd, 200, "OK", "application/json", resp, len);
}

static void handle_api_depth_post(int client_fd, const char *url) {
    char ssao_int_s[16] = {0}, ssao_cp_s[16] = {0}, ssr_int_s[16] = {0}, ssr_dist_s[16] = {0}, focus_s[16] = {0};
    get_query_param(url, "ssao_int", ssao_int_s, sizeof(ssao_int_s));
    get_query_param(url, "ssao_cp", ssao_cp_s, sizeof(ssao_cp_s));
    get_query_param(url, "ssr_int", ssr_int_s, sizeof(ssr_int_s));
    get_query_param(url, "ssr_dist", ssr_dist_s, sizeof(ssr_dist_s));
    get_query_param(url, "focus", focus_s, sizeof(focus_s));

    GnmDepthProbe dp = {0};
    get_orbx_depth_probe(&dp);
    float ssao_int = ssao_int_s[0]  ? (float)atof(ssao_int_s)  : (dp.ssao_intensity ? dp.ssao_intensity : 5.80f);
    float ssao_cp  = ssao_cp_s[0]   ? (float)atof(ssao_cp_s)   : (dp.ssao_cp_strength ? dp.ssao_cp_strength : 0.78f);
    float ssr_int  = ssr_int_s[0]   ? (float)atof(ssr_int_s)   : (dp.ssr_intensity ? dp.ssr_intensity : 2.45f);
    float ssr_dist = ssr_dist_s[0]  ? (float)atof(ssr_dist_s)  : (dp.ssr_distance ? dp.ssr_distance : 500.0f);
    set_orbx_depth_tunables(ssao_int, ssao_cp, ssr_int, ssr_dist);

    /* Real-Time PC ENB Stacking Architecture:
     * Does NOT replace the user's active ENB preset!
     * When user adjusts SSAO or Contact Shadowing, stacks MODHEN_ENB_DARK_AO or MODHEN_ENB_ALL
     * via _SET_EXTRA_TIMECYCLE_MODIFIER on top of whatever base preset is active (QuantV, NVE, etc.).
     * Also tunes GTA V's native Cascade Shadow Maps (CSM_ST_SOFT16 + cascade bounds scale)
     * so that buildings and structures cast deep, soft PC-quality shadows. */
    if (ssao_int >= 1.2f || ssao_cp >= 0.25f) {
        uint32_t extra_layer = (ssr_int > 3.0f) ? 3u : 1u; /* 1 = MODHEN_ENB_DARK_AO, 3 = MODHEN_ENB_ALL */
        trigger_orbx_menu_action(ORBX_ACT_SET_ENB_EXTRA, (int32_t)extra_layer, 0);

        /* bscale in tenths: e.g. ssao_int=5.8 -> bscale=29 (2.9x bounds scale) */
        uint32_t bscale = (uint32_t)(ssao_int * 5.0f);
        if (bscale < 15u) bscale = 15u;
        if (bscale > 50u) bscale = 50u;
        trigger_orbx_menu_action(ORBX_ACT_SET_SHADOW_QUALITY, 1, bscale);
    } else {
        trigger_orbx_menu_action(ORBX_ACT_SET_ENB_EXTRA, 0, 0);
        trigger_orbx_menu_action(ORBX_ACT_SET_SHADOW_QUALITY, 0, 0);
    }

    handle_api_depth_get(client_fd);
}

static void handle_api_read(int client_fd, const char *url) {
    char path[512] = {0};
    get_query_param(url, "path", path, sizeof(path));
    if (path[0] == '\0') {
        send_http_response(client_fd, 400, "Bad Request", "text/plain", "Missing path", 12);
        return;
    }
    
    FILE *f = fopen(path, "rb");
    if (!f) {
        send_http_response(client_fd, 404, "Not Found", "text/plain", "File not found", 14);
        return;
    }
    
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        send_http_response(client_fd, 500, "Server Error", "text/plain", "Seek failed", 11);
        return;
    }
    long fsize = ftell(f);
    if (fsize < 0) {
        fclose(f);
        send_http_response(client_fd, 500, "Server Error", "text/plain", "Tell failed", 11);
        return;
    }
    fseek(f, 0, SEEK_SET);

    struct timeval tv;
    tv.tv_sec = 30;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    char *buf = malloc((size_t)fsize + 1);
    if (!buf) {
        fclose(f);
        send_http_response(client_fd, 500, "Server Error", "text/plain", "Out of memory", 13);
        return;
    }
    
    size_t got = 0;
    while (got < (size_t)fsize) {
        size_t r = fread(buf + got, 1, (size_t)fsize - got, f);
        if (r > 0) {
            got += r;
            continue;
        }
        if (ferror(f) && errno == EINTR) {
            clearerr(f);
            continue;
        }
        break;
    }
    fclose(f);
    buf[got] = '\0';
    
    send_http_response(client_fd, 200, "OK", "text/plain; charset=utf-8", buf, got);
    free(buf);
}

static int fwrite_all(FILE *f, const char *data, size_t len) {
    size_t off = 0;
    while (off < len) {
        size_t w = fwrite(data + off, 1, len - off, f);
        if (w > 0) {
            off += w;
            continue;
        }
        if (errno == EINTR) {
            clearerr(f);
            continue;
        }
        return -1;
    }
    return 0;
}

static void handle_api_stream_save(int client_fd, const char *url,
                                   int has_content_length, size_t content_length,
                                   const char *body, size_t body_len,
                                   int reload_addon_on_dlc) {
    char path[512] = {0};
    get_query_param(url, "path", path, sizeof(path));
    if (path[0] == '\0') {
        send_http_response(client_fd, 400, "Bad Request", "text/plain", "Missing path", 12);
        return;
    }

    struct timeval tv;
    tv.tv_sec = 30;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    mkdir_parents_for_file(path);

    /* When GTA V is running with active nullfs mount over /data/orbx, avoid unlinking/replacing
     * existing lower-layer inodes for small config writes (/api/write) to prevent FreeBSD nullfs vnode locks. */
    struct stat existing_st;
    int write_in_place = (!reload_addon_on_dlc && g_game_running && stat(path, &existing_st) == 0 && S_ISREG(existing_st.st_mode));

    char tmp_path[528];
    if (write_in_place) {
        snprintf(tmp_path, sizeof(tmp_path), "%s", path);
    } else {
        snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
        unlink(tmp_path);
    }
    FILE *f = fopen(tmp_path, "wb");
    if (!f && !write_in_place) {
        rmdir(tmp_path);
        f = fopen(tmp_path, "wb");
    }
    if (!f) {
        send_http_response(client_fd, 500, "Error", "text/plain", "Cannot create file", 18);
        return;
    }

    size_t expected = has_content_length ? content_length : body_len;
    size_t written = 0;
    int io_err = 0;

    if (body && body_len > 0 && expected > 0) {
        size_t to_write = (body_len > expected) ? expected : body_len;
        if (fwrite_all(f, body, to_write) != 0) {
            io_err = 1;
        } else {
            written += to_write;
        }
    }

    if (!io_err && written < expected) {
        char *stream_buf = malloc(BUFFER_SIZE);
        if (!stream_buf) {
            fclose(f);
            unlink(tmp_path);
            send_http_response(client_fd, 500, "OOM", "text/plain", "OOM", 3);
            return;
        }

        int eagain_retries = 0;
        while (written < expected) {
            size_t to_read = expected - written;
            if (to_read > BUFFER_SIZE) to_read = BUFFER_SIZE;
            ssize_t r = read(client_fd, stream_buf, to_read);
            if (r > 0) {
                eagain_retries = 0;
                if (fwrite_all(f, stream_buf, (size_t)r) != 0) {
                    io_err = 1;
                    break;
                }
                written += (size_t)r;
                continue;
            }
            if (r < 0 && errno == EINTR) {
                continue;
            }
            if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && eagain_retries < 1) {
                eagain_retries++;
                continue;
            }
            break;
        }
        free(stream_buf);
    }

    if (fflush(f) != 0) {
        io_err = 1;
    }
    if (fclose(f) != 0) {
        io_err = 1;
    }

    if (io_err) {
        if (!write_in_place) unlink(tmp_path);
        log_msg("[WEB] ERROR: Write failure saving '%s' (%zu/%zu bytes)\n",
                path, written, expected);
        send_http_response(client_fd, 500, "Server Error", "application/json",
                           "{\"status\":\"error\",\"error\":\"write_failed\"}", 42);
        return;
    }

    if (has_content_length && written != expected) {
        if (!write_in_place) unlink(tmp_path);
        log_msg("[WEB] ERROR: Truncated upload rejected for '%s' (%zu/%zu bytes)\n",
                path, written, expected);
        send_http_response(client_fd, 400, "Bad Request", "application/json",
                           "{\"status\":\"error\",\"error\":\"truncated_upload\"}", 46);
        return;
    }

    if (!write_in_place) {
        if (rename(tmp_path, path) != 0) {
            unlink(path);
            rmdir(path);
            if (rename(tmp_path, path) != 0) {
                unlink(tmp_path);
                log_msg("[WEB] ERROR: Rename failed saving '%s' (errno=%d)\n", path, errno);
                send_http_response(client_fd, 500, "Server Error", "application/json",
                                   "{\"status\":\"error\",\"error\":\"rename_failed\"}", 43);
                return;
            }
        }
    }

    chmod(path, 0777);

    /* If uploading a DLC pack, auto-reload addon cars so it shows in the in-game menu */
    if (reload_addon_on_dlc && strstr(path, "/data/orbx/gta/rpf/dlc_addon/") != NULL) {
        reload_orbx_addon_cars();
    }

    char ok_resp[128];
    int ok_len = snprintf(ok_resp, sizeof(ok_resp),
                          "{\"status\":\"ok\",\"bytes\":%zu}", written);
    send_http_response(client_fd, 200, "OK", "application/json", ok_resp, (size_t)ok_len);
}

static void handle_api_write(int client_fd, const char *url, int has_content_length,
                             size_t content_length, const char *body, size_t body_len) {
    handle_api_stream_save(client_fd, url, has_content_length, content_length, body, body_len, 0);
}

static void handle_api_upload(int client_fd, const char *url, int has_content_length,
                              size_t content_length, const char *body, size_t body_len) {
    handle_api_stream_save(client_fd, url, has_content_length, content_length, body, body_len, 1);
}

static void handle_api_mkdir(int client_fd, const char *url) {
    char path[512] = {0};
    get_query_param(url, "path", path, sizeof(path));
    if (path[0] != '\0') {
        mkdir_p(path);
    }
    send_http_response(client_fd, 200, "OK", "application/json", "{\"status\":\"ok\"}", 15);
}

static int remove_recursive_under_data(const char *path) {
    /* Safety guard: only allow recursive deletion inside /data/ */
    if (!path || strncmp(path, "/data/", 6) != 0 || strcmp(path, "/data/") == 0 || strstr(path, "..") != NULL) {
        return -1;
    }
    struct stat st;
    if (lstat(path, &st) != 0) return -1;
    if (!S_ISDIR(st.st_mode)) {
        return unlink(path);
    }
    DIR *d = opendir(path);
    if (!d) return -1;
    struct dirent *ent;
    int rc = 0;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char child[512];
        snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
        if (remove_recursive_under_data(child) != 0) rc = -1;
    }
    closedir(d);
    if (rmdir(path) != 0) rc = -1;
    return rc;
}

static void handle_api_delete(int client_fd, const char *url) {
    char path[512] = {0};
    get_query_param(url, "path", path, sizeof(path));
    if (path[0] == '\0') {
        send_http_response(client_fd, 400, "Bad Request", "text/plain", "Missing path", 12);
        return;
    }

    int res = unlink(path);
    if (res != 0) res = rmdir(path);
    if (res != 0) res = remove_recursive_under_data(path);

    if (res == 0) {
        send_http_response(client_fd, 200, "OK", "application/json", "{\"status\":\"ok\"}", 15);
    } else {
        send_http_response(client_fd, 500, "Error", "text/plain", "Delete failed", 13);
    }
}

static void handle_api_add_dlc(int client_fd, const char *url) {
    char name[128] = {0};
    get_query_param(url, "name", name, sizeof(name));
    if (name[0] == '\0') {
        send_http_response(client_fd, 400, "Bad Request", "text/plain", "Missing DLC name", 16);
        return;
    }

    // Sanitize name
    for (int i = 0; name[i]; i++) {
        if (!((name[i] >= 'a' && name[i] <= 'z') ||
              (name[i] >= 'A' && name[i] <= 'Z') ||
              (name[i] >= '0' && name[i] <= '9') ||
              name[i] == '_' || name[i] == '-')) {
            name[i] = '_';
        }
    }

    // 1. Create directory hierarchy /data/orbx/gta/rpf/dlc_addon/<name>
    mkdir("/data/orbx", 0777);
    mkdir("/data/orbx/gta", 0777);
    mkdir("/data/orbx/gta/rpf", 0777);
    mkdir("/data/orbx/gta/rpf/dlc_addon", 0777);

    char dlc_dir[512];
    snprintf(dlc_dir, sizeof(dlc_dir), "/data/orbx/gta/rpf/dlc_addon/%s", name);
    mkdir(dlc_dir, 0777);

    // 2. Read or create dlclist.xml
    const char *xml_path = "/data/orbx/gta/dlclist.xml";
    char item_line[256];
    snprintf(item_line, sizeof(item_line), "<Item>/data/orbx/gta/rpf/dlc_addon/%s/</Item>", name);

    char *xml_content = NULL;
    size_t xml_size = 0;
    FILE *f = fopen(xml_path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        xml_size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (xml_size > 0 && xml_size < 1024 * 1024) {
            xml_content = malloc(xml_size + 1024);
            if (xml_content) {
                fread(xml_content, 1, xml_size, f);
                xml_content[xml_size] = '\0';
            }
        }
        fclose(f);
    }

    if (!xml_content) {
        // Create initial dlclist.xml template
        char initial_xml[1024];
        snprintf(initial_xml, sizeof(initial_xml),
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<SMandatoryPacksData>\n"
            "  <Paths>\n"
            "    <Item>/data/orbx/gta/rpf/dlc_addon/%s/</Item>\n"
            "  </Paths>\n"
            "</SMandatoryPacksData>\n", name);

        FILE *wf = fopen(xml_path, "wb");
        if (wf) {
            fputs(initial_xml, wf);
            fclose(wf);
            chmod(xml_path, 0777);
        }
    } else {
        // Check if item_line already exists
        if (!strstr(xml_content, item_line)) {
            char *insert_pos = strstr(xml_content, "</Paths>");
            if (insert_pos) {
                size_t prefix_len = insert_pos - xml_content;
                char *new_xml = malloc(xml_size + 512);
                if (new_xml) {
                    memcpy(new_xml, xml_content, prefix_len);
                    size_t off = prefix_len;
                    off += sprintf(new_xml + off, "    %s\n  ", item_line);
                    strcpy(new_xml + off, insert_pos);

                    FILE *wf = fopen(xml_path, "wb");
                    if (wf) {
                        fputs(new_xml, wf);
                        fclose(wf);
                        chmod(xml_path, 0777);
                    }
                    free(new_xml);
                }
            }
        }
        free(xml_content);
    }

    log_msg("[WEB] Added DLC Pack '%s' -> %s\n", name, item_line);
    reload_orbx_addon_cars();
    send_http_response(client_fd, 200, "OK", "application/json", "{\"status\":\"ok\"}", 15);
}

static void *http_client_worker(void *arg) {
    int client_fd = (int)(intptr_t)arg;
    char *buf = malloc(BUFFER_SIZE);
    if (!buf) {
        close(client_fd);
        return NULL;
    }
    
    size_t total_read = 0;
    char *header_end = NULL;
    while (total_read < BUFFER_SIZE - 1) {
        ssize_t r = read(client_fd, buf + total_read, (BUFFER_SIZE - 1) - total_read);
        if (r > 0) {
            total_read += (size_t)r;
            buf[total_read] = '\0';
            header_end = strstr(buf, "\r\n\r\n");
            if (header_end) {
                break;
            }
            continue;
        }
        if (r < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    if (total_read == 0) {
        free(buf);
        close(client_fd);
        return NULL;
    }
    buf[total_read] = '\0';
    if (!header_end) {
        header_end = strstr(buf, "\r\n\r\n");
    }
    
    char method[16] = {0};
    char url[1024] = {0};
    sscanf(buf, "%15s %1023s", method, url);
    
    char *body = NULL;
    size_t body_len = 0;
    if (header_end) {
        body = header_end + 4;
        body_len = total_read - (size_t)(body - buf);
    }
    
    int has_content_length = 0;
    int is_chunked = 0;
    size_t content_length = 0;
    if (header_end) {
        const char *line = buf;
        while (line < header_end) {
            const char *eol = strstr(line, "\r\n");
            if (!eol || eol > header_end) eol = header_end;
            size_t line_len = (size_t)(eol - line);
            if (line_len >= 15 && strncasecmp(line, "Content-Length:", 15) == 0) {
                const char *valp = line + 15;
                while (valp < eol && (*valp == ' ' || *valp == '\t')) valp++;
                char *endp = NULL;
                unsigned long long parsed = strtoull(valp, &endp, 10);
                if (endp != valp) {
                    content_length = (size_t)parsed;
                    has_content_length = 1;
                }
            } else if (line_len >= 18 && strncasecmp(line, "Transfer-Encoding:", 18) == 0) {
                const char *valp = line + 18;
                while (valp < eol && (*valp == ' ' || *valp == '\t')) valp++;
                if ((size_t)(eol - valp) >= 7 && strncasecmp(valp, "chunked", 7) == 0) {
                    is_chunked = 1;
                }
            }
            if (eol == header_end) break;
            line = eol + 2;
        }
    }
    
    if (strcmp(method, "OPTIONS") == 0) {
        send_http_response(client_fd, 204, "No Content", "text/plain", "", 0);
    } else if (strcmp(method, "GET") == 0) {
        if (strcmp(url, "/") == 0 || strcmp(url, "/index.html") == 0) {
            send_http_response(client_fd, 200, "OK", "text/html; charset=utf-8", 
                               WEB_INDEX_HTML, WEB_INDEX_HTML_LEN);
        } else if (strncmp(url, "/api/status", 11) == 0 && (url[11] == '\0' || url[11] == '?')) {
            handle_api_status(client_fd);
        } else if (strncmp(url, "/api/config", 11) == 0 && (url[11] == '\0' || url[11] == '?')) {
            handle_api_config_get(client_fd);
        } else if (strncmp(url, "/api/menu", 9) == 0 && (url[9] == '\0' || url[9] == '?')) {
            handle_api_menu_get(client_fd);
        } else if (strncmp(url, "/api/list", 9) == 0) {
            handle_api_list(client_fd, url);
        } else if (strncmp(url, "/api/read", 9) == 0) {
            handle_api_read(client_fd, url);
        } else if (strncmp(url, "/api/shutdown", 13) == 0 || strncmp(url, "/api/exit", 9) == 0) {
            send_http_response(client_fd, 200, "OK", "application/json", "{\"status\":\"shutting_down\"}", 26);
            free(buf);
            close(client_fd);
            kill(getpid(), SIGTERM);
            return NULL;
        } else if (strncmp(url, "/api/log/clear", 14) == 0) {
            handle_api_log_clear(client_fd);
        } else if (strncmp(url, "/api/shader", 11) == 0) {
            handle_api_shader_get(client_fd);
        } else if (strncmp(url, "/api/depth", 10) == 0) {
            handle_api_depth_get(client_fd);
        } else if (strncmp(url, "/api/log", 8) == 0 || strncmp(url, "/log", 4) == 0) {
            FILE *lf = fopen(LOG_PATH, "rb");
            if (lf) {
                fseek(lf, 0, SEEK_END);
                long lsz = ftell(lf);
                fseek(lf, 0, SEEK_SET);
                if (lsz > 0) {
                    char *lbuf = malloc(lsz + 1);
                    if (lbuf) {
                        size_t rd = fread(lbuf, 1, lsz, lf);
                        lbuf[rd] = '\0';
                        send_http_response(client_fd, 200, "OK", "text/plain; charset=utf-8", lbuf, rd);
                        free(lbuf);
                        fclose(lf);
                        free(buf);
                        close(client_fd);
                        return NULL;
                    }
                }
                fclose(lf);
            }
            // Serve from in-memory live log buffer
            pthread_mutex_lock(&g_live_log_mutex);
            send_http_response(client_fd, 200, "OK", "text/plain; charset=utf-8", g_live_log, g_live_log_len);
            pthread_mutex_unlock(&g_live_log_mutex);
        } else {
            send_http_response(client_fd, 404, "Not Found", "text/plain", "Endpoint not found", 18);
        }
    } else if (strcmp(method, "POST") == 0) {
        if (!header_end) {
            send_http_response(client_fd, 400, "Bad Request", "text/plain", "Incomplete HTTP headers", 23);
        } else if (is_chunked && (strncmp(url, "/api/write", 10) == 0 || strncmp(url, "/api/upload", 11) == 0)) {
            send_http_response(client_fd, 501, "Not Implemented", "text/plain", "Chunked transfer not supported", 30);
        } else if (strncmp(url, "/api/write", 10) == 0) {
            handle_api_write(client_fd, url, has_content_length, content_length, body, body_len);
        } else if (strncmp(url, "/api/upload", 11) == 0) {
            handle_api_upload(client_fd, url, has_content_length, content_length, body, body_len);
        } else if (strncmp(url, "/api/mkdir", 10) == 0) {
            handle_api_mkdir(client_fd, url);
        } else if (strncmp(url, "/api/delete", 11) == 0) {
            handle_api_delete(client_fd, url);
        } else if (strncmp(url, "/api/add_dlc", 12) == 0) {
            handle_api_add_dlc(client_fd, url);
        } else if (strncmp(url, "/api/log/clear", 14) == 0) {
            handle_api_log_clear(client_fd);
        } else if (strncmp(url, "/api/config", 11) == 0) {
            handle_api_config_post(client_fd, url);
        } else if (strncmp(url, "/api/shader", 11) == 0) {
            handle_api_shader_post(client_fd, url);
        } else if (strncmp(url, "/api/depth", 10) == 0) {
            handle_api_depth_post(client_fd, url);
        } else if (strncmp(url, "/api/menu", 9) == 0) {
            handle_api_menu_post(client_fd, url);
        } else {
            send_http_response(client_fd, 404, "Not Found", "text/plain", "Endpoint not found", 18);
        }
    } else {
        send_http_response(client_fd, 405, "Method Not Allowed", "text/plain", "Unsupported method", 18);
    }
    
    free(buf);
    close(client_fd);
    return NULL;
}

static void *server_listener_thread(void *arg) {
    int port = (int)(intptr_t)arg;
    
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) {
        printf("[WEB] Socket creation failed\n");
        return NULL;
    }
    
    int opt = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    int bound = 0;
    for (int attempt = 0; attempt < 5; attempt++) {
        if (bind(g_server_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            bound = 1;
            break;
        }
        usleep(200000);
    }
    if (!bound) {
        log_msg("[WEB] Bind failed on port %d! errno=%d\n", port, errno);
        close(g_server_fd);
        g_server_fd = -1;
        return NULL;
    }
    
    if (listen(g_server_fd, 10) < 0) {
        printf("[WEB] Listen failed! errno=%d\n", errno);
        close(g_server_fd);
        g_server_fd = -1;
        return NULL;
    }
    
    printf("[WEB] Embedded Web Dashboard Listening on http://0.0.0.0:%d\n", port);
    g_server_running = 1;
    
    while (g_server_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(g_server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (!g_server_running) break;
            continue;
        }
        
        // 4s timeout so hanging HTTP clients never leak threads or stall daemon
        struct timeval tv;
        tv.tv_sec = 4;
        tv.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#ifdef SO_NOSIGPIPE
        int nosig = 1;
        setsockopt(client_fd, SOL_SOCKET, SO_NOSIGPIPE, &nosig, sizeof(nosig));
#endif

        pthread_t client_th;
        if (pthread_create(&client_th, NULL, http_client_worker, (void *)(intptr_t)client_fd) == 0) {
            pthread_detach(client_th);
        } else {
            close(client_fd);
        }
    }

    return NULL;
}

int start_web_server(int port) {
    if (g_server_running) return 0;
    return pthread_create(&g_server_thread, NULL, server_listener_thread, (void *)(intptr_t)port);
}

void stop_web_server(void) {
    g_server_running = 0;
    if (g_server_fd >= 0) {
        shutdown(g_server_fd, SHUT_RDWR);
        close(g_server_fd);
        g_server_fd = -1;
    }
}

