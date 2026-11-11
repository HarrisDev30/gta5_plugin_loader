#define _GNU_SOURCE
#include "web_server.h"
#include "web_assets.h"
#include "modhen_config.h"
#include "orbx_native_host.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
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
        
    write(fd, header, hlen);
    if (data && len > 0) {
        write(fd, data, len);
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
        "{\"native_orbx\":%s,\"legacy_prx\":%s,\"legacy_prx_name\":\"%s\"}",
        cfg.native_orbx ? "true" : "false",
        cfg.legacy_prx ? "true" : "false",
        cfg.legacy_prx_name);
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

    if (modhen_config_save(&cfg) != 0) {
        send_http_response(client_fd, 500, "Error", "text/plain", "Failed saving config.ini", 23);
        return;
    }
    log_msg("[WEB] Updated Engine Config: native_orbx=%d, legacy_prx=%d, legacy_prx_name='%s'\n",
            cfg.native_orbx, cfg.legacy_prx, cfg.legacy_prx_name);
    handle_api_config_get(client_fd);
}

static void handle_api_menu_get(int client_fd) {
    OrbxMenuState ms;
    memset(&ms, 0, sizeof(ms));
    int ok = (get_orbx_menu_state(&ms) == 0);
    char resp[768];
    int len = snprintf(resp, sizeof(resp),
        "{\"ready\":%s,\"armed\":%u,\"ticks\":%u,\"fps\":%.1f,\"menu_open\":%u,"
        "\"pad_calls\":%u,\"pad_raw\":%u,\"last_btns\":%u,"
        "\"godmode\":%u,\"never_wanted\":%u,\"super_jump\":%u,\"fast_run\":%u,"
        "\"no_ragdoll\":%u,\"inf_ammo\":%u,\"explosive_ammo\":%u,\"fire_ammo\":%u,"
        "\"explosive_melee\":%u,\"veh_god\":%u,\"horn_boost\":%u,"
        "\"density_pct\":%u,\"lock_hour\":%d,\"weather_idx\":%d}",
        ok ? "true" : "false",
        ms.armed, ms.main_ticks, (double)ms.fps_x10 / 10.0, ms.menu_open,
        ms.pad_calls, ms.pad_raw_seen, ms.prev_btn_mask,
        ms.godmode, ms.never_wanted, ms.super_jump, ms.fast_run,
        ms.no_ragdoll, ms.inf_ammo, ms.explosive_ammo, ms.fire_ammo,
        ms.explosive_melee, ms.veh_god, ms.horn_boost,
        ms.density_pct, (int)ms.lock_hour, (int)ms.weather_idx);
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
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *buf = malloc(fsize + 1);
    if (!buf) {
        fclose(f);
        send_http_response(client_fd, 500, "Server Error", "text/plain", "Out of memory", 13);
        return;
    }
    
    fread(buf, 1, fsize, f);
    fclose(f);
    buf[fsize] = '\0';
    
    send_http_response(client_fd, 200, "OK", "text/plain; charset=utf-8", buf, fsize);
    free(buf);
}

static void handle_api_write(int client_fd, const char *url, const char *body, size_t body_len) {
    char path[512] = {0};
    get_query_param(url, "path", path, sizeof(path));
    if (path[0] == '\0') {
        send_http_response(client_fd, 400, "Bad Request", "text/plain", "Missing path", 12);
        return;
    }
    
    mkdir_parents_for_file(path);
    unlink(path);
    FILE *f = fopen(path, "wb");
    if (!f) {
        rmdir(path);
        f = fopen(path, "wb");
    }
    if (!f) {
        send_http_response(client_fd, 500, "Error", "text/plain", "Cannot write file", 17);
        return;
    }
    
    if (body && body_len > 0) {
        fwrite(body, 1, body_len, f);
    }
    fclose(f);
    chmod(path, 0777);
    
    send_http_response(client_fd, 200, "OK", "application/json", "{\"status\":\"ok\"}", 15);
}

static void handle_api_upload(int client_fd, const char *url, size_t content_length, const char *body, size_t body_len) {
    char path[512] = {0};
    get_query_param(url, "path", path, sizeof(path));
    if (path[0] == '\0') {
        send_http_response(client_fd, 400, "Bad Request", "text/plain", "Missing path", 12);
        return;
    }
    
    mkdir_parents_for_file(path);
    
    /* Cleanly unlink any existing file so replacing/overwriting never fails */
    unlink(path);
    FILE *f = fopen(path, "wb");
    if (!f) {
        rmdir(path);
        f = fopen(path, "wb");
    }
    if (!f) {
        send_http_response(client_fd, 500, "Error", "text/plain", "Cannot create file", 18);
        return;
    }
    
    size_t written = 0;
    if (body && body_len > 0) {
        fwrite(body, 1, body_len, f);
        written += body_len;
    }
    
    char *stream_buf = malloc(BUFFER_SIZE);
    if (!stream_buf) {
        fclose(f);
        send_http_response(client_fd, 500, "OOM", "text/plain", "OOM", 3);
        return;
    }
    
    while (written < content_length) {
        size_t to_read = content_length - written;
        if (to_read > BUFFER_SIZE) to_read = BUFFER_SIZE;
        ssize_t n = read(client_fd, stream_buf, to_read);
        if (n <= 0) break;
        fwrite(stream_buf, 1, n, f);
        written += n;
    }
    
    free(stream_buf);
    fclose(f);
    chmod(path, 0777);

    /* If uploading a DLC pack, auto-reload addon cars so it shows in the in-game menu */
    if (strstr(path, "/data/orbx/gta/rpf/dlc_addon/") != NULL) {
        reload_orbx_addon_cars();
    }

    send_http_response(client_fd, 200, "OK", "application/json", "{\"status\":\"ok\"}", 15);
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
    
    ssize_t n = read(client_fd, buf, BUFFER_SIZE - 1);
    if (n <= 0) {
        free(buf);
        close(client_fd);
        return NULL;
    }
    buf[n] = '\0';
    
    char method[16] = {0};
    char url[1024] = {0};
    sscanf(buf, "%15s %1023s", method, url);
    
    char *header_end = strstr(buf, "\r\n\r\n");
    char *body = NULL;
    size_t body_len = 0;
    if (header_end) {
        body = header_end + 4;
        body_len = n - (body - buf);
    }
    
    size_t content_length = 0;
    char *cl_header = strstr(buf, "Content-Length:");
    if (!cl_header) cl_header = strstr(buf, "content-length:");
    if (cl_header) {
        content_length = strtoul(cl_header + 15, NULL, 10);
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
        if (strncmp(url, "/api/write", 10) == 0) {
            handle_api_write(client_fd, url, body, body_len);
        } else if (strncmp(url, "/api/upload", 11) == 0) {
            handle_api_upload(client_fd, url, content_length, body, body_len);
        } else if (strncmp(url, "/api/mkdir", 10) == 0) {
            handle_api_mkdir(client_fd, url);
        } else if (strncmp(url, "/api/delete", 11) == 0) {
            handle_api_delete(client_fd, url);
        } else if (strncmp(url, "/api/add_dlc", 12) == 0) {
            handle_api_add_dlc(client_fd, url);
        } else if (strncmp(url, "/api/config", 11) == 0) {
            handle_api_config_post(client_fd, url);
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

        pthread_t client_th;
        pthread_create(&client_th, NULL, http_client_worker, (void *)(intptr_t)client_fd);
        pthread_detach(client_th);
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

