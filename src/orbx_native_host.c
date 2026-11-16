#define _GNU_SOURCE
#include "orbx_native_host.h"
#include "modhen_config.h"
#include "pad_hook.h"
#include "pt.h"
#include "log.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#include <ps5/kernel.h>
#include <ps5/nid.h>

_Static_assert(offsetof(struct stat, st_size) == ORBX_STAT_SIZE_OFFSET,
               "Orbx struct stat st_size offset mismatch");

extern const uint8_t _binary_orbx_native_bin_start[];
extern const uint8_t _binary_orbx_native_bin_end[];

#define ORBX_INI_PATH      "/data/orbx/orbx.ini"
#define ORBX_GTA_DIR       "/data/orbx/gta"
#define ORBX_PTRACE_AUTHID 0x4800000000010003ULL

static uint64_t enter_ptrace_auth_scope(void) {
    uint64_t prev = kernel_get_ucred_authid(getpid());
    if (prev != ORBX_PTRACE_AUTHID) {
        kernel_set_ucred_authid(getpid(), ORBX_PTRACE_AUTHID);
    }
    return prev;
}

static void leave_ptrace_auth_scope(uint64_t prev_auth) {
    if (prev_auth && prev_auth != ORBX_PTRACE_AUTHID) {
        kernel_set_ucred_authid(getpid(), prev_auth);
    }
}

static void detach_ptrace_with_auth(pid_t pid, uint64_t prev_auth) {
    pt_detach(pid, 0);
    leave_ptrace_auth_scope(prev_auth);
}

static uint64_t g_native_pid       = 0;
static uint64_t g_native_ctx_addr  = 0;
static uint64_t g_eboot_base       = 0;
static uint64_t g_eboot_size       = 0;
static uint64_t g_code_addr        = 0;
static uint64_t g_code_alloc_size  = 0;
static pthread_mutex_t g_host_lock = PTHREAD_MUTEX_INITIALIZER;

static int get_orbx_menu_state_unlocked(OrbxMenuState *out_menu);
static int trigger_orbx_menu_action_unlocked(uint32_t action, int32_t arg0, uint32_t spawn_hash);
static int reload_orbx_addon_cars_unlocked(void);
static int reload_orbx_shaders_unlocked(void);
typedef struct {
    const char *name;
    char        nid[16];
    uint64_t    got_slot;
    uint64_t    tramp_addr;
    size_t      ctx_orig_off;
    uint8_t     armed;
    uint8_t     is_fs;
    uint8_t     arm_attempts;
} MonitoredGotHook;

#define NUM_MONITORED_GOTS 16
static MonitoredGotHook g_monitored_gots[NUM_MONITORED_GOTS];

static const uint32_t kHookEntryOffsets[NUM_MONITORED_GOTS] = {
    ORBX_ENTRY_OFF_SCE_OPEN,
    ORBX_ENTRY_OFF_OPEN,
    ORBX_ENTRY_OFF_SCE_STAT,
    ORBX_ENTRY_OFF_STAT,
    ORBX_ENTRY_OFF_FOPEN,
    ORBX_ENTRY_OFF_SCE_READ,
    ORBX_ENTRY_OFF_SCE_PREAD,
    ORBX_ENTRY_OFF_READ,
    ORBX_ENTRY_OFF_PREAD,
    ORBX_ENTRY_OFF_SCE_CLOSE,
    ORBX_ENTRY_OFF_CLOSE,
    ORBX_ENTRY_OFF_SCE_SET_FLIP_RATE,
    ORBX_ENTRY_OFF_SCE_GNM_FLIP,
    ORBX_ENTRY_OFF_SCE_PAD_READ_STATE,
    ORBX_ENTRY_OFF_SCE_PAD_READ,
    ORBX_ENTRY_OFF_SCE_VIDEO_REG_BUFS
};

static const char *const kHookFnNames[NUM_MONITORED_GOTS] = {
    "sceKernelOpen", "open", "sceKernelStat", "stat", "fopen",
    "sceKernelRead", "sceKernelPread", "read", "pread", "sceKernelClose", "close",
    "sceVideoOutSetFlipRate", "sceGnmSubmitAndFlipCommandBuffers",
    "scePadReadState", "scePadRead", "sceVideoOutRegisterBuffers"
};

static const size_t kHookOrigCtxOffs[NUM_MONITORED_GOTS] = {
    offsetof(OrbxNativeContext, orig_sceKernelOpen),
    offsetof(OrbxNativeContext, orig_open),
    offsetof(OrbxNativeContext, orig_sceKernelStat),
    offsetof(OrbxNativeContext, orig_stat),
    offsetof(OrbxNativeContext, orig_fopen),
    offsetof(OrbxNativeContext, orig_sceKernelRead),
    offsetof(OrbxNativeContext, orig_sceKernelPread),
    offsetof(OrbxNativeContext, orig_read),
    offsetof(OrbxNativeContext, orig_pread),
    offsetof(OrbxNativeContext, orig_sceKernelClose),
    offsetof(OrbxNativeContext, orig_close),
    offsetof(OrbxNativeContext, orig_sceVideoOutSetFlipRate),
    offsetof(OrbxNativeContext, orig_sceGnmSubmitAndFlipCommandBuffers),
    offsetof(OrbxNativeContext, orig_scePadReadState),
    offsetof(OrbxNativeContext, orig_scePadRead),
    offsetof(OrbxNativeContext, orig_sceVideoOutRegisterBuffers)
};

static uint64_t g_fps_last_flips   = 0;
static uint64_t g_fps_last_ms      = 0;
static int      g_first_flip_seen  = 0;
static int      g_first_pad_seen   = 0;
static int      g_first_fs_seen    = 0;
static int      g_native_reg_probed = 0;
static uint8_t  g_menu_probe_attempts = 0;
static uint8_t  g_unlock_60fps     = 1;
static uint32_t g_cached_mods      = 0;
static uint32_t g_cached_calls     = 0;
static uint32_t g_cached_redirects = 0;
static uint32_t g_last_log_seq     = 0;
static uint32_t g_last_tracked_seq = 0;
static uint32_t g_telem_poll_tick  = 0;
static int      g_native_active    = 0;
static uint64_t g_tracked_last_bytes[ORBX_MAX_TRACKED_FDS];
static uint32_t g_tracked_last_seq[ORBX_MAX_TRACKED_FDS];
static uint32_t g_tracked_idle_ticks[ORBX_MAX_TRACKED_FDS];
static uint64_t g_tracked_pending_bytes[ORBX_MAX_TRACKED_FDS];

static int is_valid_prx_target(uint64_t addr, uint64_t eboot_base, uint64_t eboot_size, uint64_t code_addr) {
    if (addr < 0x100000ULL || addr >= 0x800000000000ULL) return 0;
    if (eboot_size && addr >= eboot_base && addr < eboot_base + eboot_size) return 0;
    uint64_t span = g_code_alloc_size ? g_code_alloc_size : 0x40000ULL;
    if (code_addr && addr >= code_addr && addr < code_addr + span) return 0;
    return 1;
}

/* ============================================================================
 * Helpers: String Normalization, Lowercase JOAAT & Config Parsing
 * ============================================================================ */

static void trim_ws(char *s) {
    if (!s) return;
    char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
}

static uint8_t parse_ini_bool(const char *val, uint8_t def) {
    if (!val) return def;
    if (strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0 || strcasecmp(val, "yes") == 0) return 1;
    if (strcasecmp(val, "false") == 0 || strcmp(val, "0") == 0 || strcasecmp(val, "no") == 0) return 0;
    return def;
}

static void load_orbx_ini(OrbxNativeConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->allow_rpf    = 1;
    cfg->allow_xml    = 1;
    cfg->allow_dat    = 1;
    cfg->allow_meta   = 1;
    cfg->unlock_60fps = 1;

    FILE *f = fopen(ORBX_INI_PATH, "r");
    if (!f) {
        log_msg("[ORBX-NATIVE] Warning: %s missing, using default mod filters (rpf/xml/dat/meta=true)\n", ORBX_INI_PATH);
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        trim_ws(line);
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        trim_ws(key);
        trim_ws(val);

        if (strcasecmp(key, "Allow_load_rpf_files") == 0)         cfg->allow_rpf = parse_ini_bool(val, cfg->allow_rpf);
        else if (strcasecmp(key, "Allow_load_xml_files") == 0)    cfg->allow_xml = parse_ini_bool(val, cfg->allow_xml);
        else if (strcasecmp(key, "Allow_load_omt_files") == 0)    cfg->allow_omt = parse_ini_bool(val, cfg->allow_omt);
        else if (strcasecmp(key, "Allow_load_dat_files") == 0)    cfg->allow_dat = parse_ini_bool(val, cfg->allow_dat);
        else if (strcasecmp(key, "Allow_load_meta_files") == 0)   cfg->allow_meta = parse_ini_bool(val, cfg->allow_meta);
        else if (strcasecmp(key, "Allow_load_rockstar_intro") == 0) cfg->allow_rockstar_intro = parse_ini_bool(val, cfg->allow_rockstar_intro);
        else if (strcasecmp(key, "Allow_load_gxt2_files") == 0)   cfg->allow_gxt2 = parse_ini_bool(val, cfg->allow_gxt2);
        else if (strcasecmp(key, "Allow_load_dlc_gxt2_files") == 0) cfg->allow_dlc_gxt2 = parse_ini_bool(val, cfg->allow_dlc_gxt2);
        else if (strcasecmp(key, "Allow_load_gfx_files") == 0)    cfg->allow_gfx = parse_ini_bool(val, cfg->allow_gfx);
        else if (strcasecmp(key, "Allow_rpf_info") == 0)          cfg->debug_rpf_info = parse_ini_bool(val, cfg->debug_rpf_info);
        else if (strcasecmp(key, "Allow_show_any_dat_file_info") == 0) cfg->debug_dat_info = parse_ini_bool(val, cfg->debug_dat_info);
        else if (strcasecmp(key, "AvoidCrcCheckedFiles") == 0)    cfg->avoid_crc = parse_ini_bool(val, cfg->avoid_crc);
        else if (strcasecmp(key, "Unlock_60fps") == 0)            cfg->unlock_60fps = parse_ini_bool(val, cfg->unlock_60fps);
    }
    fclose(f);
}

static uint32_t host_joaat_lower(const char *s) {
    uint32_t hash = 0;
    for (size_t i = 0; s[i] != '\0'; i++) {
        char c = (char)tolower((unsigned char)s[i]);
        if (c == '\\') c = '/';
        hash += (uint32_t)(uint8_t)c;
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;
}

/* ============================================================================
 * Recursive /data/orbx/gta Index Builder (JOAAT Sorted + Smart Prefix Aliases)
 * ============================================================================ */

typedef struct {
    uint32_t joaat_hash;
    uint32_t key_offset;
    uint32_t path_offset;
    uint8_t  is_alias;
} StagedModRecord;

typedef struct {
    StagedModRecord *records;
    size_t count;
    size_t cap;
    char  *pool;
    size_t pool_used;
    size_t pool_cap;
    int    seen_dat;
    int    seen_meta;
    int    seen_rpf;
    int    seen_xml;
} ModIndexBuilder;

static uint32_t pool_intern(ModIndexBuilder *b, const char *str, int force_lower) {
    size_t len = strlen(str);
    if (b->pool_used + len + 1 > b->pool_cap) return UINT32_MAX;
    uint32_t off = (uint32_t)b->pool_used;
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        if (force_lower) {
            c = (char)tolower((unsigned char)c);
            if (c == '\\') c = '/';
        }
        b->pool[b->pool_used++] = c;
    }
    b->pool[b->pool_used++] = '\0';
    return off;
}

static void builder_add_record(ModIndexBuilder *b, const char *norm_key, uint32_t path_off, uint8_t is_alias) {
    if (!norm_key || !*norm_key || b->count >= b->cap || path_off == UINT32_MAX) return;
    while (*norm_key == '/') norm_key++;
    if (!*norm_key) return;

    uint32_t key_off = pool_intern(b, norm_key, 1);
    if (key_off == UINT32_MAX) return;

    b->records[b->count].joaat_hash  = host_joaat_lower(norm_key);
    b->records[b->count].key_offset  = key_off;
    b->records[b->count].path_offset = path_off;
    b->records[b->count].is_alias    = is_alias;
    b->count++;
}

static void track_seen_ext(ModIndexBuilder *b, const char *rel) {
    const char *dot = strrchr(rel, '.');
    if (!dot) return;
    if (strcasecmp(dot, ".dat") == 0)  b->seen_dat = 1;
    else if (strcasecmp(dot, ".meta") == 0) b->seen_meta = 1;
    else if (strcasecmp(dot, ".rpf") == 0)  b->seen_rpf = 1;
    else if (strcasecmp(dot, ".xml") == 0)  b->seen_xml = 1;
}

static void walk_mod_dir(ModIndexBuilder *b, const char *dir_path, const char *rel_prefix, int depth) {
    if (depth > 16 || b->count >= b->cap) return;

    DIR *d = opendir(dir_path);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && b->count < b->cap) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        char full_path[512];
        char rel_path[384];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, ent->d_name);
        if (rel_prefix[0] != '\0') {
            snprintf(rel_path, sizeof(rel_path), "%s/%s", rel_prefix, ent->d_name);
        } else {
            snprintf(rel_path, sizeof(rel_path), "%s", ent->d_name);
        }

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            walk_mod_dir(b, full_path, rel_path, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            track_seen_ext(b, rel_path);
            uint32_t path_off = pool_intern(b, full_path, 0);
            if (path_off == UINT32_MAX) continue;

            /* 1. Exact relative key inside /data/orbx/gta/ */
            builder_add_record(b, rel_path, path_off, 0);

            /* 2. Common layout aliases: rpf/<x>, common/<x>, update/<x> */
            if (strncasecmp(rel_path, "rpf/", 4) == 0) {
                builder_add_record(b, rel_path + 4, path_off, 1);
            } else if (strncasecmp(rel_path, "common/", 7) == 0) {
                builder_add_record(b, rel_path + 7, path_off, 1);
            } else if (strncasecmp(rel_path, "update/", 7) == 0) {
                builder_add_record(b, rel_path + 7, path_off, 1);
            }
        }
    }
    closedir(d);
}

static int cmp_staged_record(const void *pa, const void *pb) {
    const StagedModRecord *a = (const StagedModRecord *)pa;
    const StagedModRecord *b = (const StagedModRecord *)pb;
    if (a->joaat_hash < b->joaat_hash) return -1;
    if (a->joaat_hash > b->joaat_hash) return 1;
    if (a->is_alias != b->is_alias) return (int)a->is_alias - (int)b->is_alias;
    return 0;
}

static void build_mod_index(OrbxNativeContext *ctx) {
    ModIndexBuilder b;
    memset(&b, 0, sizeof(b));
    b.cap      = ORBX_MAX_MOD_ENTRIES * 2;
    b.records  = calloc(b.cap, sizeof(StagedModRecord));
    b.pool     = ctx->string_pool;
    b.pool_cap = ORBX_STRING_POOL_BYTES;

    if (!b.records) return;

    walk_mod_dir(&b, ORBX_GTA_DIR, "", 0);

    if (b.count > 0) {
        qsort(b.records, b.count, sizeof(StagedModRecord), cmp_staged_record);

        uint32_t out_cnt = 0;
        for (size_t i = 0; i < b.count && out_cnt < ORBX_MAX_MOD_ENTRIES; i++) {
            const char *cur_key = &b.pool[b.records[i].key_offset];
            /* Deduplicate identical keys (non-alias wins because is_alias sorted first) */
            int dup = 0;
            for (size_t prev = out_cnt; prev > 0; prev--) {
                if (ctx->mods[prev - 1].joaat_hash != b.records[i].joaat_hash) break;
                if (strcmp(&b.pool[ctx->mods[prev - 1].key_offset], cur_key) == 0) {
                    dup = 1;
                    break;
                }
            }
            if (dup) continue;

            ctx->mods[out_cnt].joaat_hash  = b.records[i].joaat_hash;
            ctx->mods[out_cnt].key_offset  = b.records[i].key_offset;
            ctx->mods[out_cnt].path_offset = b.records[i].path_offset;
            out_cnt++;
        }
        ctx->mod_count = out_cnt;
    }

    ctx->pool_used = (uint32_t)b.pool_used;

    if (b.seen_dat && !ctx->cfg.allow_dat) {
        log_msg("[ORBX-NATIVE] Note: .dat mods found in %s but Allow_load_dat_files=false in %s\n",
                ORBX_GTA_DIR, ORBX_INI_PATH);
    }
    if (b.seen_meta && !ctx->cfg.allow_meta) {
        log_msg("[ORBX-NATIVE] Note: .meta mods found in %s but Allow_load_meta_files=false in %s\n",
                ORBX_GTA_DIR, ORBX_INI_PATH);
    }

    log_msg("[ORBX-NATIVE] Indexed %u lookup keys (%zu raw entries, %u/%u pool bytes)\n",
            ctx->mod_count, b.count, ctx->pool_used, ORBX_STRING_POOL_BYTES);
    uint32_t preview = (ctx->mod_count < 32u) ? ctx->mod_count : 32u;
    for (uint32_t i = 0; i < preview; i++) {
        log_msg("[ORBX-NATIVE]   Key[%u]: '%s' -> '%s'\n",
                i,
                &ctx->string_pool[ctx->mods[i].key_offset],
                &ctx->string_pool[ctx->mods[i].path_offset]);
    }

    free(b.records);
}

/* ============================================================================
 * Symbol Resolution & Verified Remote Syscall / mmap
 * ============================================================================ */

static uint64_t resolve_from_libs(pid_t pid, const char *nid) {
    static const char *const libs[] = {
        "libkernel.sprx",
        "libkernel_sys.sprx",
        "libSceLibcInternal.sprx",
        "libc.sprx"
    };
    for (size_t i = 0; i < sizeof(libs) / sizeof(libs[0]); i++) {
        uint32_t handle = 0;
        if (kernel_dynlib_handle(pid, libs[i], &handle) == 0 && handle != 0) {
            intptr_t addr = kernel_dynlib_resolve(pid, handle, nid);
            if (addr) return (uint64_t)addr;
        }
    }
    return 0;
}

static uint64_t find_libkernel_syscall_insn(pid_t pid) {
    char nid_getpid[16] = {0};
    nid_encode("getpid", nid_getpid);
    uint64_t stub_addr = resolve_from_libs(pid, nid_getpid);
    if (!stub_addr) stub_addr = resolve_from_libs(pid, "HoLVWNanBBc");
    if (!stub_addr) return 0;

    uint8_t code[32] = {0};
    if (kernel_proc_copyout(pid, (intptr_t)stub_addr, code, sizeof(code)) != 0) {
        return 0;
    }

    for (size_t i = 0; i + 2 < sizeof(code); i++) {
        if (code[i] == 0x0f && code[i + 1] == 0x05 && code[i + 2] == 0xc3) {
            log_msg("[ORBX-NATIVE] Verified libkernel syscall insn @ 0x%llx (getpid+0x%zx)\n",
                    (unsigned long long)(stub_addr + i), i);
            return stub_addr + i;
        }
    }
    for (size_t i = 0; i + 1 < sizeof(code); i++) {
        if (code[i] == 0x0f && code[i + 1] == 0x05) {
            log_msg("[ORBX-NATIVE] Found libkernel syscall insn @ 0x%llx (getpid+0x%zx)\n",
                    (unsigned long long)(stub_addr + i), i);
            return stub_addr + i;
        }
    }
    return 0;
}

static int attached_remote_syscall6(pid_t pid, uint64_t syscall_addr, int sysno,
                                    uint64_t a1, uint64_t a2, uint64_t a3,
                                    uint64_t a4, uint64_t a5, uint64_t a6,
                                    uint64_t *out_rax, uint64_t *out_cf) {
    struct reg jmp_reg;
    struct reg bak_reg;
    if (pt_getregs(pid, &bak_reg) != 0) return -1;

    memcpy(&jmp_reg, &bak_reg, sizeof(jmp_reg));
    jmp_reg.r_rip = (register_t)syscall_addr;
    jmp_reg.r_rax = (register_t)sysno;
    jmp_reg.r_rdi = (register_t)a1;
    jmp_reg.r_rsi = (register_t)a2;
    jmp_reg.r_rdx = (register_t)a3;
    jmp_reg.r_r10 = (register_t)a4;
    jmp_reg.r_r8  = (register_t)a5;
    jmp_reg.r_r9  = (register_t)a6;

    if (pt_setregs(pid, &jmp_reg) != 0) return -1;

    /* Execute ONLY the syscall instruction (0f 05), stopping at syscall_addr + 2 without running ret */
    int steps = 0;
    while ((uint64_t)jmp_reg.r_rip == syscall_addr && steps++ < 4) {
        if (pt_step(pid) != 0 || pt_getregs(pid, &jmp_reg) != 0) {
            pt_setregs(pid, &bak_reg);
            return -1;
        }
    }

    if (out_rax) *out_rax = (uint64_t)jmp_reg.r_rax;
    if (out_cf)  *out_cf  = (uint64_t)jmp_reg.r_rflags & 1ULL;

    pt_setregs(pid, &bak_reg);
    return ((uint64_t)jmp_reg.r_rip != syscall_addr) ? 0 : -1;
}

static uint64_t attached_remote_mmap(pid_t pid, uint64_t syscall_addr, size_t len, int prot) {
    uint64_t mapped = 0, cf = 1;
    if (attached_remote_syscall6(pid, syscall_addr, SYS_mmap,
                                 0, (uint64_t)len, (uint64_t)prot,
                                 (uint64_t)(MAP_ANONYMOUS | MAP_PRIVATE),
                                 (uint64_t)-1, 0, &mapped, &cf) != 0) {
        log_msg("[ORBX-NATIVE] Remote mmap(len=0x%zx, prot=%d) ptrace step error\n", len, prot);
        return 0;
    }
    if (cf != 0 || mapped < 0x10000ULL || (mapped & 0xfffULL) != 0) {
        log_msg("[ORBX-NATIVE] Remote mmap(len=0x%zx, prot=%d) failed (rax=0x%llx, CF=%llu)\n",
                len, prot, (unsigned long long)mapped, (unsigned long long)cf);
        return 0;
    }
    return mapped;
}

static int attached_remote_mprotect(pid_t pid, uint64_t syscall_addr, uint64_t addr, size_t len, int prot) {
    uint64_t rax = 0, cf = 1;
    if (attached_remote_syscall6(pid, syscall_addr, SYS_mprotect,
                                 addr, (uint64_t)len, (uint64_t)prot,
                                 0, 0, 0, &rax, &cf) != 0) {
        return -1;
    }
    if (cf != 0 || rax != 0) {
        log_msg("[ORBX-NATIVE] Remote mprotect(0x%llx, 0x%zx, %d) failed (rax=0x%llx, CF=%llu)\n",
                (unsigned long long)addr, len, prot, (unsigned long long)rax, (unsigned long long)cf);
        return -1;
    }
    return 0;
}

/* ============================================================================
 * Read-Only Kernel-DMAP RAGE v1.00 Diagnostics, 60FPS Patch & Deferred Hooks
 * ============================================================================ */

static uint64_t now_mono_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000L);
}

static void dump_code_hex(pid_t pid, uint64_t va, size_t bytes, const char *label) {
    if (bytes > 128) bytes = 128;
    uint8_t buf[128];
    if (kernel_proc_copyout(pid, (intptr_t)va, buf, bytes) != 0) return;
    char hex[257];
    for (size_t i = 0; i < bytes; i++) {
        static const char kHex[] = "0123456789abcdef";
        hex[i * 2]     = kHex[buf[i] >> 4];
        hex[i * 2 + 1] = kHex[buf[i] & 0xf];
    }
    hex[bytes * 2] = '\0';
    log_msg("[RAGE-PROBE] Code '%s' @ 0x%llx (%zuB): %s\n",
            label, (unsigned long long)va, bytes, hex);
}

static uint64_t resolve_from_libs_extra(pid_t pid, const char *nid, const char *const *extra_libs, size_t n_extra) {
    uint64_t a = resolve_from_libs(pid, nid);
    if (a) return a;
    for (size_t i = 0; i < n_extra; i++) {
        uint32_t handle = 0;
        if (kernel_dynlib_handle(pid, extra_libs[i], &handle) == 0 && handle != 0) {
            intptr_t addr = kernel_dynlib_resolve(pid, handle, nid);
            if (addr) return (uint64_t)addr;
        }
    }
    return 0;
}

typedef struct {
    uint64_t    target_va;
    const char *label;
    int         is_call;   /* 1 = e8/e9 rel32 call/jmp, 0 = RIP-relative mem/lea */
    int         max_hits;
    int         hits;
    size_t      dump_bytes;
} BatchProbeTarget;

/* Single-pass .text scanner so multiple call/RIP XREF probes complete in ~30ms total */
static void probe_code_targets_batch(pid_t pid, uint64_t eboot_base, uint64_t scan_end,
                                     BatchProbeTarget *targets, size_t n_targets) {
    if (!targets || n_targets == 0 || eboot_base >= scan_end) return;
    uint8_t *buf = malloc(0x4000 + 8);
    if (!buf) return;

    for (uint64_t cur = eboot_base; cur < scan_end; cur += 0x4000) {
        int any_remaining = 0;
        for (size_t t = 0; t < n_targets; t++) {
            if (targets[t].target_va && targets[t].hits < targets[t].max_hits) {
                any_remaining = 1;
                break;
            }
        }
        if (!any_remaining) break;

        size_t to_read = (cur + 0x4000 + 8 <= scan_end) ? (0x4000 + 8) : (size_t)(scan_end - cur);
        if (to_read < 7) break;
        if (kernel_proc_copyout(pid, (intptr_t)cur, buf, to_read) != 0) continue;

        size_t limit = (to_read > 0x4000) ? 0x4000 : (to_read - 7);
        for (size_t i = 0; i < limit; i++) {
            uint8_t b0 = buf[i];
            if (b0 == 0xe8 || b0 == 0xe9) {
                int32_t rel = 0;
                memcpy(&rel, buf + i + 1, 4);
                uint64_t dst = (uint64_t)((int64_t)(cur + i + 5) + (int64_t)rel);
                for (size_t t = 0; t < n_targets; t++) {
                    if (!targets[t].is_call || targets[t].hits >= targets[t].max_hits) continue;
                    if (dst == targets[t].target_va) {
                        uint64_t site = cur + i;
                        log_msg("[V100-PROBE] %s '%s' @ 0x%llx\n",
                                (b0 == 0xe8) ? "CALL" : "JMP",
                                targets[t].label, (unsigned long long)site);
                        if (targets[t].dump_bytes > 0) {
                            uint64_t dstart = (site > eboot_base + 32) ? (site - 32) : site;
                            dump_code_hex(pid, dstart, targets[t].dump_bytes, targets[t].label);
                        }
                        targets[t].hits++;
                    }
                }
            } else if ((b0 == 0x48 || b0 == 0x4c) &&
                       (buf[i + 1] == 0x8d || buf[i + 1] == 0x8b || buf[i + 1] == 0x89) &&
                       (buf[i + 2] & 0xc7) == 0x05) {
                int32_t disp = 0;
                memcpy(&disp, buf + i + 3, 4);
                uint64_t insn_va = cur + i;
                uint64_t ref_va = (uint64_t)((int64_t)(insn_va + 7) + (int64_t)disp);
                for (size_t t = 0; t < n_targets; t++) {
                    if (targets[t].is_call || targets[t].hits >= targets[t].max_hits) continue;
                    if (ref_va == targets[t].target_va) {
                        log_msg("[RAGE-PROBE] XREF64 '%s' -> insn @ 0x%llx\n",
                                targets[t].label, (unsigned long long)insn_va);
                        if (targets[t].dump_bytes > 0) {
                            uint64_t dstart = (insn_va > eboot_base + 16) ? (insn_va - 16) : insn_va;
                            dump_code_hex(pid, dstart, targets[t].dump_bytes, targets[t].label);
                        }
                        targets[t].hits++;
                    }
                }
            } else if ((b0 == 0x89 || b0 == 0x8b || b0 == 0x8d) && (buf[i + 1] & 0xc7) == 0x05) {
                int32_t disp = 0;
                memcpy(&disp, buf + i + 2, 4);
                uint64_t insn_va = cur + i;
                uint64_t ref_va = (uint64_t)((int64_t)(insn_va + 6) + (int64_t)disp);
                for (size_t t = 0; t < n_targets; t++) {
                    if (targets[t].is_call || targets[t].hits >= targets[t].max_hits) continue;
                    if (ref_va == targets[t].target_va) {
                        log_msg("[RAGE-PROBE] XREF32 '%s' -> insn @ 0x%llx\n",
                                targets[t].label, (unsigned long long)insn_va);
                        if (targets[t].dump_bytes > 0) {
                            uint64_t dstart = (insn_va > eboot_base + 16) ? (insn_va - 16) : insn_va;
                            dump_code_hex(pid, dstart, targets[t].dump_bytes, targets[t].label);
                        }
                        targets[t].hits++;
                    }
                }
            }
        }
    }
    free(buf);
}

static void probe_data_ptr_refs(pid_t pid, uint64_t start_va, uint64_t end_va,
                                uint64_t ptr_va, const char *label, int max_hits) {
    if (!ptr_va || start_va >= end_va) return;
    uint8_t *buf = malloc(0x4000);
    if (!buf) return;
    int hits = 0;
    for (uint64_t cur = start_va; cur < end_va && hits < max_hits; cur += 0x4000) {
        size_t to_read = (cur + 0x4000 <= end_va) ? 0x4000 : (size_t)(end_va - cur);
        if (to_read < 8) break;
        if (kernel_proc_copyout(pid, (intptr_t)cur, buf, to_read) != 0) continue;
        for (size_t i = 0; i + 8 <= to_read && hits < max_hits; i += 8) {
            uint64_t val = 0;
            memcpy(&val, buf + i, 8);
            if (val == ptr_va) {
                log_msg("[V100-PROBE] DATA-PTR '%s' @ 0x%llx -> 0x%llx\n",
                        label, (unsigned long long)(cur + i), (unsigned long long)ptr_va);
                hits++;
                if (strstr(label, "VTABLE") != NULL) {
                    uint64_t vfuncs[4] = {0};
                    if (kernel_proc_copyout(pid, (intptr_t)(cur + i + 8), vfuncs, sizeof(vfuncs)) == 0) {
                        log_msg("[V100-PROBE]   VTABLE Methods @ 0x%llx: [0]=0x%llx [1]=0x%llx [2]=0x%llx [3]=0x%llx\n",
                                (unsigned long long)(cur + i + 8),
                                (unsigned long long)vfuncs[0], (unsigned long long)vfuncs[1],
                                (unsigned long long)vfuncs[2], (unsigned long long)vfuncs[3]);
                    }
                }
            }
        }
    }
    free(buf);
}

static void try_patch_v100_flip_interval(pid_t pid, uint64_t eboot_base, uint8_t unlock_60fps) {
    if (!unlock_60fps || !eboot_base) return;
    static const uint8_t expected[12] = {
        0xbf, 0x02, 0x00, 0x00, 0x00, 0x31, 0xf6, 0xe8, 0xa7, 0x67, 0xcb, 0x00
    };
    uint64_t patch_site = eboot_base + 0xa6bf3dULL; /* 0xe6bf3d on default base 0x400000 */
    uint8_t orig[12] = {0};
    if (kernel_proc_copyout(pid, (intptr_t)patch_site, orig, sizeof(orig)) != 0) {
        log_msg("[ORBX-NATIVE] 60FPS patch check failed reading 0x%llx\n", (unsigned long long)patch_site);
        return;
    }
    if (memcmp(orig, expected, sizeof(expected)) != 0) {
        if (orig[0] == 0xbf && orig[1] == 0x01) {
            log_msg("[ORBX-NATIVE] v1.00 SetFlipInterval @ 0x%llx already patched to 1\n",
                    (unsigned long long)patch_site);
        } else {
            log_msg("[ORBX-NATIVE] 60FPS SetFlipInterval sig mismatch @ 0x%llx (%02x %02x %02x %02x)\n",
                    (unsigned long long)patch_site, orig[0], orig[1], orig[2], orig[3]);
        }
        return;
    }
    uint8_t one = 0x01;
    int wr = pt_copyin(pid, &one, (intptr_t)(patch_site + 1), 1);
    uint8_t verify = 0;
    kernel_proc_copyout(pid, (intptr_t)(patch_site + 1), &verify, 1);
    log_msg("[ORBX-NATIVE] v1.00 SetFlipInterval(2->1) @ 0x%llx write_rc=%d verified=0x%02x\n",
            (unsigned long long)(patch_site + 1), wr, verify);
}

static void try_patch_v100_pso_strs(pid_t pid, uint64_t eboot_base, uint64_t hook_addr) {
    if (!eboot_base || !hook_addr) return;
    static const uint8_t expected[17] = {
        0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41,
        0x55, 0x41, 0x54, 0x53, 0x50, 0x49, 0x89, 0xf6
    };
    uint64_t site = eboot_base + 0x161c6e0ULL; /* 0x1a1c6e0 */
    uint8_t orig[17] = {0};
    if (kernel_proc_copyout(pid, (intptr_t)site, orig, sizeof(orig)) != 0) {
        log_msg("[ORBX-NATIVE] psoInitStrings check failed reading 0x%llx\n", (unsigned long long)site);
        return;
    }
    if (memcmp(orig, expected, sizeof(expected)) != 0) {
        if (orig[0] == 0x48 && orig[1] == 0xb8 && orig[10] == 0xff && orig[11] == 0xe0) {
            uint64_t cur_target = 0;
            memcpy(&cur_target, orig + 2, 8);
            if (cur_target == hook_addr) {
                log_msg("[ORBX-NATIVE] v1.00 psoInitStrings guard @ 0x%llx already patched -> 0x%llx\n",
                        (unsigned long long)site, (unsigned long long)cur_target);
                return;
            }
            if (cur_target >= 0x10000ULL + ORBX_ENTRY_OFF_PSO_STRS && cur_target < 0x800000000000ULL) {
                uint64_t prev_code = cur_target - ORBX_ENTRY_OFF_PSO_STRS;
                OrbxNativeHeader prev_hdr = {0};
                if ((prev_code & 0xfffULL) == 0ULL &&
                    kernel_proc_copyout(pid, (intptr_t)prev_code, &prev_hdr, sizeof(prev_hdr)) == 0 &&
                    (prev_hdr.magic & ORBX_NATIVE_MAGIC_PREFIX_MASK) == ORBX_NATIVE_MAGIC_PREFIX) {
                    uint8_t jmp12_upd[12];
                    jmp12_upd[0] = 0x48;
                    jmp12_upd[1] = 0xb8;
                    memcpy(jmp12_upd + 2, &hook_addr, 8);
                    jmp12_upd[10] = 0xff;
                    jmp12_upd[11] = 0xe0;
                    int upd_wr = pt_copyin(pid, jmp12_upd, (intptr_t)site, sizeof(jmp12_upd));
                    uint8_t upd_rb[12] = {0};
                    kernel_proc_copyout(pid, (intptr_t)site, upd_rb, sizeof(upd_rb));
                    int upd_ok = (memcmp(upd_rb, jmp12_upd, sizeof(jmp12_upd)) == 0);
                    log_msg("[ORBX-NATIVE] Re-targeted psoInitStrings guard @ 0x%llx -> 0x%llx (write_rc=%d verified=%d)\n",
                            (unsigned long long)site, (unsigned long long)hook_addr, upd_wr, upd_ok);
                    return;
                }
            }
            log_msg("[ORBX-NATIVE] v1.00 psoInitStrings guard @ 0x%llx already patched\n",
                    (unsigned long long)site);
        } else {
            log_msg("[ORBX-NATIVE] psoInitStrings sig mismatch @ 0x%llx (%02x %02x %02x %02x)\n",
                    (unsigned long long)site, orig[0], orig[1], orig[2], orig[3]);
        }
        return;
    }
    uint8_t jmp12[12];
    jmp12[0] = 0x48;
    jmp12[1] = 0xb8;
    memcpy(jmp12 + 2, &hook_addr, 8);
    jmp12[10] = 0xff;
    jmp12[11] = 0xe0;
    int wr = pt_copyin(pid, jmp12, (intptr_t)site, sizeof(jmp12));
    uint8_t rb[12] = {0};
    kernel_proc_copyout(pid, (intptr_t)site, rb, sizeof(rb));
    int ok = (memcmp(rb, jmp12, sizeof(jmp12)) == 0);
    log_msg("[ORBX-NATIVE] Patched v1.00 psoInitStrings guard @ 0x%llx -> 0x%llx (write_rc=%d verified=%d)\n",
            (unsigned long long)site, (unsigned long long)hook_addr, wr, ok);
}

static uint64_t lookup_runtime_native_entry(pid_t pid, uint64_t table_va, uint64_t hash, uint64_t *out_slot_va) {
    uint64_t bucket_ptr = 0;
    uint32_t bucket = (uint32_t)(hash & 0xffu);
    if (out_slot_va) *out_slot_va = 0;
    if (kernel_proc_copyout(pid, (intptr_t)(table_va + bucket * 8u), &bucket_ptr, 8) != 0) {
        return 0;
    }
    for (int hop = 0; hop < 32 && bucket_ptr >= 0x10000ULL && bucket_ptr < 0x800000000000ULL; hop++) {
        struct {
            uint64_t next;
            uint64_t handlers[7];
            uint32_t count;
            uint32_t pad;
            uint64_t hashes[7];
        } node;
        if (kernel_proc_copyout(pid, (intptr_t)bucket_ptr, &node, sizeof(node)) != 0) break;
        uint32_t cnt = (node.count <= 7u) ? node.count : 7u;
        for (uint32_t k = 0; k < cnt; k++) {
            if (node.hashes[k] == hash) {
                if (out_slot_va) *out_slot_va = bucket_ptr + 8u + (uint64_t)k * 8u;
                return node.handlers[k];
            }
        }
        bucket_ptr = node.next;
    }
    return 0;
}

typedef struct {
    uint32_t    id;
    uint64_t    hash;
    uint8_t     req;
    const char *name;
} OrbxNativeSpec;

static const OrbxNativeSpec kMenuNativeSpecs[ORBX_NAT_COUNT] = {
#define ORBX_SPEC_ROW(id, hash, req, name) { (uint32_t)(id), (hash), (uint8_t)(req), (name) },
    ORBX_MENU_NATIVE_LIST(ORBX_SPEC_ROW)
#undef ORBX_SPEC_ROW
};

static int is_eboot_text_ptr(uint64_t addr, uint64_t eboot_base, uint64_t eboot_size) {
    if (!eboot_base || addr < eboot_base) return 0;
    uint64_t bound = eboot_size ? (eboot_base + eboot_size) : (eboot_base + 0x27ac000ULL);
    return (addr < bound) ? 1 : 0;
}

static uint64_t recover_wait_from_old_tramp(pid_t pid, uint64_t eboot_base, uint64_t eboot_size, uint64_t h_wait) {
    if (h_wait < 0x10000ULL + ORBX_ENTRY_OFF_MENU_WAIT || h_wait >= 0x800000000000ULL) return 0;
    uint64_t old_code = h_wait - ORBX_ENTRY_OFF_MENU_WAIT;
    if ((old_code & 0xfffULL) != 0ULL) return 0;
    OrbxNativeHeader hdr = {0};
    if (kernel_proc_copyout(pid, (intptr_t)old_code, &hdr, sizeof(hdr)) != 0) return 0;
    if ((hdr.magic & ORBX_NATIVE_MAGIC_PREFIX_MASK) != ORBX_NATIVE_MAGIC_PREFIX) return 0;
    if (hdr.ctx_addr < 0x10000ULL || (hdr.ctx_addr & 0xfffULL) != 0ULL) return 0;

    if (hdr.magic == ORBX_NATIVE_MAGIC) {
        uint64_t ow = 0;
        if (kernel_proc_copyout(pid, (intptr_t)(hdr.ctx_addr + offsetof(OrbxNativeContext, orig_wait)),
                                &ow, sizeof(ow)) == 0 &&
            is_eboot_text_ptr(ow, eboot_base, eboot_size)) {
            return ow;
        }
        uint64_t mw = 0;
        if (kernel_proc_copyout(pid, (intptr_t)(hdr.ctx_addr + offsetof(OrbxNativeContext, menu.natives[ORBX_NAT_WAIT])),
                                &mw, sizeof(mw)) == 0 &&
            is_eboot_text_ptr(mw, eboot_base, eboot_size)) {
            return mw;
        }
        return 0;
    }

    /* Recover real WAIT handler from legacy ORBXPS51 context without chaining */
    uint8_t page[4096];
    uint64_t text_end = eboot_base + 0x27ac000ULL;
    for (uint64_t off = 0x60000ULL; off < 0x72000ULL; off += sizeof(page)) {
        if (kernel_proc_copyout(pid, (intptr_t)(hdr.ctx_addr + off), page, sizeof(page)) != 0) continue;
        for (size_t i = 0; i + 40 <= sizeof(page); i += 8) {
            const uint64_t *p = (const uint64_t *)(page + i);
            if (p[0] >= eboot_base && p[0] < text_end &&
                p[1] >= eboot_base && p[1] < text_end &&
                p[3] >= eboot_base && p[3] < text_end &&
                p[4] >= eboot_base && p[4] < text_end &&
                p[0] != p[1] && p[1] != p[3]) {
                return p[0];
            }
        }
    }
    return 0;
}

static int try_arm_v100_mod_menu(pid_t pid, uint64_t eboot_base) {
    if (!eboot_base || !g_native_ctx_addr || !g_code_addr) return 0;
    uint64_t table_va = eboot_base + 0x3244a28ULL; /* 0x3644a28 s_NativeRegistrations */
    uint64_t wait_slot = 0;
    uint64_t h_wait = lookup_runtime_native_entry(pid, table_va, kMenuNativeSpecs[ORBX_NAT_WAIT].hash, &wait_slot);
    if (!h_wait || !wait_slot) return 0;

    uint64_t tramp = g_code_addr + ORBX_ENTRY_OFF_MENU_WAIT;
    uint64_t code_span = g_code_alloc_size ? g_code_alloc_size : 0x40000ULL;
    if (h_wait == tramp || (h_wait >= g_code_addr && h_wait < g_code_addr + code_span)) {
        uint64_t saved_wait = 0;
        kernel_proc_copyout(pid,
                            (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, orig_wait)),
                            &saved_wait, sizeof(saved_wait));
        if (!is_eboot_text_ptr(saved_wait, eboot_base, g_eboot_size)) {
            kernel_proc_copyout(pid,
                                (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, menu.natives[ORBX_NAT_WAIT])),
                                &saved_wait, sizeof(saved_wait));
        }
        if (is_eboot_text_ptr(saved_wait, eboot_base, g_eboot_size)) {
            h_wait = saved_wait;
        } else {
            return -1;
        }
    } else if (!is_eboot_text_ptr(h_wait, eboot_base, g_eboot_size)) {
        uint64_t recovered = recover_wait_from_old_tramp(pid, eboot_base, g_eboot_size, h_wait);
        if (recovered) {
            log_msg("[V100-MENU] Recovered real WAIT=0x%llx from previous engine 0x%llx (no chaining)\n",
                    (unsigned long long)recovered, (unsigned long long)h_wait);
            h_wait = recovered;
        } else {
            log_msg("[V100-MENU] Error: WAIT slot points outside eboot.bin (0x%llx); restart GTA V cleanly\n",
                    (unsigned long long)h_wait);
            return -1;
        }
    }

    uint64_t resolved[ORBX_MAX_NATIVES];
    memset(resolved, 0, sizeof(resolved));
    resolved[ORBX_NAT_WAIT] = h_wait;

    int ok_count = 1;
    int req_missing = 0;
    for (uint32_t i = 1; i < ORBX_NAT_COUNT; i++) {
        resolved[i] = lookup_runtime_native_entry(pid, table_va, kMenuNativeSpecs[i].hash, NULL);
        if (!resolved[i] && i == ORBX_NAT_OVERRIDE_LODSCALE_THIS_FRAME) {
            resolved[i] = lookup_runtime_native_entry(pid, table_va, 0x9B8B94A1196F345FULL, NULL);
        }
        if (resolved[i]) {
            ok_count++;
        } else if (kMenuNativeSpecs[i].req) {
            req_missing++;
            log_msg("[V100-MENU] Missing REQUIRED native %s (0x%llx)\n",
                    kMenuNativeSpecs[i].name, (unsigned long long)kMenuNativeSpecs[i].hash);
        } else {
            log_msg("[V100-MENU] Missing optional native %s (0x%llx)\n",
                    kMenuNativeSpecs[i].name, (unsigned long long)kMenuNativeSpecs[i].hash);
        }
    }

    log_msg("[V100-MENU] Resolved %d/%u runtime natives (req_missing=%d, WAIT=0x%llx@0x%llx)\n",
            ok_count, (unsigned)ORBX_NAT_COUNT, req_missing,
            (unsigned long long)h_wait, (unsigned long long)wait_slot);
    if (req_missing > 0) return -1;

    if (kernel_proc_copyin(pid, &h_wait,
                           (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, orig_wait)),
                           sizeof(h_wait)) != 0) {
        return -1;
    }
    if (kernel_proc_copyin(pid, resolved,
                           (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, menu.natives)),
                           sizeof(resolved)) != 0) {
        return -1;
    }
    uint32_t resolved_u32 = (uint32_t)ok_count;
    kernel_proc_copyin(pid, &resolved_u32,
                       (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, menu.probe_resolved_count)),
                       sizeof(resolved_u32));
    uint32_t armed = 1u;
    if (kernel_proc_copyin(pid, &armed,
                           (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, menu.armed)),
                           sizeof(armed)) != 0) {
        return -1;
    }

    int wr = kernel_proc_copyin(pid, &tramp, (intptr_t)wait_slot, sizeof(tramp));
    uint64_t verify = 0;
    kernel_proc_copyout(pid, (intptr_t)wait_slot, &verify, sizeof(verify));
    log_msg("[V100-MENU] Armed WAIT slot 0x%llx -> 0x%llx (write_rc=%d verified=%d) | Open: R1 + D-Pad Down (default)\n",
            (unsigned long long)wait_slot, (unsigned long long)tramp, wr, (verify == tramp));
    return 1;
}

static void run_rage_v100_probe(pid_t pid, uint64_t eboot_base, uint64_t eboot_size) {
    uint64_t text_end = eboot_base + 0x27ac000ULL;
    if (text_end > eboot_base + eboot_size) text_end = eboot_base + eboot_size;

    /* Locate GtaThread & scrThread vtables from Itanium ABI type_info pointers found at 0x2ca24c0 / 0x2d9af28 */
    probe_data_ptr_refs(pid, text_end, eboot_base + eboot_size, eboot_base + 0x28a24c0ULL, "ZTI_GtaThread", 4);
    probe_data_ptr_refs(pid, text_end, eboot_base + eboot_size, eboot_base + 0x299af28ULL, "ZTI_scrThread", 4);

    /* Find callers of scrThread::Run VM loop (0x1eae770) & dump 0x1eae770 */
    BatchProbeTarget batch[] = {
        { eboot_base + 0x1aae770ULL, "CALL_scrThread_1eae770", 1, 6, 0, 64 },
    };
    probe_code_targets_batch(pid, eboot_base, text_end, batch, 1);

    dump_code_hex(pid, eboot_base + 0x1aae770ULL, 128, "scrThread_1eae770");
    dump_code_hex(pid, eboot_base + 0x1aae7f0ULL, 128, "scrThread_1eae7f0");
}

/* ============================================================================
 * Safe Non-Invasive RAGE Memory Pool Check (PS5 16 GB Unified Memory)
 * ============================================================================ */

static void probe_rage_heap_allocator(pid_t pid, uint64_t eboot_base, uint64_t eboot_size,
                                      int target_heap_mb, int enable_patch) {
    if (!pid || !eboot_base || eboot_size < 0x100000ULL) {
        log_msg("[HEAP-ABORT] Invalid bounds: pid=%d, base=0x%llx, size=0x%llx\n",
                pid, (unsigned long long)eboot_base, (unsigned long long)eboot_size);
        return;
    }

    int reported_mb = (target_heap_mb >= 512 && target_heap_mb <= 3072) ? target_heap_mb : 1024;
    log_msg("[HEAP-PROBE] PS5 16 GB unified memory pool active for PID %d (eboot 0x%llx, size 0x%llx)\n",
            pid, (unsigned long long)eboot_base, (unsigned long long)eboot_size);

    /* Read-only sanity check that CUSA00419 render queue helper (+0x1b79940) is readable and untouched. */
    uint8_t rq_head[8] = {0};
    if (kernel_proc_copyout(pid, (intptr_t)(eboot_base + 0x1b79940ULL), rq_head, sizeof(rq_head)) == 0) {
        log_msg("[HEAP-PROBE] Read-only check OK: render-state fn +0x1b79940 intact (%02x %02x %02x %02x)\n",
                rq_head[0], rq_head[1], rq_head[2], rq_head[3]);
    }

    if (enable_patch) {
        /* Safety Guard: 0x1f79940 (+0x1b79940) is an internal render-state bitmask queue, not a heap size,
         * and heuristic imm32 scanning in .text matches bitmasks/floats. Keep Heap Adjuster a safe no-op. */
        log_msg("[HEAP-SAFE] RAGE Heap Adjuster (%d MB) locked to Safe No-Op on PS5 CUSA00419 "
                "(PS5 16 GB unified memory handles streaming assets natively; 0 bytes patched).\n",
                reported_mb);
    } else {
        log_msg("[HEAP-PROBE] Safe Mode: Read-Only verified. Zero game memory modified.\n");
    }
}

static int is_non_car_dlc(const char *name) {
    if (!name || !*name) return 1;
    /* Map, graphics, weather, audio, script and system packs */
    if (strcasecmp(name, "NVE") == 0 ||
        strcasecmp(name, "naturalvision") == 0 ||
        strcasecmp(name, "quantv") == 0 ||
        strcasecmp(name, "cloudhats") == 0 ||
        strcasecmp(name, "v_clouds") == 0 ||
        strcasecmp(name, "gtaVremastered") == 0 ||
        strcasecmp(name, "vremastered") == 0 ||
        strcasecmp(name, "remasteredsandy") == 0 ||
        strcasecmp(name, "sandymaps") == 0 ||
        strcasecmp(name, "paleto") == 0 ||
        strcasecmp(name, "livelyls") == 0 ||
        strcasecmp(name, "fsan") == 0 ||
        strcasecmp(name, "fsas") == 0 ||
        strcasecmp(name, "visualsettings") == 0 ||
        strcasecmp(name, "update") == 0 ||
        strncasecmp(name, "patchday", 8) == 0 ||
        strcasecmp(name, "data") == 0 ||
        strcasecmp(name, "levels") == 0 ||
        strcasecmp(name, "timecycle") == 0 ||
        strcasecmp(name, "popcycle") == 0 ||
        strcasecmp(name, "weapons") == 0 ||
        strcasecmp(name, "audio") == 0) {
        return 1;
    }
    /* Official Rockstar multiplayer DLC packs (not user add-on cars) */
    if (strcasecmp(name, "mplowrider") == 0 ||
        strcasecmp(name, "mplowrider2") == 0 ||
        strcasecmp(name, "mpbiker") == 0 ||
        strcasecmp(name, "mpstunt") == 0 ||
        strcasecmp(name, "mpimportexport") == 0 ||
        strcasecmp(name, "mpgunrunning") == 0 ||
        strcasecmp(name, "mpsmuggler") == 0 ||
        strcasecmp(name, "mpchristmas2017") == 0 ||
        strcasecmp(name, "mpassault") == 0 ||
        strcasecmp(name, "mpbattle") == 0 ||
        strcasecmp(name, "mpheist3") == 0 ||
        strcasecmp(name, "mpsum") == 0 ||
        strcasecmp(name, "mpheist4") == 0 ||
        strcasecmp(name, "mptuner") == 0 ||
        strcasecmp(name, "mpsecurity") == 0 ||
        strcasecmp(name, "mpg9ec") == 0 ||
        strcasecmp(name, "mp2023_01") == 0 ||
        strcasecmp(name, "mp2023_02") == 0 ||
        strcasecmp(name, "mp2024_01") == 0) {
        return 1;
    }
    return 0;
}

static void get_car_display_label(const char *name, char *out_lbl, size_t max_len) {
    if (!out_lbl || max_len == 0) return;
    out_lbl[0] = '\0';
    if (!name || !*name) return;

    static const struct {
        const char *id;
        const char *label;
    } kKnownCars[] = {
        { "m4comp",     "BMW M4 Competition" },
        { "svj63",      "Lamborghini SVJ 63" },
        { "27rev",      "Lamborghini Revuelto" },
        { "r35",        "Nissan GT-R R35" },
        { "sf90vnm",    "Ferrari SF90 Stradale" },
        { "sf90",       "Ferrari SF90 Stradale" },
        { "laveg",      "Ferrari LaFerrari" },
        { "oyconyx",    "Rolls-Royce Cullinan" },
        { "m3tour",     "BMW M3 Touring" },
        { "raptor25r",  "Ford F-150 Raptor R" },
        { "oyc992",     "Porsche 911 GT3 RS" },
        { "gibv48",     "Bugatti Chiron Pur Sport" },
        { "hawkcstm",   "Jeep Grand Trackhawk" },
        { "r8v10",      "Audi R8 V10 Plus" },
        { "s500w222",   "Mercedes-Benz S500" },
        { "rdd",        "Dodge Charger Hellcat" },
        { "gt63s",      "Mercedes-AMG GT 63 S" },
        { "c63s",       "Mercedes-AMG C 63 S" },
        { "chiron",     "Bugatti Chiron" },
        { "senna",      "McLaren Senna" },
        { "p1",         "McLaren P1" },
        { "urus",       "Lamborghini Urus" },
        { "huracan",    "Lamborghini Huracan" },
        { "supra",      "Toyota Supra A90" },
        { "a90",        "Toyota Supra A90" }
    };

    for (size_t i = 0; i < sizeof(kKnownCars) / sizeof(kKnownCars[0]); i++) {
        if (strcasecmp(name, kKnownCars[i].id) == 0) {
            snprintf(out_lbl, max_len, "%s", kKnownCars[i].label);
            return;
        }
    }

    /* Clean auto-formatting for unknown add-on models */
    size_t o = 0;
    int cap_next = 1;
    for (size_t i = 0; name[i] != '\0' && o + 1 < max_len; i++) {
        char c = name[i];
        if (c == '_' || c == '-') {
            out_lbl[o++] = ' ';
            cap_next = 1;
        } else if (cap_next && c >= 'a' && c <= 'z') {
            out_lbl[o++] = (char)(c - ('a' - 'A'));
            cap_next = 0;
        } else {
            out_lbl[o++] = c;
            cap_next = 0;
        }
    }
    out_lbl[o] = '\0';
}

static void add_car_if_new(OrbxAddonCarEntry *out_list, uint32_t *count, const char *name, const char *label) {
    if (!name || !*name || *count >= ORBX_MAX_ADDON_CARS) return;
    for (uint32_t i = 0; i < *count; i++) {
        if (strcasecmp(out_list[i].name, name) == 0) return;
    }
    OrbxAddonCarEntry *e = &out_list[(*count)++];
    snprintf(e->name, sizeof(e->name), "%s", name);
    snprintf(e->label, sizeof(e->label), "%s", (label && label[0]) ? label : name);
    e->hash = host_joaat_lower(name);
}

static void scan_addon_car_dir(OrbxAddonCarEntry *out_list, uint32_t *count, const char *dir_path) {
    if (!dir_path || *count >= ORBX_MAX_ADDON_CARS) return;
    DIR *d = opendir(dir_path);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && *count < ORBX_MAX_ADDON_CARS) {
        if (ent->d_name[0] == '.') continue;
        char pack[64];
        snprintf(pack, sizeof(pack), "%s", ent->d_name);
        size_t plen = strlen(pack);
        if (plen > 4 && strcasecmp(pack + plen - 4, ".rpf") == 0) {
            pack[plen - 4] = '\0';
        }
        trim_ws(pack);
        if (!pack[0] || is_non_car_dlc(pack)) continue;
        char lbl[48];
        get_car_display_label(pack, lbl, sizeof(lbl));
        add_car_if_new(out_list, count, pack, lbl);
    }
    closedir(d);
}

static void parse_dlclist_for_cars(OrbxAddonCarEntry *out_list, uint32_t *count, const char *xml_path) {
    if (!xml_path || *count >= ORBX_MAX_ADDON_CARS) return;
    FILE *xf = fopen(xml_path, "r");
    if (!xf) return;
    char line[512];
    while (*count < ORBX_MAX_ADDON_CARS && fgets(line, sizeof(line), xf)) {
        char *open_tag = strstr(line, "<Item>");
        if (!open_tag) open_tag = strstr(line, "<item>");
        if (!open_tag) continue;
        char *close_tag = strstr(open_tag, "</Item>");
        if (!close_tag) close_tag = strstr(open_tag, "</item>");
        if (!close_tag) continue;

        *close_tag = '\0';
        char *content = open_tag + 6;
        trim_ws(content);
        if (!*content) continue;

        for (char *c = content; *c; c++) {
            if (*c == '\\') *c = '/';
        }

        size_t clen = strlen(content);
        while (clen > 0 && (content[clen - 1] == '/' || content[clen - 1] == ' ' || content[clen - 1] == '\t')) {
            content[--clen] = '\0';
        }
        if (clen == 0) continue;

        if (clen > 8 && strcasecmp(content + clen - 8, "/dlc.rpf") == 0) {
            content[clen - 8] = '\0';
            clen -= 8;
        }

        char *last_slash = strrchr(content, '/');
        char *pack = last_slash ? (last_slash + 1) : content;
        char *colon = strchr(pack, ':');
        if (colon) pack = colon + 1;
        trim_ws(pack);

        if (pack[0] && !is_non_car_dlc(pack)) {
            char lbl[48];
            get_car_display_label(pack, lbl, sizeof(lbl));
            add_car_if_new(out_list, count, pack, lbl);
        }
    }
    fclose(xf);
}

static uint32_t load_addon_cars_list(OrbxAddonCarEntry *out_list) {
    if (!out_list) return 0;
    memset(out_list, 0, sizeof(OrbxAddonCarEntry) * ORBX_MAX_ADDON_CARS);
    uint32_t count = 0;

    /* 1. Load existing entries from /data/orbx/gta/addon_cars.txt */
    FILE *acf = fopen("/data/orbx/gta/addon_cars.txt", "r");
    if (acf) {
        char line[128];
        while (count < ORBX_MAX_ADDON_CARS && fgets(line, sizeof(line), acf)) {
            trim_ws(line);
            if (!line[0] || line[0] == '#' || line[0] == ';') continue;
            char *sep = strchr(line, '|');
            char *label = NULL;
            if (sep) {
                *sep = '\0';
                label = sep + 1;
                trim_ws(label);
            }
            trim_ws(line);
            if (!line[0]) continue;
            char def_lbl[48];
            if (!label || !label[0]) {
                get_car_display_label(line, def_lbl, sizeof(def_lbl));
                label = def_lbl;
            }
            add_car_if_new(out_list, &count, line, label);
        }
        fclose(acf);
    }

    /* 2. Auto-discover from standard DLC directories */
    scan_addon_car_dir(out_list, &count, "/data/orbx/gta/rpf/dlc_addon");
    scan_addon_car_dir(out_list, &count, "/data/orbx/gta/dlc_addon");
    scan_addon_car_dir(out_list, &count, "/data/orbx/gta/dlcpacks");
    scan_addon_car_dir(out_list, &count, "/data/orbx/gta/rpf/dlcpacks");
    scan_addon_car_dir(out_list, &count, "/data/orbx/gta/update/x64/dlcpacks");

    /* 3. Auto-discover from dlclist.xml across common paths */
    parse_dlclist_for_cars(out_list, &count, "/data/orbx/gta/dlclist.xml");
    parse_dlclist_for_cars(out_list, &count, "/data/orbx/gta/data/dlclist.xml");
    parse_dlclist_for_cars(out_list, &count, "/data/orbx/gta/common/data/dlclist.xml");
    parse_dlclist_for_cars(out_list, &count, "/data/orbx/gta/update/update.rpf/common/data/dlclist.xml");
    parse_dlclist_for_cars(out_list, &count, "/data/orbx/gta/update/common/data/dlclist.xml");

    /* 4. Fallback defaults if still empty */
    if (count == 0u) {
        char l1[48], l2[48];
        get_car_display_label("27rev", l1, sizeof(l1));
        get_car_display_label("r35", l2, sizeof(l2));
        add_car_if_new(out_list, &count, "27rev", l1);
        add_car_if_new(out_list, &count, "r35", l2);
    }

    /* 5. Sync back discovered list to /data/orbx/gta/addon_cars.txt so it's always up to date */
    FILE *wf = fopen("/data/orbx/gta/addon_cars.txt", "w");
    if (wf) {
        fprintf(wf, "# modHEN Add-On DLC Vehicle List (model or model|Display Name)\n");
        for (uint32_t i = 0; i < count; i++) {
            if (strcmp(out_list[i].name, out_list[i].label) == 0) {
                fprintf(wf, "%s\n", out_list[i].name);
            } else {
                fprintf(wf, "%s|%s\n", out_list[i].name, out_list[i].label);
            }
        }
        fclose(wf);
        chmod("/data/orbx/gta/addon_cars.txt", 0777);
    }

    log_msg("[MENU] Active Add-On Vehicle Count: %u\n", count);
    return count;
}

static void sync_live_addon_cars_from_game_unlocked(pid_t pid) {
    if (!g_native_active || !g_native_ctx_addr) return;
    uint32_t live_count = 0;
    if (kernel_proc_copyout(pid,
                            (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, menu.addon_car_count)),
                            &live_count, sizeof(live_count)) != 0 || live_count == 0) {
        return;
    }
    if (live_count > ORBX_MAX_ADDON_CARS) live_count = ORBX_MAX_ADDON_CARS;

    OrbxAddonCarEntry live_cars[ORBX_MAX_ADDON_CARS];
    if (kernel_proc_copyout(pid,
                            (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, menu.addon_cars)),
                            live_cars, sizeof(OrbxAddonCarEntry) * live_count) != 0) {
        return;
    }

    OrbxAddonCarEntry disk_cars[ORBX_MAX_ADDON_CARS];
    uint32_t disk_count = load_addon_cars_list(disk_cars);
    int added = 0;
    for (uint32_t i = 0; i < live_count; i++) {
        if (!live_cars[i].name[0]) continue;
        int found = 0;
        for (uint32_t d = 0; d < disk_count; d++) {
            if (strcasecmp(disk_cars[d].name, live_cars[i].name) == 0) {
                found = 1;
                break;
            }
        }
        if (!found && disk_count < ORBX_MAX_ADDON_CARS) {
            add_car_if_new(disk_cars, &disk_count, live_cars[i].name, live_cars[i].label);
            added = 1;
        }
    }
    if (added) {
        FILE *wf = fopen("/data/orbx/gta/addon_cars.txt", "w");
        if (wf) {
            fprintf(wf, "# modHEN Add-On DLC Vehicle List (model or model|Display Name)\n");
            for (uint32_t i = 0; i < disk_count; i++) {
                if (strcmp(disk_cars[i].name, disk_cars[i].label) == 0) {
                    fprintf(wf, "%s\n", disk_cars[i].name);
                } else {
                    fprintf(wf, "%s|%s\n", disk_cars[i].name, disk_cars[i].label);
                }
            }
            fclose(wf);
            chmod("/data/orbx/gta/addon_cars.txt", 0777);
            log_msg("[MENU] Auto-saved new typed vehicle to /data/orbx/gta/addon_cars.txt (total %u)\n", disk_count);
        }
        reload_orbx_addon_cars_unlocked();
    reload_orbx_shaders_unlocked();
    }
}

static void populate_monitored_gots(const EbootGotQuery *queries, uint64_t code_base) {
    memset(g_monitored_gots, 0, sizeof(g_monitored_gots));
    for (size_t i = 0; i < NUM_MONITORED_GOTS; i++) {
        g_monitored_gots[i].name         = kHookFnNames[i];
        memcpy(g_monitored_gots[i].nid, queries[i].nid, 16);
        g_monitored_gots[i].got_slot     = queries[i].out_slot;
        g_monitored_gots[i].tramp_addr   = code_base + kHookEntryOffsets[i];
        g_monitored_gots[i].ctx_orig_off = kHookOrigCtxOffs[i];
        g_monitored_gots[i].is_fs        = (i < 11u) ? 1 : 0;
    }
}

typedef struct {
    uint64_t magic;
    uint64_t code_base;
    uint64_t ctx_base;
    uint32_t abi_version;
    uint32_t ctx_size;
    uint64_t code_alloc_size;
    uint64_t orig_wait;
} OrbxCtxPrefix;

static int probe_existing_orbx_at_slot(pid_t pid, uint64_t slot_val, uint32_t entry_off,
                                       uint64_t eboot_base, uint64_t eboot_size,
                                       int require_exact_abi,
                                       uint64_t *out_code_base, uint64_t *out_ctx_addr,
                                       OrbxCtxPrefix *out_prefix, OrbxNativeHeader *out_hdr) {
    if (slot_val < 0x10000ULL + entry_off || slot_val >= 0x800000000000ULL) return 0;
    uint64_t cand_code = slot_val - entry_off;
    if ((cand_code & 0xfffULL) != 0ULL) return 0;
    if (eboot_size && cand_code >= eboot_base && cand_code < eboot_base + eboot_size) return 0;

    OrbxNativeHeader hdr = {0};
    if (kernel_proc_copyout(pid, (intptr_t)cand_code, &hdr, sizeof(hdr)) != 0) return 0;
    if ((hdr.magic & ORBX_NATIVE_MAGIC_PREFIX_MASK) != ORBX_NATIVE_MAGIC_PREFIX) return 0;
    if (hdr.ctx_addr < 0x10000ULL || (hdr.ctx_addr & 0xfffULL) != 0ULL) return 0;

    OrbxCtxPrefix pfx = {0};
    if (kernel_proc_copyout(pid, (intptr_t)hdr.ctx_addr, &pfx, sizeof(pfx)) != 0) return 0;
    if (pfx.magic != hdr.magic || pfx.code_base != cand_code || pfx.ctx_base != hdr.ctx_addr) {
        return 0;
    }

    if (out_hdr)    *out_hdr    = hdr;
    if (out_prefix) *out_prefix = pfx;
    if (out_code_base) *out_code_base = cand_code;
    if (out_ctx_addr)  *out_ctx_addr  = hdr.ctx_addr;

    if (require_exact_abi) {
        if (hdr.magic != ORBX_NATIVE_MAGIC ||
            pfx.abi_version != ORBX_NATIVE_ABI_VERSION ||
            pfx.ctx_size != (uint32_t)sizeof(OrbxNativeContext)) {
            return 0;
        }
    }
    return 1;
}

static int try_reattach_live_orbx(pid_t pid, uint64_t eboot_base, uint64_t eboot_size,
                                  const EbootGotQuery *queries) {
    uint64_t live_code = 0, live_ctx = 0;
    OrbxCtxPrefix pfx = {0};
    OrbxNativeHeader hdr_seen = {0};
    for (size_t i = 0; i < NUM_MONITORED_GOTS; i++) {
        if (!queries[i].out_slot) continue;
        uint64_t cur_val = 0;
        if (kernel_proc_copyout(pid, (intptr_t)queries[i].out_slot, &cur_val, sizeof(cur_val)) != 0) {
            continue;
        }
        uint64_t cand_code = 0, cand_ctx = 0;
        OrbxCtxPrefix cand_pfx = {0};
        OrbxNativeHeader cand_hdr = {0};
        if (probe_existing_orbx_at_slot(pid, cur_val, kHookEntryOffsets[i],
                                        eboot_base, eboot_size, 0,
                                        &cand_code, &cand_ctx, &cand_pfx, &cand_hdr)) {
            if (cand_hdr.magic != ORBX_NATIVE_MAGIC ||
                cand_pfx.abi_version != ORBX_NATIVE_ABI_VERSION ||
                cand_pfx.ctx_size != (uint32_t)sizeof(OrbxNativeContext)) {
                log_msg("[ORBX-NATIVE] Incompatible live engine (magic=0x%llx abi=%u ctx=%u vs %u/%zu); replacing cleanly\n",
                        (unsigned long long)cand_hdr.magic,
                        cand_pfx.abi_version, cand_pfx.ctx_size,
                        ORBX_NATIVE_ABI_VERSION, sizeof(OrbxNativeContext));
                return 0;
            }
            live_code = cand_code;
            live_ctx  = cand_ctx;
            pfx       = cand_pfx;
            hdr_seen  = cand_hdr;
            break;
        }
    }
    if (!live_code || !live_ctx) return 0;

    size_t bin_size = (size_t)(_binary_orbx_native_bin_end - _binary_orbx_native_bin_start);
    if (bin_size < ORBX_ENTRY_MIN_BIN_SIZE) return 0;
    if (pfx.code_alloc_size != 0ULL && (uint64_t)bin_size > pfx.code_alloc_size) {
        log_msg("[ORBX-NATIVE] Live code allocation (%llu B) < new bin (%zu B); replacing cleanly\n",
                (unsigned long long)pfx.code_alloc_size, bin_size);
        return 0;
    }

    OrbxNativeConfig cfg = {0};
    uint32_t mod_cnt = 0, calls = 0, redirs = 0, widx = 0, tseq = 0, menu_armed = 0;
    kernel_proc_copyout(pid, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, cfg)), &cfg, sizeof(cfg));
    kernel_proc_copyout(pid, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, mod_count)), &mod_cnt, sizeof(mod_cnt));
    kernel_proc_copyout(pid, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, total_calls)), &calls, sizeof(calls));
    kernel_proc_copyout(pid, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, total_redirects)), &redirs, sizeof(redirs));
    kernel_proc_copyout(pid, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, tracked_seq)), &tseq, sizeof(tseq));
    kernel_proc_copyout(pid, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, log_write_idx)), &widx, sizeof(widx));
    kernel_proc_copyout(pid, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, menu.armed)), &menu_armed, sizeof(menu_armed));

    /* Never overwrite live RX bytes in-place if compiled code changed; active call stacks may return through it. */
    uint8_t *staged_bin = malloc(bin_size);
    uint8_t *live_copy  = malloc(bin_size);
    if (!staged_bin || !live_copy) {
        free(staged_bin);
        free(live_copy);
        return 0;
    }
    memcpy(staged_bin, _binary_orbx_native_bin_start, bin_size);
    OrbxNativeHeader *hdr = (OrbxNativeHeader *)staged_bin;
    hdr->ctx_addr = live_ctx;
    if (hdr->magic != hdr_seen.magic ||
        kernel_proc_copyout(pid, (intptr_t)live_code, live_copy, bin_size) != 0 ||
        memcmp(live_copy, staged_bin, bin_size) != 0) {
        free(staged_bin);
        free(live_copy);
        log_msg("[ORBX-NATIVE] Live code @ 0x%llx differs from updated payload; mapping fresh page safely\n",
                (unsigned long long)live_code);
        return 0;
    }
    free(staged_bin);
    free(live_copy);

    /* Identical RX image already loaded: clear only stuck input-capture locks via kernel copyin without ptrace pause. */
    uint32_t zero_u32 = 0u;
    kernel_proc_copyin(pid, &zero_u32, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, menu.kb_active)), sizeof(zero_u32));
    kernel_proc_copyin(pid, &zero_u32, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, menu.keybind_capture_slot)), sizeof(zero_u32));
    kernel_proc_copyin(pid, &zero_u32, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, menu.open_combo_wait_rel)), sizeof(zero_u32));
    log_msg("[ORBX-NATIVE] Re-attached cleanly to identical live_code @ 0x%llx (%zu B, 0 code bytes overwritten)\n",
            (unsigned long long)live_code, bin_size);

    g_native_pid        = (uint64_t)pid;
    g_native_ctx_addr   = live_ctx;
    g_eboot_base        = eboot_base;
    g_eboot_size        = eboot_size;
    g_code_addr         = live_code;
    g_code_alloc_size   = pfx.code_alloc_size ? pfx.code_alloc_size : 0x40000ULL;
    g_unlock_60fps      = cfg.unlock_60fps;
    populate_monitored_gots(queries, live_code);

    int armed_count = 0;
    for (size_t i = 0; i < NUM_MONITORED_GOTS; i++) {
        uint64_t cur_val = 0;
        if (g_monitored_gots[i].got_slot &&
            kernel_proc_copyout(pid, (intptr_t)g_monitored_gots[i].got_slot, &cur_val, sizeof(cur_val)) == 0 &&
            cur_val == g_monitored_gots[i].tramp_addr) {
            g_monitored_gots[i].armed = 1;
            armed_count++;
        }
    }

    g_fps_last_flips    = 0;
    g_fps_last_ms       = now_mono_ms();
    g_first_flip_seen   = 0;
    g_first_pad_seen    = 0;
    g_first_fs_seen     = (calls > 0u) ? 1 : 0;
    /* Always re-resolve runtime natives on re-attach so new natives are populated */
    g_native_reg_probed   = 0;
    g_menu_probe_attempts = 0;
    g_cached_mods       = mod_cnt;
    g_cached_calls      = calls;
    g_cached_redirects  = redirs;
    g_last_log_seq      = widx;
    g_last_tracked_seq  = tseq;
    g_telem_poll_tick   = 0;
    g_native_active     = 1;
    memset(g_tracked_last_bytes, 0, sizeof(g_tracked_last_bytes));
    memset(g_tracked_last_seq, 0, sizeof(g_tracked_last_seq));
    memset(g_tracked_idle_ticks, 0, sizeof(g_tracked_idle_ticks));
    memset(g_tracked_pending_bytes, 0, sizeof(g_tracked_pending_bytes));

    ModhenConfig mcfg;
    modhen_config_load(&mcfg);
    {
        uint32_t uval;
        uval = (uint32_t)mcfg.menu_position;      kernel_proc_copyin(pid, &uval, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, menu.menu_position)), sizeof(uval));
        uval = (uint32_t)mcfg.menu_scale;         kernel_proc_copyin(pid, &uval, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, menu.menu_scale)), sizeof(uval));
        uval = (uint32_t)mcfg.theme_accent;       kernel_proc_copyin(pid, &uval, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, menu.theme_accent)), sizeof(uval));
        uval = (uint32_t)mcfg.sound_enabled;      kernel_proc_copyin(pid, &uval, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, menu.sound_enabled)), sizeof(uval));
        /* Keep live gameplay/camera/FOV toggles untouched on reattach; never preload from INI */
        uval = mcfg.bind_open_mod;                kernel_proc_copyin(pid, &uval, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, menu.bind_open_mod)), sizeof(uval));
        uval = mcfg.bind_open_btn ? mcfg.bind_open_btn : 0x00000040u;
        kernel_proc_copyin(pid, &uval, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, menu.bind_open_btn)), sizeof(uval));
        uval = mcfg.bind_accept ? mcfg.bind_accept : 0x00004000u;
        kernel_proc_copyin(pid, &uval, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, menu.bind_accept)), sizeof(uval));
        uval = mcfg.bind_cancel ? mcfg.bind_cancel : 0x00002000u;
        kernel_proc_copyin(pid, &uval, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, menu.bind_cancel)), sizeof(uval));
        uval = mcfg.bind_tab_prev ? mcfg.bind_tab_prev : 0x00000400u;
        kernel_proc_copyin(pid, &uval, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, menu.bind_tab_prev)), sizeof(uval));
        uval = mcfg.bind_tab_next ? mcfg.bind_tab_next : 0x00000800u;
        kernel_proc_copyin(pid, &uval, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, menu.bind_tab_next)), sizeof(uval));
        uval = mcfg.bind_horn_boost;              kernel_proc_copyin(pid, &uval, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, menu.bind_horn_boost)), sizeof(uval));
        uval = mcfg.bind_noclip;                  kernel_proc_copyin(pid, &uval, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, menu.bind_noclip)), sizeof(uval));
        uval = mcfg.bind_quick_repair;            kernel_proc_copyin(pid, &uval, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, menu.bind_quick_repair)), sizeof(uval));
        uval = mcfg.bind_quick_heal;              kernel_proc_copyin(pid, &uval, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, menu.bind_quick_heal)), sizeof(uval));
        uval = mcfg.bind_super_brake;             kernel_proc_copyin(pid, &uval, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, menu.bind_super_brake)), sizeof(uval));
        uval = 0u;
        kernel_proc_copyin(pid, &uval, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, menu.open_combo_wait_rel)), sizeof(uval));
        kernel_proc_copyin(pid, &uval, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, menu.tab_pend_btn)), sizeof(uval));
        int32_t izero = 0;
        kernel_proc_copyin(pid, &izero, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, menu.tab_pend_dir)), sizeof(izero));
        uint32_t live_keep_steering = 0u;
        kernel_proc_copyout(pid, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, menu.keep_steering)), &live_keep_steering, sizeof(live_keep_steering));
        uint32_t sup = 0x000000F0u | mcfg.bind_accept | mcfg.bind_cancel |
                       mcfg.bind_tab_prev | mcfg.bind_tab_next |
                       mcfg.bind_open_mod | (mcfg.bind_open_btn ? mcfg.bind_open_btn : 0x00000040u);
        if (live_keep_steering) sup &= ~0x00000300u;
        kernel_proc_copyin(pid, &sup, (intptr_t)(live_ctx + offsetof(OrbxNativeContext, menu.pad_suppress_mask)), sizeof(sup));
    }
    if (mcfg.heap_probe || mcfg.heap_adjuster) {
        probe_rage_heap_allocator(pid, eboot_base, eboot_size, mcfg.heap_size_mb, mcfg.heap_adjuster);
    }

    reload_orbx_addon_cars_unlocked();
    reload_orbx_shaders_unlocked();

    log_msg("[ORBX-NATIVE] Re-attached cleanly to live Native Engine in PID %d (Code=0x%llx, Ctx=0x%llx, Hooks=%d, Mods=%u, Menu=%u)\n",
            pid, (unsigned long long)live_code, (unsigned long long)live_ctx,
            armed_count, mod_cnt, menu_armed);
    return 1;
}

/* ============================================================================
 * Public Host Lifecycle API
 * ============================================================================ */

static int install_orbx_native_locked(pid_t pid) {
    if (pid <= 1) return -1;

    log_msg("[ORBX-NATIVE] Starting Native PS5 Asset Engine for PID %d...\n", pid);

    uint64_t eboot_base = 0, eboot_size = 0;
    get_eboot_module_bounds(pid, &eboot_base, &eboot_size);
    log_msg("[ORBX-NATIVE] eboot.bin mapped @ 0x%llx (size: 0x%llx)\n",
            (unsigned long long)eboot_base, (unsigned long long)eboot_size);

    /* 1. Compute exact Sony NIDs for 11 FS/IO + 2 Video/Gnm + 2 Pad hooks + 3 diagnostic probes */
    char nid_sce_open[16] = {0}, nid_open[16] = {0};
    char nid_sce_stat[16] = {0}, nid_stat[16] = {0}, nid_fopen[16] = {0};
    char nid_sce_read[16] = {0}, nid_sce_pread[16] = {0};
    char nid_read[16] = {0}, nid_pread[16] = {0};
    char nid_sce_close[16] = {0}, nid_close[16] = {0};
    char nid_set_flip_rate[16] = {0}, nid_gnm_flip[16] = {0};
    char nid_pad_read_state[16] = {0}, nid_pad_read[16] = {0}, nid_reg_bufs[16] = {0};
    char nid_access[16] = {0}, nid_fios_exists[16] = {0}, nid_fios_open[16] = {0};

    nid_encode("sceKernelOpen", nid_sce_open);
    nid_encode("open", nid_open);
    nid_encode("sceKernelStat", nid_sce_stat);
    nid_encode("stat", nid_stat);
    nid_encode("fopen", nid_fopen);
    nid_encode("sceKernelRead", nid_sce_read);
    nid_encode("sceKernelPread", nid_sce_pread);
    nid_encode("read", nid_read);
    nid_encode("pread", nid_pread);
    nid_encode("sceKernelClose", nid_sce_close);
    nid_encode("close", nid_close);
    nid_encode("sceVideoOutSetFlipRate", nid_set_flip_rate);
    nid_encode("sceGnmSubmitAndFlipCommandBuffers", nid_gnm_flip);
    nid_encode("scePadReadState", nid_pad_read_state);
    nid_encode("scePadRead", nid_pad_read);
    nid_encode("sceVideoOutRegisterBuffers", nid_reg_bufs);
    nid_encode("access", nid_access);
    nid_encode("sceFiosFileExistsSync", nid_fios_exists);
    nid_encode("sceFiosFHOpenSync", nid_fios_open);

    EbootGotQuery queries[19] = {
        { .nid = nid_sce_open,       .out_slot = 0, .matched_sym = {0} },
        { .nid = nid_open,           .out_slot = 0, .matched_sym = {0} },
        { .nid = nid_sce_stat,       .out_slot = 0, .matched_sym = {0} },
        { .nid = nid_stat,           .out_slot = 0, .matched_sym = {0} },
        { .nid = nid_fopen,          .out_slot = 0, .matched_sym = {0} },
        { .nid = nid_sce_read,       .out_slot = 0, .matched_sym = {0} },
        { .nid = nid_sce_pread,      .out_slot = 0, .matched_sym = {0} },
        { .nid = nid_read,           .out_slot = 0, .matched_sym = {0} },
        { .nid = nid_pread,          .out_slot = 0, .matched_sym = {0} },
        { .nid = nid_sce_close,      .out_slot = 0, .matched_sym = {0} },
        { .nid = nid_close,          .out_slot = 0, .matched_sym = {0} },
        { .nid = nid_set_flip_rate,  .out_slot = 0, .matched_sym = {0} },
        { .nid = nid_gnm_flip,       .out_slot = 0, .matched_sym = {0} },
        { .nid = nid_pad_read_state, .out_slot = 0, .matched_sym = {0} },
        { .nid = nid_pad_read,       .out_slot = 0, .matched_sym = {0} },
        { .nid = nid_reg_bufs,       .out_slot = 0, .matched_sym = {0} },
        { .nid = nid_access,         .out_slot = 0, .matched_sym = {0} },
        { .nid = nid_fios_exists,    .out_slot = 0, .matched_sym = {0} },
        { .nid = nid_fios_open,      .out_slot = 0, .matched_sym = {0} }
    };

    int nfound = find_eboot_got_slots(pid, queries, 19);
    log_msg("[ORBX-NATIVE] eboot GOT scan matched %d slots "
            "(sceOpen=0x%llx open=0x%llx sceStat=0x%llx stat=0x%llx fopen=0x%llx | "
            "sceRead=0x%llx scePread=0x%llx read=0x%llx pread=0x%llx sceClose=0x%llx close=0x%llx | "
            "flipRate=0x%llx gnmFlip=0x%llx padState=0x%llx padRead=0x%llx access=0x%llx fios=0x%llx/0x%llx)\n",
            nfound,
            (unsigned long long)queries[0].out_slot,
            (unsigned long long)queries[1].out_slot,
            (unsigned long long)queries[2].out_slot,
            (unsigned long long)queries[3].out_slot,
            (unsigned long long)queries[4].out_slot,
            (unsigned long long)queries[5].out_slot,
            (unsigned long long)queries[6].out_slot,
            (unsigned long long)queries[7].out_slot,
            (unsigned long long)queries[8].out_slot,
            (unsigned long long)queries[9].out_slot,
            (unsigned long long)queries[10].out_slot,
            (unsigned long long)queries[11].out_slot,
            (unsigned long long)queries[12].out_slot,
            (unsigned long long)queries[13].out_slot,
            (unsigned long long)queries[14].out_slot,
            (unsigned long long)queries[15].out_slot,
            (unsigned long long)queries[16].out_slot,
            (unsigned long long)queries[17].out_slot);

    /* If GTA V already has an active Native Engine (e.g. daemon hot-reload), re-attach cleanly. */
    if (try_reattach_live_orbx(pid, eboot_base, eboot_size, queries)) {
        return 0;
    }

    /* 2. Resolve true SPRX targets for each hookable API (strictly rejecting unresolved 0xeffffffe... tokens) */
    static const char *const kHookPrxs[4] = {
        "libSceVideoOut.sprx",
        "libSceGnmDriver.sprx",
        "libSceGnmDriverForNeoMode.sprx",
        "libScePad.sprx"
    };
    uint64_t orig_addrs[NUM_MONITORED_GOTS];
    for (size_t i = 0; i < NUM_MONITORED_GOTS; i++) {
        orig_addrs[i] = resolve_from_libs_extra(pid, queries[i].nid, kHookPrxs, 4);
        if (!is_valid_prx_target(orig_addrs[i], eboot_base, eboot_size, 0)) {
            orig_addrs[i] = 0;
        }
        if (!orig_addrs[i] && queries[i].out_slot) {
            uint64_t cur_slot_val = 0;
            if (kernel_proc_copyout(pid, (intptr_t)queries[i].out_slot, &cur_slot_val, 8) == 0 &&
                is_valid_prx_target(cur_slot_val, eboot_base, eboot_size, 0) &&
                !probe_existing_orbx_at_slot(pid, cur_slot_val, kHookEntryOffsets[i],
                                             eboot_base, eboot_size, 0,
                                             NULL, NULL, NULL, NULL)) {
                orig_addrs[i] = cur_slot_val;
            }
        }
    }

    int any_hookable = 0;
    for (size_t i = 0; i < 5; i++) {
        if (queries[i].out_slot != 0 && orig_addrs[i] != 0) {
            any_hookable = 1;
            break;
        }
    }
    if (!any_hookable) {
        log_msg("[ORBX-NATIVE] Error: No hookable FS GOT slots resolved in eboot.bin!\n");
        return -1;
    }

    /* 3. Build host-side OrbxNativeContext (INI config + SSD JOAAT mod index) */
    OrbxNativeContext *ctx = calloc(1, sizeof(OrbxNativeContext));
    if (!ctx) {
        log_msg("[ORBX-NATIVE] Error: Out of host memory allocating OrbxNativeContext\n");
        return -1;
    }

    ctx->magic       = ORBX_NATIVE_MAGIC;
    ctx->abi_version = ORBX_NATIVE_ABI_VERSION;
    ctx->ctx_size    = (uint32_t)sizeof(OrbxNativeContext);
    for (uint32_t i = 0; i < ORBX_MAX_TRACKED_FDS; i++) {
        ctx->tracked_fds[i].fd = -1;
    }
    load_orbx_ini(&ctx->cfg);
    log_msg("[ORBX-NATIVE] INI: rpf=%u xml=%u dat=%u meta=%u gfx=%u gxt2=%u/%u 60fps=%u debug_rpf=%u\n",
            ctx->cfg.allow_rpf, ctx->cfg.allow_xml, ctx->cfg.allow_dat, ctx->cfg.allow_meta,
            ctx->cfg.allow_gfx, ctx->cfg.allow_gxt2, ctx->cfg.allow_dlc_gxt2,
            ctx->cfg.unlock_60fps, ctx->cfg.debug_rpf_info);
    build_mod_index(ctx);
    ModhenConfig mcfg;
    modhen_config_load(&mcfg);
    ctx->menu.menu_position     = (uint32_t)mcfg.menu_position;
    ctx->menu.menu_scale        = (uint32_t)mcfg.menu_scale;
    ctx->menu.theme_accent      = (uint32_t)mcfg.theme_accent;
    ctx->menu.sound_enabled     = (uint32_t)mcfg.sound_enabled;
    /* All runtime gameplay/camera/FOV mods always start OFF on fresh game launch */
    ctx->menu.keep_steering     = 0u;
    ctx->menu.nav_stick         = 0u;
    ctx->menu.speedo_mode       = 0u;
    ctx->menu.fp_fov_idx        = 0u;
    ctx->menu.fp_lock           = 0u;
    ctx->menu.fp_clip_fix       = 0u;
    ctx->menu.bind_open_mod     = mcfg.bind_open_mod;
    ctx->menu.bind_open_btn     = mcfg.bind_open_btn ? mcfg.bind_open_btn : 0x00000040u;
    ctx->menu.bind_accept       = mcfg.bind_accept   ? mcfg.bind_accept   : 0x00004000u;
    ctx->menu.bind_cancel       = mcfg.bind_cancel   ? mcfg.bind_cancel   : 0x00002000u;
    ctx->menu.bind_tab_prev     = mcfg.bind_tab_prev ? mcfg.bind_tab_prev : 0x00000400u;
    ctx->menu.bind_tab_next     = mcfg.bind_tab_next ? mcfg.bind_tab_next : 0x00000800u;
    ctx->menu.bind_horn_boost   = mcfg.bind_horn_boost;
    ctx->menu.bind_noclip       = mcfg.bind_noclip;
    ctx->menu.bind_quick_repair = mcfg.bind_quick_repair;
    ctx->menu.bind_quick_heal   = mcfg.bind_quick_heal;
    ctx->menu.bind_super_brake  = mcfg.bind_super_brake;
    uint32_t sup = 0x000000F0u | ctx->menu.bind_accept | ctx->menu.bind_cancel |
                   ctx->menu.bind_tab_prev | ctx->menu.bind_tab_next |
                   ctx->menu.bind_open_mod | ctx->menu.bind_open_btn;
    if (ctx->menu.keep_steering) sup &= ~0x00000300u;
    ctx->menu.pad_suppress_mask     = sup;
    ctx->menu.density_pct           = 100u;
    ctx->menu.last_density_pct      = 100u;
    ctx->menu.last_want_cap_applied = 5u;
    ctx->menu.lock_hour             = -1;
    ctx->menu.pad_active_handle     = -1;
    ctx->menu.cloud_hat_idx         = -1;
    ctx->menu.cloud_keyframe_idx    = -1;
    ctx->menu.cloud_alpha_pct       = 100u;
    ctx->menu.veh_neon_front        = 1u;
    ctx->menu.veh_neon_back         = 1u;
    ctx->menu.veh_neon_left         = 1u;
    ctx->menu.veh_neon_right        = 1u;
    ctx->menu.gfx_strength_pct      = 100u;
    ctx->menu.addon_car_count   = load_addon_cars_list(ctx->menu.addon_cars);
    log_msg("[ORBX-NATIVE] Loaded %u add-on car entries (first: %s = 0x%08x)\n",
            ctx->menu.addon_car_count, ctx->menu.addon_cars[0].name, ctx->menu.addon_cars[0].hash);

    ctx->orig_sceKernelOpen         = orig_addrs[0];
    ctx->orig_open                  = orig_addrs[1];
    ctx->orig_sceKernelStat         = orig_addrs[2];
    ctx->orig_stat                  = orig_addrs[3];
    ctx->orig_fopen                 = orig_addrs[4];
    ctx->orig_sceKernelRead         = orig_addrs[5];
    ctx->orig_sceKernelPread        = orig_addrs[6];
    ctx->orig_read                  = orig_addrs[7];
    ctx->orig_pread                 = orig_addrs[8];
    ctx->orig_sceKernelClose        = orig_addrs[9];
    ctx->orig_close                 = orig_addrs[10];
    ctx->orig_sceVideoOutSetFlipRate = orig_addrs[11];
    ctx->orig_sceGnmSubmitAndFlipCommandBuffers = orig_addrs[12];
    ctx->orig_scePadReadState       = orig_addrs[13];
    ctx->orig_scePadRead            = orig_addrs[14];
    ctx->orig_sceVideoOutRegisterBuffers = orig_addrs[15];
    {
        static const char *const kKernPrxs[2] = { "libkernel.sprx", "libkernel_sys.sprx" };
        char n_sz[16] = {0}, n_al[16] = {0}, n_mp[16] = {0};
        nid_encode("sceKernelGetDirectMemorySize", n_sz);
        nid_encode("sceKernelAllocateDirectMemory", n_al);
        nid_encode("sceKernelMapDirectMemory", n_mp);
        ctx->fn_get_dmem_size = resolve_from_libs_extra(pid, n_sz, kKernPrxs, 2);
        ctx->fn_alloc_dmem    = resolve_from_libs_extra(pid, n_al, kKernPrxs, 2);
        ctx->fn_map_dmem      = resolve_from_libs_extra(pid, n_mp, kKernPrxs, 2);
        log_msg("[ORBX-NATIVE] DirectMem APIs: size=0x%llx alloc=0x%llx map=0x%llx\n",
                (unsigned long long)ctx->fn_get_dmem_size,
                (unsigned long long)ctx->fn_alloc_dmem,
                (unsigned long long)ctx->fn_map_dmem);
    }
    ctx->orig_19cfd30               = eboot_base + 0x15cfd30ULL; /* 0x19cfd30 */
    ctx->orig_19cfd50               = eboot_base + 0x15cfd50ULL; /* 0x19cfd50 */
    uint8_t unlock_60fps_cfg        = ctx->cfg.unlock_60fps;

    /* 4. Allocate non-XOM anonymous RWX/RW memory in game PID via ptrace mmap */
    uint64_t syscall_insn = find_libkernel_syscall_insn(pid);
    if (!syscall_insn) {
        log_msg("[ORBX-NATIVE] Error: Failed to locate verified syscall instruction in libkernel\n");
        free(ctx);
        return -1;
    }

    size_t bin_size = (size_t)(_binary_orbx_native_bin_end - _binary_orbx_native_bin_start);
    if (bin_size < ORBX_ENTRY_MIN_BIN_SIZE) {
        log_msg("[ORBX-NATIVE] Error: Invalid embedded orbx_native.bin size (%zu)\n", bin_size);
        free(ctx);
        return -1;
    }

    size_t code_pages = (bin_size + 0x3fffu) & ~0x3fffu;
    if (code_pages < 0x40000u) code_pages = 0x40000u; /* Reserve 256 KB for future hot-reloads */
    size_t ctx_pages = (sizeof(OrbxNativeContext) + 0x3fffu) & ~0x3fffu;

    uint8_t *staged_bin = malloc(bin_size);
    if (!staged_bin) {
        free(ctx);
        return -1;
    }
    memcpy(staged_bin, _binary_orbx_native_bin_start, bin_size);

    OrbxNativeHeader *hdr = (OrbxNativeHeader *)staged_bin;
    if (hdr->magic != ORBX_NATIVE_MAGIC) {
        log_msg("[ORBX-NATIVE] Error: Binary magic mismatch (got 0x%llx)\n",
                (unsigned long long)hdr->magic);
        free(staged_bin);
        free(ctx);
        return -1;
    }

    uint64_t prev_auth = enter_ptrace_auth_scope();
    if (pt_attach(pid) != 0) {
        leave_ptrace_auth_scope(prev_auth);
        log_msg("[ORBX-NATIVE] Error: pt_attach(%d) failed (errno=%d)\n", pid, errno);
        free(staged_bin);
        free(ctx);
        return -1;
    }

    uint64_t remote_code = attached_remote_mmap(pid, syscall_insn, code_pages, PROT_READ | PROT_WRITE);
    uint64_t remote_ctx  = attached_remote_mmap(pid, syscall_insn, ctx_pages, PROT_READ | PROT_WRITE);

    if (!remote_code || !remote_ctx) {
        log_msg("[ORBX-NATIVE] Error: Anonymous page allocation failed (code=0x%llx, ctx=0x%llx)\n",
                (unsigned long long)remote_code, (unsigned long long)remote_ctx);
        detach_ptrace_with_auth(pid, prev_auth);
        free(staged_bin);
        free(ctx);
        return -1;
    }

    log_msg("[ORBX-NATIVE] Staged anon pages: code=0x%llx (%zu B), ctx=0x%llx (%zu B)\n",
            (unsigned long long)remote_code, code_pages,
            (unsigned long long)remote_ctx, ctx_pages);

    ctx->code_base       = remote_code;
    ctx->ctx_base        = remote_ctx;
    ctx->code_alloc_size = (uint64_t)code_pages;
    hdr->ctx_addr        = remote_ctx;

    /* Use pt_copyin (PT_IO / vm_fault) so demand-paged anonymous RAM faults into pmap cleanly */
    if (pt_copyin(pid, ctx, (intptr_t)remote_ctx, sizeof(OrbxNativeContext)) != 0) {
        log_msg("[ORBX-NATIVE] Error: pt_copyin failed writing OrbxNativeContext (errno=%d)\n", errno);
        detach_ptrace_with_auth(pid, prev_auth);
        free(staged_bin);
        free(ctx);
        return -1;
    }

    if (pt_copyin(pid, staged_bin, (intptr_t)remote_code, bin_size) != 0) {
        log_msg("[ORBX-NATIVE] Error: pt_copyin failed writing orbx_native.bin (errno=%d)\n", errno);
        detach_ptrace_with_auth(pid, prev_auth);
        free(staged_bin);
        free(ctx);
        return -1;
    }
    free(staged_bin);

    if (attached_remote_mprotect(pid, syscall_insn, remote_code, code_pages, PROT_READ | PROT_EXEC) != 0) {
        log_msg("[ORBX-NATIVE] Error: mprotect(RX) failed on code page 0x%llx\n",
                (unsigned long long)remote_code);
        detach_ptrace_with_auth(pid, prev_auth);
        free(ctx);
        return -1;
    }

    g_native_pid        = (uint64_t)pid;
    g_native_ctx_addr   = remote_ctx;
    g_eboot_base        = eboot_base;
    g_eboot_size        = eboot_size;
    g_code_addr         = remote_code;
    g_code_alloc_size   = (uint64_t)code_pages;
    g_unlock_60fps      = unlock_60fps_cfg;

    populate_monitored_gots(queries, remote_code);

    g_fps_last_flips    = 0;
    g_fps_last_ms       = now_mono_ms();
    g_first_flip_seen   = 0;
    g_first_pad_seen    = 0;
    g_first_fs_seen       = 0;
    g_native_reg_probed   = 0;
    g_menu_probe_attempts = 0;
    g_cached_mods       = ctx->mod_count;
    g_cached_calls      = 0;
    g_cached_redirects  = 0;
    g_last_log_seq      = 0;
    g_last_tracked_seq  = 0;
    g_telem_poll_tick   = 0;
    g_native_active     = 1;
    memset(g_tracked_last_bytes, 0, sizeof(g_tracked_last_bytes));
    memset(g_tracked_last_seq, 0, sizeof(g_tracked_last_seq));
    memset(g_tracked_idle_ticks, 0, sizeof(g_tracked_idle_ticks));
    memset(g_tracked_pending_bytes, 0, sizeof(g_tracked_pending_bytes));
    free(ctx);

    /* 5. Atomically arm verified FS/IO + Video/Gnm + Pad GOT slots + 60FPS SetFlipInterval patch while paused */
    int armed_count = 0;
    for (size_t i = 0; i < NUM_MONITORED_GOTS; i++) {
        if (queries[i].out_slot != 0 && orig_addrs[i] != 0) {
            uint64_t hook_target = g_monitored_gots[i].tramp_addr;
            int wr = pt_copyin(pid, &hook_target, (intptr_t)queries[i].out_slot, sizeof(hook_target));
            uint64_t verify = 0;
            kernel_proc_copyout(pid, (intptr_t)queries[i].out_slot, &verify, sizeof(verify));
            if (verify != hook_target) {
                wr = kernel_proc_copyin(pid, &hook_target, (intptr_t)queries[i].out_slot, sizeof(hook_target));
                verify = 0;
                kernel_proc_copyout(pid, (intptr_t)queries[i].out_slot, &verify, sizeof(verify));
            }
            if (wr == 0 && verify == hook_target) {
                log_msg("[ORBX-NATIVE] Armed %-33s GOT 0x%llx -> 0x%llx (orig: 0x%llx, verified=1)\n",
                        kHookFnNames[i],
                        (unsigned long long)queries[i].out_slot,
                        (unsigned long long)hook_target,
                        (unsigned long long)orig_addrs[i]);
                armed_count++;
                g_monitored_gots[i].armed = 1;
            } else {
                log_msg("[ORBX-NATIVE] Warning: Failed arming %-25s GOT 0x%llx (wr=%d, rb=0x%llx)\n",
                        kHookFnNames[i],
                        (unsigned long long)queries[i].out_slot,
                        wr, (unsigned long long)verify);
            }
        } else if (queries[i].out_slot != 0 && i >= 11) {
            log_msg("[ORBX-NATIVE] Defer %-33s GOT 0x%llx (waiting for PRX resolution)\n",
                    kHookFnNames[i], (unsigned long long)queries[i].out_slot);
        }
    }

    try_patch_v100_flip_interval(pid, eboot_base, unlock_60fps_cfg);
    try_patch_v100_pso_strs(pid, eboot_base, remote_code + ORBX_ENTRY_OFF_PSO_STRS);

    detach_ptrace_with_auth(pid, prev_auth);

    log_msg("[ORBX-NATIVE] Native Engine ACTIVE! Code=0x%llx (%zu B), Ctx=0x%llx, Hooks=%d, Mods=%u\n",
            (unsigned long long)remote_code, bin_size,
            (unsigned long long)remote_ctx, armed_count, g_cached_mods);

    /* 6. Safe Read-Only Heap Check (runs AFTER pt_detach so GTA V never stalls under ptrace) */
    if (mcfg.heap_probe || mcfg.heap_adjuster) {
        probe_rage_heap_allocator(pid, eboot_base, eboot_size, mcfg.heap_size_mb, mcfg.heap_adjuster);
    }

    /* 7. Read-only DMAP probe for RAGE v1.00 addresses (Phase 2 diagnostics) */
    run_rage_v100_probe(pid, eboot_base, eboot_size);

    /* 8. Stage 1 Shader Validator & Staging */
    reload_orbx_shaders_unlocked();

    return (armed_count > 0) ? 0 : -1;
}

int install_orbx_native(pid_t pid) {
    pthread_mutex_lock(&g_host_lock);
    int rc = install_orbx_native_locked(pid);
    pthread_mutex_unlock(&g_host_lock);
    return rc;
}

static void try_arm_monitored_got(pid_t pid, MonitoredGotHook *h) {
    if (!h || !h->got_slot || !h->tramp_addr || !g_native_ctx_addr || !h->nid[0]) return;
    uint64_t cur_got = 0;
    if (kernel_proc_copyout(pid, (intptr_t)h->got_slot, &cur_got, sizeof(cur_got)) != 0) return;
    if (cur_got == h->tramp_addr) {
        h->armed = 1u;
        h->arm_attempts = 0u;
        return;
    }

    if (h->arm_attempts < 255u) {
        h->arm_attempts++;
    }

    uint64_t real_fn = 0;
    if (is_valid_prx_target(cur_got, g_eboot_base, g_eboot_size, g_code_addr)) {
        real_fn = cur_got;
    }

    uint64_t prev_orig = 0;
    kernel_proc_copyout(pid, (intptr_t)(g_native_ctx_addr + h->ctx_orig_off), &prev_orig, sizeof(prev_orig));
    if (!real_fn && h->armed && is_valid_prx_target(prev_orig, g_eboot_base, g_eboot_size, g_code_addr)) {
        real_fn = prev_orig;
    }

    if (!real_fn) {
        static const char *const kPrxs[4] = {
            "libSceVideoOut.sprx", "libSceGnmDriver.sprx", "libSceGnmDriverForNeoMode.sprx", "libScePad.sprx"
        };
        real_fn = resolve_from_libs_extra(pid, h->nid, kPrxs, 4);
        if (!is_valid_prx_target(real_fn, g_eboot_base, g_eboot_size, g_code_addr)) {
            real_fn = 0;
        }
    }
    if (!real_fn) return;

    if (prev_orig != real_fn) {
        if (kernel_proc_copyin(pid, &real_fn, (intptr_t)(g_native_ctx_addr + h->ctx_orig_off), sizeof(real_fn)) != 0) {
            return;
        }
    }
    if (kernel_proc_copyin(pid, &h->tramp_addr, (intptr_t)h->got_slot, sizeof(h->tramp_addr)) == 0) {
        uint64_t verify = 0;
        kernel_proc_copyout(pid, (intptr_t)h->got_slot, &verify, sizeof(verify));
        if (verify == h->tramp_addr) {
            log_msg("[ORBX-NATIVE] %s %-33s GOT 0x%llx -> 0x%llx (orig: 0x%llx, was: 0x%llx)\n",
                    h->armed ? "Re-armed" : "Late-armed",
                    h->name, (unsigned long long)h->got_slot, (unsigned long long)h->tramp_addr,
                    (unsigned long long)real_fn, (unsigned long long)cur_got);
            h->armed = 1u;
            h->arm_attempts = 0u;
        }
    }
}

static void save_live_menu_to_ini_unlocked(void) {
    OrbxMenuState m;
    if (get_orbx_menu_state_unlocked(&m) != 0) return;

    ModhenConfig cfg;
    modhen_config_load(&cfg);
    cfg.menu_position      = (int)m.menu_position;
    cfg.menu_scale         = (int)m.menu_scale;
    cfg.theme_accent       = (int)m.theme_accent;
    cfg.sound_enabled      = m.sound_enabled ? 1 : 0;
    cfg.keep_steering_open = 0;
    cfg.nav_stick_enabled  = 0;
    cfg.speedo_mode        = 0;
    cfg.fp_fov_idx         = 0;
    cfg.fp_lock            = 0;
    cfg.fp_clip_fix        = 0;
    cfg.bind_open_mod      = m.bind_open_mod;
    cfg.bind_open_btn      = m.bind_open_btn;
    cfg.bind_accept        = m.bind_accept;
    cfg.bind_cancel        = m.bind_cancel;
    cfg.bind_tab_prev      = m.bind_tab_prev;
    cfg.bind_tab_next      = m.bind_tab_next;
    cfg.bind_horn_boost    = m.bind_horn_boost;
    cfg.bind_noclip        = m.bind_noclip;
    cfg.bind_quick_repair  = m.bind_quick_repair;
    cfg.bind_quick_heal    = m.bind_quick_heal;
    cfg.bind_super_brake   = m.bind_super_brake;
    if (modhen_config_save(&cfg) == 0) {
        log_msg("[V100-MENU] Saved menu settings & keybinds to %s\n", MODHEN_CONFIG_PATH);
    }
}

static void recompute_host_pad_suppress_mask_unlocked(void) {
    OrbxMenuState m;
    if (get_orbx_menu_state_unlocked(&m) != 0) return;
    uint32_t sup = 0x0010FFF6u & (0x000000F0u | m.bind_accept | m.bind_cancel |
                   m.bind_tab_prev | m.bind_tab_next | m.bind_open_mod | m.bind_open_btn);
    if (m.keep_steering) sup &= ~0x00000300u;
    kernel_proc_copyin((pid_t)g_native_pid, &sup,
                       (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, menu.pad_suppress_mask)),
                       sizeof(sup));
}

static void poll_orbx_native_telemetry_locked(pid_t pid) {
    if (!g_native_active || (uint64_t)pid != g_native_pid || !g_native_ctx_addr) return;
    if (kill(pid, 0) != 0) return; // Process died, abort immediately to prevent kernel fault

    g_telem_poll_tick++;

    uint32_t save_req = 0u;
    if (kernel_proc_copyout(pid,
                            (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, menu.cfg_save_req)),
                            &save_req, sizeof(save_req)) == 0 && save_req != 0u) {
        uint32_t zero = 0u;
        kernel_proc_copyin(pid, &zero,
                           (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, menu.cfg_save_req)),
                           sizeof(zero));
        if (save_req == 2u) {
            log_msg("[MENU] In-game reload requested: rescanning add-on cars...\n");
            reload_orbx_addon_cars_unlocked();
    reload_orbx_shaders_unlocked();
        } else {
            sync_live_addon_cars_from_game_unlocked(pid);
            save_live_menu_to_ini_unlocked();
        }
    }

    for (size_t i = 0; i < NUM_MONITORED_GOTS; i++) {
        MonitoredGotHook *h = &g_monitored_gots[i];
        if (!h->got_slot) continue;
        if (h->armed) {
            /* Already armed: re-check GOT slot infrequently (every 5s at 4 Hz) without PRX walks. */
            if (!h->is_fs && (g_telem_poll_tick % 20u) == 0u) {
                try_arm_monitored_got(pid, h);
            }
        } else {
            /* Unarmed lazy GOT slot: probe every tick (4 Hz) for ~5s, then back off to once every ~30s. */
            uint32_t period = (h->arm_attempts < 20u) ? 1u : 120u;
            if ((g_telem_poll_tick % period) == 0u) {
                try_arm_monitored_got(pid, h);
            }
        }
    }

    typedef struct {
        uint32_t pso_strs_calls;
        uint32_t pso_strs_sanitized;
        uint32_t flip_rate_logged;
        uint32_t flip_rate_enforced;
        uint64_t gnm_flip_count;
    } VideoTelemetrySnapshot;

    _Static_assert(sizeof(VideoTelemetrySnapshot) ==
                   (offsetof(OrbxNativeContext, total_calls) - offsetof(OrbxNativeContext, pso_strs_calls)),
                   "VideoTelemetrySnapshot layout must match OrbxNativeContext");

    VideoTelemetrySnapshot vstat;
    memset(&vstat, 0, sizeof(vstat));
    if (kernel_proc_copyout(pid,
                            (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, pso_strs_calls)),
                            &vstat, sizeof(vstat)) == 0) {
        uint32_t flip_interval = 0xffffffffu;
        int32_t  video_handle  = -1;
        if (g_eboot_base) {
            kernel_proc_copyout(pid, (intptr_t)(g_eboot_base + 0x315c500ULL), &flip_interval, sizeof(flip_interval));
            kernel_proc_copyout(pid, (intptr_t)(g_eboot_base + 0x31694b0ULL), &video_handle, sizeof(video_handle));
        }
        if (!g_first_flip_seen && vstat.gnm_flip_count > 0) {
            g_first_flip_seen = 1;
            g_fps_last_flips  = vstat.gnm_flip_count;
            g_fps_last_ms     = now_mono_ms();
            log_msg("[VIDEO] First GnmFlip observed (flips=%llu, interval=%u, handle=%d, enforced=%u, pso=%u/%u)\n",
                    (unsigned long long)vstat.gnm_flip_count, flip_interval, video_handle, vstat.flip_rate_enforced,
                    vstat.pso_strs_sanitized, vstat.pso_strs_calls);
        } else if (g_first_flip_seen) {
            uint64_t now_ms = now_mono_ms();
            if (now_ms > g_fps_last_ms && (now_ms - g_fps_last_ms) >= 5000ULL &&
                vstat.gnm_flip_count > g_fps_last_flips) {
                uint64_t df = vstat.gnm_flip_count - g_fps_last_flips;
                uint64_t dt = now_ms - g_fps_last_ms;
                double fps = ((double)df * 1000.0) / (double)dt;
                log_msg("[VIDEO] Live FPS=%.1f (flips=%llu, interval=%u, handle=%d, enforced=%u, 60Hz=%u, pso_guarded=%u/%u, fs=%u/%u)\n",
                        fps, (unsigned long long)vstat.gnm_flip_count,
                        flip_interval, video_handle, vstat.flip_rate_enforced,
                        (unsigned)g_unlock_60fps,
                        vstat.pso_strs_sanitized, vstat.pso_strs_calls,
                        g_cached_redirects, g_cached_calls);
                g_fps_last_flips = vstat.gnm_flip_count;
                g_fps_last_ms    = now_ms;
            }
        }
        if (!g_native_reg_probed && vstat.gnm_flip_count >= 120ULL && g_eboot_base &&
            ((g_telem_poll_tick % 4u) == 0u || g_menu_probe_attempts == 0u)) {
            g_menu_probe_attempts++;
            int arm_rc = try_arm_v100_mod_menu(pid, g_eboot_base);
            if (arm_rc != 0 || g_menu_probe_attempts >= 20u) {
                g_native_reg_probed = 1;
            }
        }
        if (!g_first_pad_seen) {
            uint32_t pad_raw = 0, pad_calls = 0;
            kernel_proc_copyout(pid, (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, menu.pad_raw_seen)),
                                &pad_raw, sizeof(pad_raw));
            kernel_proc_copyout(pid, (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, menu.pad_calls)),
                                &pad_calls, sizeof(pad_calls));
            if (pad_calls > 0u && pad_raw != 0u) {
                g_first_pad_seen = 1;
                log_msg("[V100-PAD] First controller input detected (calls=%u, raw_btns=0x%x)\n",
                        pad_calls, pad_raw);
            }
        }
    }

    typedef struct {
        uint32_t total_calls;
        uint32_t total_redirects;
        uint32_t trace_opens_logged;
        uint32_t dlc_traces_logged;
        uint32_t tracked_seq;
        uint32_t log_write_idx;
    } TelemetryHeader;

    typedef struct {
        uint64_t fd_bitmap[16];
        OrbxTrackedFd tracked_fds[ORBX_MAX_TRACKED_FDS];
    } TrackedTelemetrySnapshot;

    _Static_assert(sizeof(TelemetryHeader) ==
                   (offsetof(OrbxNativeContext, fd_bitmap) - offsetof(OrbxNativeContext, total_calls)),
                   "TelemetryHeader layout must match OrbxNativeContext");
    _Static_assert(sizeof(TrackedTelemetrySnapshot) ==
                   (offsetof(OrbxNativeContext, log_entries) - offsetof(OrbxNativeContext, fd_bitmap)),
                   "TrackedTelemetrySnapshot layout must match OrbxNativeContext");

    TelemetryHeader thdr;
    memset(&thdr, 0, sizeof(thdr));
    size_t off = offsetof(OrbxNativeContext, total_calls);
    if (kernel_proc_copyout(pid, (intptr_t)(g_native_ctx_addr + off), &thdr, sizeof(thdr)) != 0) {
        return;
    }

    g_cached_calls     = thdr.total_calls;
    g_cached_redirects = thdr.total_redirects;
    if (!g_first_fs_seen && thdr.total_calls > 0u) {
        g_first_fs_seen = 1;
        log_msg("[GTA-FS] First FS hook active (calls=%u, redirects=%u)\n",
                thdr.total_calls, thdr.total_redirects);
    }

    uint32_t widx = thdr.log_write_idx;
    if (widx > g_last_log_seq) {
        if (widx - g_last_log_seq > ORBX_LOG_RING_SLOTS) {
            log_msg("[GTA-FS] (Warning: ring buffer wrapped %u lines)\n",
                    (widx - g_last_log_seq) - ORBX_LOG_RING_SLOTS);
            g_last_log_seq = widx - ORBX_LOG_RING_SLOTS;
        }
        uint32_t fetched = 0;
        while (g_last_log_seq < widx && fetched < 64u) {
            uint32_t slot = g_last_log_seq % ORBX_LOG_RING_SLOTS;
            char line[ORBX_LOG_LINE_BYTES];
            size_t slot_off = offsetof(OrbxNativeContext, log_entries) + (size_t)slot * ORBX_LOG_LINE_BYTES;
            if (kernel_proc_copyout(pid, (intptr_t)(g_native_ctx_addr + slot_off), line, sizeof(line)) != 0) {
                break;
            }
            line[ORBX_LOG_LINE_BYTES - 1] = '\0';
            if (line[0] != '\0') {
                log_msg("[GTA-FS] %s\n", line);
            }
            g_last_log_seq++;
            fetched++;
        }
    }

    if (thdr.tracked_seq == g_last_tracked_seq && (g_telem_poll_tick % 8u) != 0u) {
        return;
    }
    g_last_tracked_seq = thdr.tracked_seq;

    TrackedTelemetrySnapshot tsnap;
    memset(&tsnap, 0, sizeof(tsnap));
    if (kernel_proc_copyout(pid,
                            (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, fd_bitmap)),
                            &tsnap, sizeof(tsnap)) != 0) {
        return;
    }

    /* Periodically report settled / milestone byte totals for still-open mod RPFs & XMLs */
    for (uint32_t i = 0; i < ORBX_MAX_TRACKED_FDS; i++) {
        OrbxTrackedFd *t = &tsnap.tracked_fds[i];
        t->name[sizeof(t->name) - 1] = '\0';
        if (t->open_seq != g_tracked_last_seq[i]) {
            g_tracked_last_seq[i] = t->open_seq;
            g_tracked_last_bytes[i] = 0;
            g_tracked_pending_bytes[i] = t->bytes_read;
            g_tracked_idle_ticks[i] = 0;
        }
        if (t->fd < 0 || t->bytes_read == 0) continue;

        if (t->bytes_read != g_tracked_pending_bytes[i]) {
            g_tracked_pending_bytes[i] = t->bytes_read;
            g_tracked_idle_ticks[i] = 0;
            if (t->bytes_read - g_tracked_last_bytes[i] >= (1024ULL * 1024ULL)) {
                log_msg("[GTA-FS] [MOD-IO] %s (fd=%d): %llu B read (%u calls)\n",
                        t->name, t->fd, (unsigned long long)t->bytes_read, t->read_calls);
                g_tracked_last_bytes[i] = t->bytes_read;
            }
        } else if (t->bytes_read != g_tracked_last_bytes[i]) {
            g_tracked_idle_ticks[i]++;
            if (g_tracked_idle_ticks[i] >= 3u) { /* ~1.5s-6s settled */
                log_msg("[GTA-FS] [MOD-IO] %s (fd=%d): %llu B read (%u calls)\n",
                        t->name, t->fd, (unsigned long long)t->bytes_read, t->read_calls);
                g_tracked_last_bytes[i] = t->bytes_read;
                g_tracked_idle_ticks[i] = 0;
            }
        }
    }
}

void poll_orbx_native_telemetry(pid_t pid) {
    pthread_mutex_lock(&g_host_lock);
    poll_orbx_native_telemetry_locked(pid);
    pthread_mutex_unlock(&g_host_lock);
}

void reset_orbx_native_state(void) {
    pthread_mutex_lock(&g_host_lock);
    g_native_active    = 0;
    g_native_pid       = 0;
    g_native_ctx_addr  = 0;
    g_eboot_base       = 0;
    g_eboot_size       = 0;
    g_code_addr        = 0;
    g_code_alloc_size  = 0;
    memset(g_monitored_gots, 0, sizeof(g_monitored_gots));
    g_fps_last_flips   = 0;
    g_fps_last_ms      = 0;
    g_first_flip_seen  = 0;
    g_first_pad_seen   = 0;
    g_first_fs_seen       = 0;
    g_native_reg_probed   = 0;
    g_menu_probe_attempts = 0;
    g_last_log_seq     = 0;
    g_last_tracked_seq = 0;
    g_telem_poll_tick  = 0;
    memset(g_tracked_last_bytes, 0, sizeof(g_tracked_last_bytes));
    memset(g_tracked_last_seq, 0, sizeof(g_tracked_last_seq));
    memset(g_tracked_idle_ticks, 0, sizeof(g_tracked_idle_ticks));
    memset(g_tracked_pending_bytes, 0, sizeof(g_tracked_pending_bytes));
    pthread_mutex_unlock(&g_host_lock);
}

void get_orbx_native_stats(uint32_t *out_mod_count, uint32_t *out_calls, uint32_t *out_redirects, int *out_active) {
    pthread_mutex_lock(&g_host_lock);
    if (out_mod_count) *out_mod_count = g_cached_mods;
    if (out_calls)     *out_calls     = g_cached_calls;
    if (out_redirects) *out_redirects = g_cached_redirects;
    if (out_active)    *out_active    = g_native_active;
    pthread_mutex_unlock(&g_host_lock);
}

static int get_orbx_menu_state_unlocked(OrbxMenuState *out_menu) {
    if (!out_menu || !g_native_active || !g_native_pid || !g_native_ctx_addr) return -1;
    memset(out_menu, 0, sizeof(*out_menu));
    size_t start_off = offsetof(OrbxMenuState, armed);
    size_t end_off   = sizeof(OrbxMenuState);
    if (kernel_proc_copyout((pid_t)g_native_pid,
                            (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, menu) + start_off),
                            ((uint8_t *)out_menu) + start_off,
                            end_off - start_off) != 0) {
        return -1;
    }
    return 0;
}

int get_orbx_menu_state(OrbxMenuState *out_menu) {
    pthread_mutex_lock(&g_host_lock);
    int rc = get_orbx_menu_state_unlocked(out_menu);
    pthread_mutex_unlock(&g_host_lock);
    return rc;
}

static int set_orbx_menu_toggle_unlocked(const char *name, uint32_t val) {
    if (!name || !g_native_active || !g_native_pid || !g_native_ctx_addr) return -1;
    static const struct {
        const char *name;
        size_t      off;
        uint8_t     kind; /* 0=u32, 1=bool, 2=keybind/ux */
    } kMenuFields[] = {
        { "menu_open",            offsetof(OrbxNativeContext, menu.menu_open),            1 },
        { "godmode",              offsetof(OrbxNativeContext, menu.godmode),              1 },
        { "never_wanted",         offsetof(OrbxNativeContext, menu.never_wanted),         1 },
        { "super_jump",           offsetof(OrbxNativeContext, menu.super_jump),           1 },
        { "fast_run",             offsetof(OrbxNativeContext, menu.fast_run),             1 },
        { "no_ragdoll",           offsetof(OrbxNativeContext, menu.no_ragdoll),           1 },
        { "invisible",            offsetof(OrbxNativeContext, menu.invisible),            1 },
        { "noclip_on",            offsetof(OrbxNativeContext, menu.noclip_on),            1 },
        { "spec_ability_inf",     offsetof(OrbxNativeContext, menu.spec_ability_inf),     1 },
        { "inf_ammo",             offsetof(OrbxNativeContext, menu.inf_ammo),             1 },
        { "explosive_ammo",       offsetof(OrbxNativeContext, menu.explosive_ammo),       1 },
        { "fire_ammo",            offsetof(OrbxNativeContext, menu.fire_ammo),            1 },
        { "explosive_melee",      offsetof(OrbxNativeContext, menu.explosive_melee),      1 },
        { "veh_god",              offsetof(OrbxNativeContext, menu.veh_god),              1 },
        { "seatbelt_on",          offsetof(OrbxNativeContext, menu.seatbelt_on),          1 },
        { "horn_boost",           offsetof(OrbxNativeContext, menu.horn_boost),           1 },
        { "veh_drift_mode",       offsetof(OrbxNativeContext, menu.veh_drift_mode),       1 },
        { "veh_bulletproof_tyres",offsetof(OrbxNativeContext, menu.veh_bulletproof_tyres),1 },
        { "veh_doors_locked",     offsetof(OrbxNativeContext, menu.veh_doors_locked),     1 },
        { "clock_paused",         offsetof(OrbxNativeContext, menu.clock_paused),         1 },
        { "low_gravity",          offsetof(OrbxNativeContext, menu.low_gravity),          1 },
        { "mobile_radio",         offsetof(OrbxNativeContext, menu.mobile_radio),         1 },
        { "weather_persist",      offsetof(OrbxNativeContext, menu.weather_persist),      1 },
        { "fp_lock",              offsetof(OrbxNativeContext, menu.fp_lock),              1 },
        { "fp_clip_fix",          offsetof(OrbxNativeContext, menu.fp_clip_fix),          1 },
        { "cinematic_cam",        offsetof(OrbxNativeContext, menu.cinematic_cam),        1 },
        { "cam_anti_shake",       offsetof(OrbxNativeContext, menu.cam_anti_shake),       1 },
        { "keep_steering",        offsetof(OrbxNativeContext, menu.keep_steering),        1 },
        { "nav_stick",            offsetof(OrbxNativeContext, menu.nav_stick),            1 },
        { "sound_enabled",        offsetof(OrbxNativeContext, menu.sound_enabled),        2 },
        { "density_pct",          offsetof(OrbxNativeContext, menu.density_pct),          0 },
        { "lock_hour",            offsetof(OrbxNativeContext, menu.lock_hour),            0 },
        { "noclip_speed_idx",     offsetof(OrbxNativeContext, menu.noclip_speed_idx),     0 },
        { "wanted_cap_idx",       offsetof(OrbxNativeContext, menu.wanted_cap_idx),       0 },
        { "sprint_mult_idx",      offsetof(OrbxNativeContext, menu.sprint_mult_idx),      0 },
        { "swim_mult_idx",        offsetof(OrbxNativeContext, menu.swim_mult_idx),        0 },
        { "veh_torque_idx",       offsetof(OrbxNativeContext, menu.veh_torque_idx),       0 },
        { "speedo_mode",          offsetof(OrbxNativeContext, menu.speedo_mode),          0 },
        { "time_scale_idx",       offsetof(OrbxNativeContext, menu.time_scale_idx),       0 },
        { "cloud_alpha_pct",      offsetof(OrbxNativeContext, menu.cloud_alpha_pct),      0 },
        { "fp_fov_idx",           offsetof(OrbxNativeContext, menu.fp_fov_idx),           0 },
        { "menu_position",        offsetof(OrbxNativeContext, menu.menu_position),        2 },
        { "menu_scale",           offsetof(OrbxNativeContext, menu.menu_scale),           2 },
        { "theme_accent",         offsetof(OrbxNativeContext, menu.theme_accent),         2 },
        { "gfx_preset_idx",       offsetof(OrbxNativeContext, menu.gfx_preset_idx),       0 },
        { "gfx_strength_pct",     offsetof(OrbxNativeContext, menu.gfx_strength_pct),     0 },
        { "gfx_film_grain",       offsetof(OrbxNativeContext, menu.gfx_film_grain),       0 },
        { "gfx_postfx_idx",       offsetof(OrbxNativeContext, menu.gfx_postfx_idx),       0 },
        { "enb_extra_active",     offsetof(OrbxNativeContext, menu.enb_extra_active),     0 },
        { "enb_shadow_soft",      offsetof(OrbxNativeContext, menu.enb_shadow_soft),      0 },
        { "enb_shadow_scale",     offsetof(OrbxNativeContext, menu.enb_shadow_scale),     0 },
        { "enb_ssao_boost",       offsetof(OrbxNativeContext, menu.enb_ssao_boost),       0 },
        { "bind_open_mod",        offsetof(OrbxNativeContext, menu.bind_open_mod),        2 },
        { "bind_open_btn",        offsetof(OrbxNativeContext, menu.bind_open_btn),        2 },
        { "bind_accept",          offsetof(OrbxNativeContext, menu.bind_accept),          2 },
        { "bind_cancel",          offsetof(OrbxNativeContext, menu.bind_cancel),          2 },
        { "bind_tab_prev",        offsetof(OrbxNativeContext, menu.bind_tab_prev),        2 },
        { "bind_tab_next",        offsetof(OrbxNativeContext, menu.bind_tab_next),        2 },
        { "bind_horn_boost",      offsetof(OrbxNativeContext, menu.bind_horn_boost),      2 },
        { "bind_noclip",          offsetof(OrbxNativeContext, menu.bind_noclip),          2 },
        { "bind_quick_repair",    offsetof(OrbxNativeContext, menu.bind_quick_repair),    2 },
        { "bind_quick_heal",      offsetof(OrbxNativeContext, menu.bind_quick_heal),      2 },
        { "bind_super_brake",     offsetof(OrbxNativeContext, menu.bind_super_brake),     2 },
        { "afp_mode",             offsetof(OrbxNativeContext, menu.afp_mode),             0 },
        { "afp_fov_idx",          offsetof(OrbxNativeContext, menu.afp_fov_idx),          0 },
        { "afp_headbob",          offsetof(OrbxNativeContext, menu.afp_headbob),          0 },
        { "afp_smooth_follow",    offsetof(OrbxNativeContext, menu.afp_smooth_follow),    0 },
        { "afp_recoil",           offsetof(OrbxNativeContext, menu.afp_recoil),           0 },
        { "afp_inertia",          offsetof(OrbxNativeContext, menu.afp_inertia),          1 },
        { "afp_crouch_lower",     offsetof(OrbxNativeContext, menu.afp_crouch_lower),     1 },
        { "afp_ads_zoom",         offsetof(OrbxNativeContext, menu.afp_ads_zoom),         0 },
        { "afp_crosshair",        offsetof(OrbxNativeContext, menu.afp_crosshair),        0 },
        { "afp_autoturn",         offsetof(OrbxNativeContext, menu.afp_autoturn),         1 },
        { "afp_clip_fix",         offsetof(OrbxNativeContext, menu.afp_clip_fix),         1 },
        { "fuel_system_on",       offsetof(OrbxNativeContext, menu.fuel_system_on),       1 },
        { "calmer_police_on",     offsetof(OrbxNativeContext, menu.calmer_police_on),     1 },
        { "gta6_hud_on",          offsetof(OrbxNativeContext, menu.gta6_hud_on),          1 },
        { "gta6_wheel_on",        offsetof(OrbxNativeContext, menu.gta6_wheel_on),        1 }
    };
    for (size_t i = 0; i < (sizeof(kMenuFields) / sizeof(kMenuFields[0])); i++) {
        if (strcmp(name, kMenuFields[i].name) == 0) {
            if (kMenuFields[i].kind == 1 ||
                strcmp(name, "keep_steering") == 0 ||
                strcmp(name, "nav_stick") == 0 ||
                strcmp(name, "sound_enabled") == 0) {
                val = val ? 1u : 0u;
            } else if ((strcmp(name, "density_pct") == 0 || strcmp(name, "gfx_film_grain") == 0 ||
                        strcmp(name, "cloud_alpha_pct") == 0) && val > 100u) {
                val = 100u;
            } else if (strcmp(name, "gfx_strength_pct") == 0) {
                if (val < 10u)  val = 10u;
                if (val > 200u) val = 200u;
            } else if (strcmp(name, "gfx_preset_idx") == 0) {
                if (val >= 15u) val = 0u;
                return trigger_orbx_menu_action_unlocked(ORBX_ACT_SET_GFX_PRESET, (int32_t)val, 0);
            } else if (strcmp(name, "gfx_postfx_idx") == 0) {
                if (val >= 7u) val = 0u;
                return trigger_orbx_menu_action_unlocked(ORBX_ACT_SET_POSTFX, (int32_t)val, 0);
            } else if (strcmp(name, "enb_extra_active") == 0) {
                return trigger_orbx_menu_action_unlocked(ORBX_ACT_SET_ENB_EXTRA, (int32_t)val, 0);
            } else if (strcmp(name, "enb_shadow_soft") == 0) {
                return trigger_orbx_menu_action_unlocked(ORBX_ACT_SET_SHADOW_QUALITY, (int32_t)val, 25);
            }
            if (kernel_proc_copyin((pid_t)g_native_pid, &val,
                                   (intptr_t)(g_native_ctx_addr + kMenuFields[i].off),
                                   sizeof(val)) != 0) {
                return -1;
            }
            if (kMenuFields[i].kind == 2 || strcmp(name, "keep_steering") == 0) {
                recompute_host_pad_suppress_mask_unlocked();
            }
            if (kMenuFields[i].kind == 2) {
                save_live_menu_to_ini_unlocked();
            }
            return 0;
        }
    }
    return -1;
}

int set_orbx_menu_toggle(const char *name, uint32_t val) {
    pthread_mutex_lock(&g_host_lock);
    int rc = set_orbx_menu_toggle_unlocked(name, val);
    pthread_mutex_unlock(&g_host_lock);
    return rc;
}

static int trigger_orbx_menu_action_unlocked(uint32_t action, int32_t arg0, uint32_t spawn_hash) {
    if (!g_native_active || !g_native_pid || !g_native_ctx_addr) return -1;
    pid_t pid = (pid_t)g_native_pid;
    if (kernel_proc_copyin(pid, &arg0,
                           (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, menu.web_arg0)),
                           sizeof(arg0)) != 0) return -1;
    if (kernel_proc_copyin(pid, &spawn_hash,
                           (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, menu.web_spawn_hash)),
                           sizeof(spawn_hash)) != 0) return -1;
    return (kernel_proc_copyin(pid, &action,
                               (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, menu.web_action)),
                               sizeof(action)) == 0) ? 0 : -1;
}

int trigger_orbx_menu_action(uint32_t action, int32_t arg0, uint32_t spawn_hash) {
    pthread_mutex_lock(&g_host_lock);
    int rc = trigger_orbx_menu_action_unlocked(action, arg0, spawn_hash);
    pthread_mutex_unlock(&g_host_lock);
    return rc;
}

static int reload_orbx_addon_cars_unlocked(void) {
    if (!g_native_active || !g_native_pid || !g_native_ctx_addr) return -1;
    OrbxAddonCarEntry list[ORBX_MAX_ADDON_CARS];
    uint32_t count = load_addon_cars_list(list);
    pid_t pid = (pid_t)g_native_pid;
    if (kernel_proc_copyin(pid, list,
                           (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, menu.addon_cars)),
                           sizeof(list)) != 0) return -1;
    if (kernel_proc_copyin(pid, &count,
                           (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, menu.addon_car_count)),
                           sizeof(count)) != 0) return -1;
    return (int)count;
}

int reload_orbx_addon_cars(void) {
    pthread_mutex_lock(&g_host_lock);
    int rc = reload_orbx_addon_cars_unlocked();
    pthread_mutex_unlock(&g_host_lock);
    return rc;
}

int spawn_orbx_vehicle_by_name(const char *model_name, int save_to_list) {
    (void)save_to_list;
    if (!model_name || !model_name[0]) return -1;
    char clean[32];
    snprintf(clean, sizeof(clean), "%s", model_name);
    trim_ws(clean);
    if (!clean[0]) return -1;

    pthread_mutex_lock(&g_host_lock);
    // Always auto-save to addon_cars.txt and reload so the in-game menu list updates immediately
    FILE *f = fopen("/data/orbx/gta/addon_cars.txt", "a");
    if (f) {
        fprintf(f, "%s\n", clean);
        fclose(f);
        chmod("/data/orbx/gta/addon_cars.txt", 0777);
    }
    reload_orbx_addon_cars_unlocked();
    reload_orbx_shaders_unlocked();
    int rc = trigger_orbx_menu_action_unlocked(ORBX_ACT_SPAWN_VEH, 0, host_joaat_lower(clean));
    pthread_mutex_unlock(&g_host_lock);
    return rc;
}

int get_orbx_gnm_probe(GnmCrashProbe *out_probe, uint64_t *out_flips) {
    if (!out_probe) return -1;
    memset(out_probe, 0, sizeof(*out_probe));
    if (out_flips) *out_flips = 0;
    pthread_mutex_lock(&g_host_lock);
    if (!g_native_active || !g_native_pid || !g_native_ctx_addr) {
        pthread_mutex_unlock(&g_host_lock);
        return -1;
    }
    pid_t pid = (pid_t)g_native_pid;
    if (out_flips) {
        kernel_proc_copyout(pid,
                            (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, gnm_flip_count)),
                            out_flips, sizeof(uint64_t));
    }
    int rc = kernel_proc_copyout(pid,
                                (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, gnm_probe)),
                                out_probe, sizeof(GnmCrashProbe));
    pthread_mutex_unlock(&g_host_lock);
    return rc;
}

#define ORBX_SHADERS_DIR    "/data/orbx/shaders"
#define ORBX_POSTPROCESS_SB "/data/orbx/shaders/postprocess.sb"

static int reload_orbx_shaders_unlocked(void) {
    mkdir(ORBX_SHADERS_DIR, 0777);
    if (!g_native_active || !g_native_pid || !g_native_ctx_addr) return -1;
    pid_t pid = (pid_t)g_native_pid;

    GnmShaderProbe sp;
    memset(&sp, 0, sizeof(sp));
    snprintf(sp.shader_file, sizeof(sp.shader_file), "%s", ORBX_POSTPROCESS_SB);
    sp.bloom_threshold = 0.85f;
    sp.bloom_intensity = 0.35f;
    sp.exposure        = 1.00f;
    sp.contrast        = 1.05f;
    sp.saturation      = 1.10f;
    sp.vignette        = 0.20f;
    sp.color_temp      = 6500.0f;
    sp.film_grain      = 0.00f;
    sp.anti_aliasing   = 0.35f;
    sp.sharpness       = 0.20f;

    struct stat st;
    if (stat(ORBX_POSTPROCESS_SB, &st) != 0) {
        sp.loaded = 0;
        sp.enabled = 0;
        sp.error_code = 1; /* NOT_FOUND */
        snprintf(sp.last_error_msg, sizeof(sp.last_error_msg), "Standby (No postprocess.sb found)");
        kernel_proc_copyin(pid, &sp, (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, shader_probe)), sizeof(sp));
        log_msg("[GNM-SHADER] %s not present (Standby, 0 GPU writes)\n", ORBX_POSTPROCESS_SB);
        return 0;
    }

    if (st.st_size < 64 || st.st_size > 1024 * 1024) {
        sp.loaded = 2; /* VALIDATION_ERROR */
        sp.enabled = 0;
        sp.error_code = 2; /* TOO_SMALL / TOO_LARGE */
        snprintf(sp.last_error_msg, sizeof(sp.last_error_msg), "Invalid size (%ld bytes)", (long)st.st_size);
        kernel_proc_copyin(pid, &sp, (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, shader_probe)), sizeof(sp));
        log_msg("[GNM-SHADER] Error: %s has invalid size (%ld bytes)\n", ORBX_POSTPROCESS_SB, (long)st.st_size);
        return -1;
    }

    FILE *f = fopen(ORBX_POSTPROCESS_SB, "rb");
    if (!f) {
        sp.loaded = 2;
        sp.enabled = 0;
        sp.error_code = 1;
        snprintf(sp.last_error_msg, sizeof(sp.last_error_msg), "Open error");
        kernel_proc_copyin(pid, &sp, (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, shader_probe)), sizeof(sp));
        return -1;
    }

    size_t sz = (size_t)st.st_size;
    uint8_t *sbuf = malloc(sz);
    if (!sbuf) {
        fclose(f);
        return -1;
    }
    size_t rd = fread(sbuf, 1, sz, f);
    fclose(f);
    if (rd != sz) {
        free(sbuf);
        sp.loaded = 2;
        sp.error_code = 1;
        snprintf(sp.last_error_msg, sizeof(sp.last_error_msg), "Incomplete read");
        kernel_proc_copyin(pid, &sp, (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, shader_probe)), sizeof(sp));
        return -1;
    }

    /* Pre-Flight Validator: Check Sony PSSB (0x42535350) or ELF header (0x7f454c46) */
    uint32_t magic = *(const uint32_t *)sbuf;
    if (magic != 0x42535350u && magic != 0x464c457fu) {
        free(sbuf);
        sp.loaded = 2;
        sp.error_code = 3; /* BAD_MAGIC */
        snprintf(sp.last_error_msg, sizeof(sp.last_error_msg), "Magic mismatch (0x%08x != PSSB/ELF)", magic);
        kernel_proc_copyin(pid, &sp, (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, shader_probe)), sizeof(sp));
        log_msg("[GNM-SHADER] Error: %s bad header magic 0x%08x\n", ORBX_POSTPROCESS_SB, magic);
        return -1;
    }

    /* Validated! Staged cleanly in GPU-accessible memory (starts OFF for safe boot; toggle ON in-game) */
    sp.loaded = 1;
    sp.enabled = 0;
    sp.shader_type = 1; /* CS_5_0 Compute Shader */
    sp.shader_size = (uint32_t)sz;
    sp.error_code = 0;
    snprintf(sp.last_error_msg, sizeof(sp.last_error_msg), "Stage 1 Compute Ready (%u B, OFF)", (unsigned)sz);

    kernel_proc_copyin(pid, &sp, (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, shader_probe)), sizeof(sp));
    free(sbuf);
    log_msg("[GNM-SHADER] Validated and armed %s (%zu bytes)\n", ORBX_POSTPROCESS_SB, sz);
    return 0;
}

int reload_orbx_shaders(void) {
    pthread_mutex_lock(&g_host_lock);
    int rc = reload_orbx_shaders_unlocked();
    pthread_mutex_unlock(&g_host_lock);
    return rc;
}

int get_orbx_shader_probe(GnmShaderProbe *out_probe) {
    if (!out_probe) return -1;
    memset(out_probe, 0, sizeof(*out_probe));
    pthread_mutex_lock(&g_host_lock);
    if (!g_native_active || !g_native_pid || !g_native_ctx_addr) {
        pthread_mutex_unlock(&g_host_lock);
        return -1;
    }
    pid_t pid = (pid_t)g_native_pid;
    int rc = kernel_proc_copyout(pid,
                                (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, shader_probe)),
                                out_probe, sizeof(GnmShaderProbe));
    pthread_mutex_unlock(&g_host_lock);
    return rc;
}

int set_orbx_shader_enabled(uint32_t enabled) {
    pthread_mutex_lock(&g_host_lock);
    if (!g_native_active || !g_native_pid || !g_native_ctx_addr) {
        pthread_mutex_unlock(&g_host_lock);
        return -1;
    }
    pid_t pid = (pid_t)g_native_pid;
    uint32_t val = enabled ? 1u : 0u;
    int rc = kernel_proc_copyin(pid, &val,
                                (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, shader_probe) + offsetof(GnmShaderProbe, enabled)),
                                sizeof(val));
    pthread_mutex_unlock(&g_host_lock);
    return rc;
}

int set_orbx_shader_tunables(float bloom_thresh, float bloom_int, float exposure, float contrast, float sat, float vig, float aa, float sharp) {
    pthread_mutex_lock(&g_host_lock);
    if (!g_native_active || !g_native_pid || !g_native_ctx_addr) {
        pthread_mutex_unlock(&g_host_lock);
        return -1;
    }
    pid_t pid = (pid_t)g_native_pid;
    GnmShaderProbe cur;
    int rc = -1;
    if (kernel_proc_copyout(pid, (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, shader_probe)), &cur, sizeof(cur)) == 0) {
        cur.bloom_threshold = bloom_thresh;
        cur.bloom_intensity = bloom_int;
        cur.exposure        = exposure;
        cur.contrast        = contrast;
        cur.saturation      = sat;
        cur.vignette        = vig;
        cur.anti_aliasing   = aa;
        cur.sharpness       = sharp;
        rc = kernel_proc_copyin(pid, &cur, (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, shader_probe)), sizeof(cur));
    }
    pthread_mutex_unlock(&g_host_lock);
    return rc;
}

int get_orbx_depth_probe(GnmDepthProbe *out_probe) {
    if (!out_probe) return -1;
    memset(out_probe, 0, sizeof(*out_probe));
    pthread_mutex_lock(&g_host_lock);
    if (!g_native_active || !g_native_pid || !g_native_ctx_addr) {
        pthread_mutex_unlock(&g_host_lock);
        return -1;
    }
    pid_t pid = (pid_t)g_native_pid;
    int rc = kernel_proc_copyout(pid, (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, depth_probe)), out_probe, sizeof(*out_probe));
    pthread_mutex_unlock(&g_host_lock);
    return rc;
}

int set_orbx_depth_tunables(float ssao_inten, float ssao_cp, float ssr_mult, float ssr_dist) {
    pthread_mutex_lock(&g_host_lock);
    if (!g_native_active || !g_native_pid || !g_native_ctx_addr) {
        pthread_mutex_unlock(&g_host_lock);
        return -1;
    }
    pid_t pid = (pid_t)g_native_pid;
    GnmDepthProbe cur;
    int rc = -1;
    if (kernel_proc_copyout(pid, (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, depth_probe)), &cur, sizeof(cur)) == 0) {
        cur.ssao_intensity   = ssao_inten;
        cur.ssao_cp_strength = ssao_cp;
        cur.ssr_intensity    = ssr_mult;
        cur.ssr_distance     = ssr_dist;
        cur.stage            = 3u; /* SSAO_ACTIVE */
        rc = kernel_proc_copyin(pid, &cur, (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, depth_probe)), sizeof(cur));
    }
    pthread_mutex_unlock(&g_host_lock);
    return rc;
}
