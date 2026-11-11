/*
 * PS5 GTA V modHEN Plugin & Mod Loader Daemon
 * Compatible Console: PS5 Firmware 13.60 / All FW
 * Target Game: Grand Theft Auto V (PS4 Version: CUSA00419 / CUSA00411)
 *
 * Capabilities:
 * - Standalone Native ELF Daemon (Zero GoldHEN or external HEN required)
 * - HyperHEN-Derived nullfs Sandbox Staging (FreeBSD nmount loopback)
 * - Synchronous scePadReadState Controller Trampoline Hooking
 * - Game-Native In-Pthread Module Loading (sceKernelLoadStartModule)
 * - Orbx Engine / RaiDougie Process-Info & Libkernel Syscall Gate Compatibility Bridge
 * - Embedded LAN Web UI & Mod Manager on Port 8050
 * - PlayStation On-Screen Toast Notifications
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/user.h>

#include <ps5/kernel.h>

#include "log.h"
#include "nullfs_mount.h"
#include "pad_hook.h"
#include "hyperhen_injector.h"
#include "orbx_compat.h"
#include "orbx_native_host.h"
#include "modhen_config.h"
#include "web_server.h"

#define HTTP_PORT           8050
#define PRIMARY_PLUGINS_DIR "/data/modHEN/plugins"
#define FALLBACK_PLUGINS    "/data/plugins"
#define LEGACY_PLUGINS      "/data/gta/plugins"
#define LOG_PATH            "/data/modHEN/loader.log"

extern const uint8_t _binary_orbx_prx_start[];
extern const uint8_t _binary_orbx_prx_end[];
extern const uint8_t _binary_orbx_ini_start[];
extern const uint8_t _binary_orbx_ini_end[];

typedef struct notify_request {
    char useless1[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int device, notify_request_t *req, size_t size, int null);
int sceKernelGetAppInfo(pid_t pid, void *info);
int sceKernelSetProcessName(const char *name);

typedef struct __attribute__((packed)) app_info {
    uint32_t app_id;
    uint64_t unknown1;
    uint32_t app_type;
    char     title_id[10];
    char     unknown2[0x3c];
} app_info_t;

static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_msg(const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    pthread_mutex_lock(&g_log_mutex);
    printf("%s", buf);
    append_live_log(buf);

    FILE *f = fopen(LOG_PATH, "ab");
    if (f) {
        fputs(buf, f);
        fflush(f);
        fclose(f);
    }
    pthread_mutex_unlock(&g_log_mutex);
}

void notify(const char *message) {
    notify_request_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.message, sizeof(req.message), "%s", message);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

// Elevate process credentials to full root and escape jail
static void elevate_proc_full(pid_t pid) {
    intptr_t rootvnode = kernel_get_root_vnode();
    if (rootvnode) {
        kernel_set_proc_rootdir(pid, rootvnode);
        kernel_set_proc_jaildir(pid, rootvnode);
    }
    if (KERNEL_ADDRESS_PRISON0) {
        kernel_set_ucred_prison(pid, KERNEL_ADDRESS_PRISON0);
    }
    kernel_set_ucred_uid(pid, 0);
    kernel_set_ucred_ruid(pid, 0);
    kernel_set_ucred_svuid(pid, 0);
    kernel_set_ucred_rgid(pid, 0);
    kernel_set_ucred_svgid(pid, 0);
    kernel_set_ucred_authid(pid, 0x4800000000010003ULL);

    uint8_t caps[16];
    memset(caps, 0xff, sizeof(caps));
    kernel_set_ucred_caps(pid, caps);

    uint8_t attrs[32];
    memset(attrs, 0, sizeof(attrs));
    attrs[0] = 0x80;
    kernel_set_ucred_attrs(pid, attrs);
}

void elevate_self(void) {
    elevate_proc_full(getpid());
}

static void deploy_file_if_missing(const char *path, const uint8_t *start, const uint8_t *end, const char *label) {
    struct stat st;
    size_t sz = (size_t)(end - start);
    if (stat(path, &st) != 0 || st.st_size == 0) {
        FILE *f = fopen(path, "wb");
        if (f) {
            fwrite(start, 1, sz, f);
            fclose(f);
            chmod(path, 0777);
            log_msg("[modHEN] Deployed %s (%zu bytes) -> %s\n", label, sz, path);
        }
    }
}

// Initialize directory hierarchy on PS5 SSD and deploy configs/plugins
void prepare_directories(void) {
    mkdir("/data/modHEN", 0777);
    mkdir("/data/modHEN/plugins", 0777);
    mkdir("/data/plugins", 0777);
    mkdir("/data/orbx", 0777);
    mkdir("/data/orbx/gta", 0777);
    mkdir("/data/orbx/gta/data", 0777);
    mkdir("/data/orbx/gta/data/levels", 0777);
    mkdir("/data/orbx/gta/data/levels/gta5", 0777);
    mkdir("/data/orbx/gta/data/levels/gta5/weather", 0777);
    mkdir("/data/orbx/gta/data/timecycle", 0777);
    mkdir("/data/orbx/gta/rpf", 0777);
    mkdir("/data/orbx/gta/rpf/dlc_addon", 0777);

    // 1. Deploy /data/orbx/orbx.ini and plugins/orbx.ini if missing
    deploy_file_if_missing("/data/orbx/orbx.ini", _binary_orbx_ini_start, _binary_orbx_ini_end, "orbx.ini");
    deploy_file_if_missing(PRIMARY_PLUGINS_DIR "/orbx.ini", _binary_orbx_ini_start, _binary_orbx_ini_end, "orbx.ini");

    // 1b. Seed /data/orbx/gta/addon_cars.txt template once if missing
    struct stat car_st;
    if (stat("/data/orbx/gta/addon_cars.txt", &car_st) != 0) {
        FILE *cf = fopen("/data/orbx/gta/addon_cars.txt", "w");
        if (cf) {
            fputs("# modHEN Add-On DLC Vehicle List (model or model|Display Name)\n"
                  "27rev|Lamborghini Revuelto\n"
                  "r35|Nissan GTR R35\n", cf);
            fclose(cf);
            chmod("/data/orbx/gta/addon_cars.txt", 0777);
            log_msg("[modHEN] Seeded /data/orbx/gta/addon_cars.txt\n");
        }
    }

    // 2. Ensure /data/modHEN/config.ini exists (Native Orbx ON, Legacy PRX OFF by default)
    ModhenConfig cfg;
    modhen_config_load(&cfg);
    log_msg("[modHEN] Engine Config: native_orbx=%d, legacy_prx=%d, legacy_prx_name='%s'\n",
            cfg.native_orbx, cfg.legacy_prx, cfg.legacy_prx_name);

    // 3. Keep legacy orbx.prx staged on disk (only injected if user enables legacy_prx)
    size_t embedded_prx_sz = (size_t)(_binary_orbx_prx_end - _binary_orbx_prx_start);
    struct stat st;
    if (stat(PRIMARY_PLUGINS_DIR "/orbx.prx", &st) != 0 || st.st_size < 1000000) {
        unlink(PRIMARY_PLUGINS_DIR "/orbx.prx");
        FILE *f = fopen(PRIMARY_PLUGINS_DIR "/orbx.prx", "wb");
        if (f) {
            fwrite(_binary_orbx_prx_start, 1, embedded_prx_sz, f);
            fclose(f);
            chmod(PRIMARY_PLUGINS_DIR "/orbx.prx", 0777);
            log_msg("[modHEN] Staged legacy orbx.prx (%zu bytes, inactive unless legacy_prx=1)\n", embedded_prx_sz);
        }
    } else {
        log_msg("[modHEN] Legacy orbx.prx present on disk (%lld bytes, inactive unless legacy_prx=1)\n",
                (long long)st.st_size);
    }
}

static int is_gta5_title_id(const char *id) {
    if (!id || id[0] == '\0') return 0;
    // Standard GTA V Title IDs: CUSA00410 through CUSA00419
    if (strncasecmp(id, "CUSA0041", 8) == 0) return 1;
    // Regional releases
    if (strcasecmp(id, "CUSA00419") == 0 || strcasecmp(id, "CUSA00411") == 0 ||
        strcasecmp(id, "CUSA00412") == 0 || strcasecmp(id, "CUSA00413") == 0 ||
        strcasecmp(id, "CUSA00410") == 0) return 1;
    return 0;
}

// Scan running processes specifically for GTA V (CUSA0041*)
pid_t find_gta_process(char *out_title) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t buf_size = 0;
    app_info_t appinfo;

    if (sysctl(mib, 4, NULL, &buf_size, NULL, 0) != 0 || buf_size == 0) {
        return 0;
    }

    void *buf = malloc(buf_size);
    if (!buf) return 0;

    if (sysctl(mib, 4, buf, &buf_size, NULL, 0) != 0) {
        free(buf);
        return 0;
    }

    pid_t found_pid = 0;
    pid_t my_pid = getpid();

    for (void *ptr = buf; ptr < (buf + buf_size); ) {
        struct kinfo_proc *ki = (struct kinfo_proc*)ptr;
        if (ki->ki_structsize == 0) break;
        ptr += ki->ki_structsize;

        if (ki->ki_pid == my_pid || ki->ki_pid <= 10) continue;
        if (ki->ki_stat == 5) continue; // Skip zombie processes (SZOMB=5)
        if (strstr(ki->ki_comm, "loader") || strstr(ki->ki_comm, "modHEN") || strstr(ki->ki_comm, "payload")) continue;

        // ONLY examine processes named "eboot.bin" to prevent spamming system daemons with IPC queries
        if (strcmp(ki->ki_comm, "eboot.bin") != 0) continue;

        memset(&appinfo, 0, sizeof(appinfo));
        if (sceKernelGetAppInfo(ki->ki_pid, &appinfo) == 0 && appinfo.title_id[0] != '\0') {
            if (is_gta5_title_id(appinfo.title_id)) {
                found_pid = ki->ki_pid;
                if (out_title) snprintf(out_title, 16, "%s", appinfo.title_id);
                break;
            }
        }
    }

    free(buf);
    return found_pid;
}

static int resolve_legacy_prx_name(const ModhenConfig *cfg, char *out_name, size_t max_len) {
    if (cfg->legacy_prx_name[0] != '\0') {
        char candidate[512];
        snprintf(candidate, sizeof(candidate), "%s/%s", PRIMARY_PLUGINS_DIR, cfg->legacy_prx_name);
        struct stat st;
        if (stat(candidate, &st) == 0 && st.st_size > 0) {
            snprintf(out_name, max_len, "%s", cfg->legacy_prx_name);
            return 0;
        }
    }

    DIR *d = opendir(PRIMARY_PLUGINS_DIR);
    if (!d) return -1;

    char fallback_orbx[128] = {0};
    struct dirent *ent;
    int found = -1;
    while ((ent = readdir(d)) != NULL) {
        size_t nlen = strlen(ent->d_name);
        if (nlen > 4 && strcasecmp(ent->d_name + nlen - 4, ".prx") == 0) {
            if (strcasecmp(ent->d_name, "orbx.prx") == 0) {
                snprintf(fallback_orbx, sizeof(fallback_orbx), "%s", ent->d_name);
            } else {
                snprintf(out_name, max_len, "%s", ent->d_name);
                found = 0;
                break;
            }
        }
    }
    closedir(d);

    if (found != 0 && fallback_orbx[0] != '\0') {
        snprintf(out_name, max_len, "%s", fallback_orbx);
        found = 0;
    }
    return found;
}

// Handle GTA V lifecycle injection
void handle_game_launch(pid_t pid, const char *title_id) {
    log_msg("====================================================\n");
    log_msg(" GTA V Detected: PID %d (%s)\n", pid, title_id);
    log_msg("====================================================\n");

    set_web_server_game_status(1, pid, title_id);

    char toast[256];
    snprintf(toast, sizeof(toast), "GTA V Detected (%s, PID %d)", title_id, pid);
    notify(toast);

    ModhenConfig cfg;
    modhen_config_load(&cfg);
    log_msg("[modHEN] Active Config: native_orbx=%d, legacy_prx=%d, heap_probe=%d, heap_adjuster=%d, heap_size_mb=%d, legacy_prx_name='%s'\n",
            cfg.native_orbx, cfg.legacy_prx, cfg.heap_probe, cfg.heap_adjuster, cfg.heap_size_mb, cfg.legacy_prx_name);

    // Poll every 150ms until eboot GOT / libkernel imports are ready so we hook BEFORE update.rpf loads
    log_msg("[modHEN] Waiting for eboot.bin dynamic linker...\n");
    uint32_t waited_ms = 0;
    for (int i = 0; i < 30; i++) {
        if (kill(pid, 0) != 0) {
            log_msg("[modHEN] Game exited during boot wait, aborting staging.\n");
            return;
        }
        if (find_eboot_got_slot_by_nid(pid, "1G3lF1Gg1k8") != 0) {
            break;
        }
        usleep(150000);
        waited_ms += 150;
    }
    if (kill(pid, 0) != 0) return;
    log_msg("[modHEN] Dynamic linker ready after %ums\n", waited_ms);

    // 1. Mount /data/orbx and /data/modHEN/plugins into sandbox via nullfs
    char sandbox_orbx[128] = {0};
    if (mount_sandbox_subpath(title_id, "/data/orbx", "data/orbx", NULL, sandbox_orbx, sizeof(sandbox_orbx)) == 0) {
        log_msg("[modHEN] Sandbox mounted: %s -> /data/orbx\n", sandbox_orbx);
    } else {
        log_msg("[modHEN] Warning: /data/orbx nullfs mount returned non-zero!\n");
    }

    char legacy_name[128] = {0};
    int have_legacy_name = (cfg.legacy_prx && resolve_legacy_prx_name(&cfg, legacy_name, sizeof(legacy_name)) == 0);

    char sandbox_plugins[128] = {0};
    int mres = mount_sandbox_subpath(title_id, PRIMARY_PLUGINS_DIR, "plugins",
                                     have_legacy_name ? legacy_name : NULL,
                                     sandbox_plugins, sizeof(sandbox_plugins));
    if (mres == 0) {
        log_msg("[modHEN] Sandbox mounted: %s\n", sandbox_plugins);
    }

    // 2. Keep GTA V inside its jail root so /app0 and /savedata0 remain intact while nullfs exposes /data/orbx
    intptr_t rootvnode = kernel_get_root_vnode();
    intptr_t game_root = kernel_get_proc_rootdir(pid);
    int is_unjailed = (rootvnode != 0 && game_root == rootvnode);
    log_msg("[modHEN] Game PID %d root status: %s (gameRoot=0x%lx, rootVnode=0x%lx)\n",
            pid, is_unjailed ? "UNJAILED (Global Root)" : "SANDBOXED (Jail Root)",
            (unsigned long)game_root, (unsigned long)rootvnode);

    // 3. Install ModHEN Orbx Native FS Redirection Engine (PS5 Native, Zero XOM faults)
    if (cfg.native_orbx) {
        if (install_orbx_native(pid) == 0) {
            uint32_t mods = 0;
            get_orbx_native_stats(&mods, NULL, NULL, NULL);
            snprintf(toast, sizeof(toast), "ModHEN Orbx Native Active! (%u mod keys)", mods);
            notify(toast);
        } else {
            log_msg("[modHEN] Error: ModHEN Orbx Native initialization failed!\n");
            notify("ModHEN Orbx Native failed! Check Web Logs.");
        }
    } else {
        log_msg("[modHEN] Native Orbx disabled in %s\n", MODHEN_CONFIG_PATH);
    }

    // 4. Optional Legacy PS4 .prx Slot via HyperHEN Pad Hook
    if (cfg.legacy_prx) {
        if (!have_legacy_name) {
            log_msg("[modHEN] Legacy PRX enabled, but no .prx file found in %s\n", PRIMARY_PLUGINS_DIR);
        } else {
            char target_plugin[256];
            if (is_unjailed) {
                snprintf(target_plugin, sizeof(target_plugin), "%s/%s", PRIMARY_PLUGINS_DIR, legacy_name);
            } else {
                snprintf(target_plugin, sizeof(target_plugin), "%s/%s",
                         sandbox_plugins[0] ? sandbox_plugins : "/plugins", legacy_name);
            }
            log_msg("[modHEN] Legacy PRX Slot armed for: %s (waiting 3s for libScePad...)\n", target_plugin);
            sleep(3);

            uint32_t handle = 0;
            int hres = install_pad_hook(pid, target_plugin, &handle);
            if (hres == 0) {
                if (handle > 0) {
                    log_msg("[modHEN] Legacy PRX Loaded: %s (Handle: 0x%08x)\n", target_plugin, handle);
                } else {
                    log_msg("[modHEN] Legacy PRX Pad Hook armed for %s\n", target_plugin);
                }
            } else {
                log_msg("[modHEN] Legacy PRX Pad Hook failed (%d)\n", hres);
            }
        }
    }
}

static volatile sig_atomic_t g_keep_running = 1;

static void handle_exit_signal(int sig) {
    (void)sig;
    g_keep_running = 0;
}

static void kill_previous_loader_instances(void) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t buf_size = 0;
    if (sysctl(mib, 4, NULL, &buf_size, NULL, 0) != 0 || buf_size == 0) return;

    void *buf = malloc(buf_size);
    if (!buf) return;
    if (sysctl(mib, 4, buf, &buf_size, NULL, 0) != 0) {
        free(buf);
        return;
    }

    pid_t my_pid = getpid();
    pid_t victims[16];
    size_t victim_count = 0;

    for (void *ptr = buf; ptr < (buf + buf_size); ) {
        struct kinfo_proc *ki = (struct kinfo_proc *)ptr;
        if (ki->ki_structsize == 0) break;
        ptr += ki->ki_structsize;
        if (ki->ki_pid == my_pid || ki->ki_pid <= 1) continue;
        if (strcmp(ki->ki_comm, "modHEN_loader") == 0) {
            if (victim_count < 16) {
                victims[victim_count++] = ki->ki_pid;
            }
        }
    }
    free(buf);

    if (victim_count == 0) return;

    // Phase 1: Graceful SIGTERM request so old instance finishes loops & closes sockets
    for (size_t i = 0; i < victim_count; i++) {
        kill(victims[i], SIGTERM);
    }

    // Wait up to 800ms for old process to exit cleanly
    for (int step = 0; step < 8; step++) {
        int any_alive = 0;
        for (size_t i = 0; i < victim_count; i++) {
            if (kill(victims[i], 0) == 0) {
                any_alive = 1;
                break;
            }
        }
        if (!any_alive) {
            usleep(150000); // 150ms buffer to ensure port 8050 is fully released by OS
            return;
        }
        usleep(100000);
    }

    // Phase 2: Force kill only if it refused to terminate gracefully
    for (size_t i = 0; i < victim_count; i++) {
        if (kill(victims[i], 0) == 0) {
            kill(victims[i], SIGKILL);
        }
    }
    usleep(250000);
}

int main(void) {
    sceKernelSetProcessName("modHEN_loader");
    elevate_self();
    kill_previous_loader_instances();

    signal(SIGTERM, handle_exit_signal);
    signal(SIGINT,  handle_exit_signal);
    signal(SIGHUP,  handle_exit_signal);
    signal(SIGQUIT, handle_exit_signal);

    unlink(LOG_PATH);
    log_msg("====================================================\n");
    log_msg(" PlayStation 5 - modHEN Plugin & Mod Loader\n");
    log_msg(" Build: Native Orbx v1.1 (Early Hook + Self-Replace)\n");
    log_msg(" Firmware: PS5 13.60 / All FW\n");
    log_msg(" HTTP Web Dashboard Port: %d\n", HTTP_PORT);
    log_msg(" Plugins Directory: %s\n", PRIMARY_PLUGINS_DIR);
    log_msg("====================================================\n");

    prepare_directories();

    // Start Web Server on port 8050
    if (start_web_server(HTTP_PORT) == 0) {
        log_msg("[modHEN] Web Dashboard started on port %d\n", HTTP_PORT);
    } else {
        log_msg("[modHEN] Warning: Web Dashboard failed to start on port %d\n", HTTP_PORT);
    }

    notify("modHEN PS5 Loader Active! (Port 8050) Launch GTA V now.");

    pid_t last_pid = 0;

    while (g_keep_running) {
        if (last_pid > 0) {
            // Check if game process is still alive FIRST!
            if (kill(last_pid, 0) != 0) {
                log_msg("[modHEN] Game session ended (PID %d)\n", last_pid);
                reset_orbx_native_state();
                set_web_server_game_status(0, 0, NULL);

                // Give SceShellCore 300ms to begin its own app teardown without VFS lock contention
                usleep(300000);
                unmount_sandbox_nullfs("CUSA00419");
                last_pid = 0;

                // 1.0s cooldown to allow SceShellCore to restore full foreground focus and event handling
                usleep(1000000);
                continue;
            }

            // Only poll telemetry while process is verified alive!
            poll_orbx_native_telemetry(last_pid);
            usleep(50000);
            continue;
        }

        char title_id[16] = {0};
        pid_t gta_pid = find_gta_process(title_id);
        if (gta_pid > 0) {
            last_pid = gta_pid;
            handle_game_launch(gta_pid, title_id);
        }

        usleep(500000); // 500ms launch detection polling (gentle on OS IPC)
    }

    log_msg("[modHEN] Daemon shutting down gracefully...\n");
    if (last_pid > 0) {
        reset_orbx_native_state();
        unmount_sandbox_nullfs("CUSA00419");
    }
    stop_web_server();
    log_msg("[modHEN] Shutdown complete.\n");
    return 0;
}
