#define _GNU_SOURCE
#include "orbx_native_host.h"
#include "pad_hook.h"
#include "pt.h"
#include "log.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
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

#define ORBX_INI_PATH "/data/orbx/orbx.ini"
#define ORBX_GTA_DIR  "/data/orbx/gta"

static uint64_t g_native_pid       = 0;
static uint64_t g_native_ctx_addr  = 0;
static uint64_t g_eboot_base       = 0;
static uint64_t g_eboot_size       = 0;
static uint64_t g_code_addr        = 0;
typedef struct {
    const char *name;
    char        nid[16];
    uint64_t    got_slot;
    uint64_t    tramp_addr;
    size_t      ctx_orig_off;
    int         armed;
    int         is_fs;
} MonitoredGotHook;

#define NUM_MONITORED_GOTS 15
static MonitoredGotHook g_monitored_gots[NUM_MONITORED_GOTS];

static uint64_t g_fps_last_flips   = 0;
static uint64_t g_fps_last_ms      = 0;
static int      g_first_flip_seen  = 0;
static int      g_first_pad_seen   = 0;
static int      g_first_fs_seen    = 0;
static int      g_native_reg_probed = 0;
static uint8_t  g_unlock_60fps     = 1;
static uint32_t g_cached_mods      = 0;
static uint32_t g_cached_calls     = 0;
static uint32_t g_cached_redirects = 0;
static uint32_t g_last_log_seq     = 0;
static int      g_native_active    = 0;
static uint64_t g_tracked_last_bytes[ORBX_MAX_TRACKED_FDS];
static uint32_t g_tracked_last_seq[ORBX_MAX_TRACKED_FDS];
static uint32_t g_tracked_idle_ticks[ORBX_MAX_TRACKED_FDS];
static uint64_t g_tracked_pending_bytes[ORBX_MAX_TRACKED_FDS];

static int is_valid_prx_target(uint64_t addr, uint64_t eboot_base, uint64_t eboot_size, uint64_t code_addr) {
    if (addr < 0x100000ULL || addr >= 0x800000000000ULL) return 0;
    if (eboot_size && addr >= eboot_base && addr < eboot_base + eboot_size) return 0;
    if (code_addr && addr >= code_addr && addr < code_addr + 0x20000ULL) return 0;
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
        log_msg("[ORBX-NATIVE] psoInitStrings sig mismatch @ 0x%llx (%02x %02x %02x %02x)\n",
                (unsigned long long)site, orig[0], orig[1], orig[2], orig[3]);
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

static int try_arm_v100_mod_menu(pid_t pid, uint64_t eboot_base) {
    if (!eboot_base || !g_native_ctx_addr || !g_code_addr) return 0;
    uint64_t table_va = eboot_base + 0x3244a28ULL; /* 0x3644a28 s_NativeRegistrations */
    uint64_t wait_slot = 0;
    uint64_t h_wait = lookup_runtime_native_entry(pid, table_va, kMenuNativeSpecs[ORBX_NAT_WAIT].hash, &wait_slot);
    if (!h_wait || !wait_slot) return 0;

    uint64_t resolved[ORBX_NAT_COUNT];
    memset(resolved, 0, sizeof(resolved));
    resolved[ORBX_NAT_WAIT] = h_wait;

    int ok_count = 1;
    int req_missing = 0;
    for (uint32_t i = 1; i < ORBX_NAT_COUNT; i++) {
        resolved[i] = lookup_runtime_native_entry(pid, table_va, kMenuNativeSpecs[i].hash, NULL);
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

    if (kernel_proc_copyin(pid, resolved,
                           (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, menu.natives)),
                           sizeof(resolved)) != 0) {
        return -1;
    }
    uint32_t armed = 1u;
    if (kernel_proc_copyin(pid, &armed,
                           (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, menu.armed)),
                           sizeof(armed)) != 0) {
        return -1;
    }

    uint64_t tramp = g_code_addr + ORBX_ENTRY_OFF_MENU_WAIT;
    int wr = kernel_proc_copyin(pid, &tramp, (intptr_t)wait_slot, sizeof(tramp));
    uint64_t verify = 0;
    kernel_proc_copyout(pid, (intptr_t)wait_slot, &verify, sizeof(verify));
    log_msg("[V100-MENU] Armed WAIT slot 0x%llx -> 0x%llx (write_rc=%d verified=%d) | Open: R1 + D-Pad Left\n",
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

static int is_non_car_dlc(const char *name) {
    if (!name || !*name) return 1;
    if (strcasecmp(name, "NVE") == 0 ||
        strcasecmp(name, "gtaVremastered") == 0 ||
        strcasecmp(name, "vremastered") == 0 ||
        strcasecmp(name, "visualsettings") == 0 ||
        strcasecmp(name, "update") == 0 ||
        strncasecmp(name, "patchday", 8) == 0 ||
        strcasecmp(name, "mplowrider") == 0 ||
        strcasecmp(name, "mpbiker") == 0 ||
        strcasecmp(name, "data") == 0 ||
        strcasecmp(name, "levels") == 0) {
        return 1;
    }
    return 0;
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
            add_car_if_new(out_list, &count, line, label);
        }
        fclose(acf);
    }

    /* 2. Auto-discover from /data/orbx/gta/rpf/dlc_addon/ directories */
    DIR *d = opendir("/data/orbx/gta/rpf/dlc_addon");
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            if (is_non_car_dlc(ent->d_name)) continue;
            char lbl[48];
            if (strcasecmp(ent->d_name, "r35") == 0) {
                snprintf(lbl, sizeof(lbl), "Nissan GTR R35");
            } else if (strcasecmp(ent->d_name, "27rev") == 0) {
                snprintf(lbl, sizeof(lbl), "Lamborghini Revuelto");
            } else {
                snprintf(lbl, sizeof(lbl), "%s", ent->d_name);
            }
            add_car_if_new(out_list, &count, ent->d_name, lbl);
        }
        closedir(d);
    }

    /* 3. Auto-discover from dlclist.xml */
    FILE *xf = fopen("/data/orbx/gta/dlclist.xml", "r");
    if (xf) {
        char xline[256];
        while (count < ORBX_MAX_ADDON_CARS && fgets(xline, sizeof(xline), xf)) {
            char *p = strstr(xline, "dlc_addon/");
            if (p) {
                p += 10;
                char pack_name[48] = {0};
                size_t pidx = 0;
                while (*p && *p != '/' && *p != '<' && pidx + 1 < sizeof(pack_name)) {
                    pack_name[pidx++] = *p++;
                }
                pack_name[pidx] = '\0';
                trim_ws(pack_name);
                if (pack_name[0] && !is_non_car_dlc(pack_name)) {
                    add_car_if_new(out_list, &count, pack_name, pack_name);
                }
            }
        }
        fclose(xf);
    }

    /* 4. Fallback defaults if still empty */
    if (count == 0u) {
        add_car_if_new(out_list, &count, "27rev", "Lamborghini Revuelto");
        add_car_if_new(out_list, &count, "r35", "Nissan GTR R35");
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

/* ============================================================================
 * Public Host Lifecycle API
 * ============================================================================ */

int install_orbx_native(pid_t pid) {
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
    char nid_pad_read_state[16] = {0}, nid_pad_read[16] = {0};
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
    nid_encode("access", nid_access);
    nid_encode("sceFiosFileExistsSync", nid_fios_exists);
    nid_encode("sceFiosFHOpenSync", nid_fios_open);

    EbootGotQuery queries[18] = {
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
        { .nid = nid_access,         .out_slot = 0, .matched_sym = {0} },
        { .nid = nid_fios_exists,    .out_slot = 0, .matched_sym = {0} },
        { .nid = nid_fios_open,      .out_slot = 0, .matched_sym = {0} }
    };

    int nfound = find_eboot_got_slots(pid, queries, 18);
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

    /* 2. Resolve true SPRX targets for each hookable API (strictly rejecting unresolved 0xeffffffe... tokens) */
    static const char *const kHookPrxs[4] = {
        "libSceVideoOut.sprx",
        "libSceGnmDriver.sprx",
        "libSceGnmDriverForNeoMode.sprx",
        "libScePad.sprx"
    };
    uint64_t orig_addrs[15];
    for (size_t i = 0; i < 15; i++) {
        orig_addrs[i] = resolve_from_libs_extra(pid, queries[i].nid, kHookPrxs, 4);
        if (!is_valid_prx_target(orig_addrs[i], eboot_base, eboot_size, 0)) {
            orig_addrs[i] = 0;
        }
        if (!orig_addrs[i] && queries[i].out_slot) {
            uint64_t cur_slot_val = 0;
            if (kernel_proc_copyout(pid, (intptr_t)queries[i].out_slot, &cur_slot_val, 8) == 0 &&
                is_valid_prx_target(cur_slot_val, eboot_base, eboot_size, 0)) {
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

    ctx->magic = ORBX_NATIVE_MAGIC;
    for (uint32_t i = 0; i < ORBX_MAX_TRACKED_FDS; i++) {
        ctx->tracked_fds[i].fd = -1;
    }
    load_orbx_ini(&ctx->cfg);
    log_msg("[ORBX-NATIVE] INI: rpf=%u xml=%u dat=%u meta=%u gfx=%u gxt2=%u/%u 60fps=%u debug_rpf=%u\n",
            ctx->cfg.allow_rpf, ctx->cfg.allow_xml, ctx->cfg.allow_dat, ctx->cfg.allow_meta,
            ctx->cfg.allow_gfx, ctx->cfg.allow_gxt2, ctx->cfg.allow_dlc_gxt2,
            ctx->cfg.unlock_60fps, ctx->cfg.debug_rpf_info);
    build_mod_index(ctx);
    ctx->menu.density_pct       = 100u;
    ctx->menu.lock_hour         = -1;
    ctx->menu.pad_active_handle = -1;
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
    if (code_pages < 0x4000u) code_pages = 0x4000u;
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

    if (pt_attach(pid) != 0) {
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
        pt_detach(pid, 0);
        free(staged_bin);
        free(ctx);
        return -1;
    }

    log_msg("[ORBX-NATIVE] Staged anon pages: code=0x%llx (%zu B), ctx=0x%llx (%zu B)\n",
            (unsigned long long)remote_code, code_pages,
            (unsigned long long)remote_ctx, ctx_pages);

    ctx->code_base = remote_code;
    ctx->ctx_base  = remote_ctx;
    hdr->ctx_addr  = remote_ctx;

    /* Use pt_copyin (PT_IO / vm_fault) so demand-paged anonymous RAM faults into pmap cleanly */
    if (pt_copyin(pid, ctx, (intptr_t)remote_ctx, sizeof(OrbxNativeContext)) != 0) {
        log_msg("[ORBX-NATIVE] Error: pt_copyin failed writing OrbxNativeContext (errno=%d)\n", errno);
        pt_detach(pid, 0);
        free(staged_bin);
        free(ctx);
        return -1;
    }

    if (pt_copyin(pid, staged_bin, (intptr_t)remote_code, bin_size) != 0) {
        log_msg("[ORBX-NATIVE] Error: pt_copyin failed writing orbx_native.bin (errno=%d)\n", errno);
        pt_detach(pid, 0);
        free(staged_bin);
        free(ctx);
        return -1;
    }
    free(staged_bin);

    if (attached_remote_mprotect(pid, syscall_insn, remote_code, code_pages, PROT_READ | PROT_EXEC) != 0) {
        log_msg("[ORBX-NATIVE] Error: mprotect(RX) failed on code page 0x%llx\n",
                (unsigned long long)remote_code);
        pt_detach(pid, 0);
        free(ctx);
        return -1;
    }

    g_native_pid        = (uint64_t)pid;
    g_native_ctx_addr   = remote_ctx;
    g_eboot_base        = eboot_base;
    g_eboot_size        = eboot_size;
    g_code_addr         = remote_code;
    g_unlock_60fps      = unlock_60fps_cfg;

    static const uint32_t entry_offsets[NUM_MONITORED_GOTS] = {
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
        ORBX_ENTRY_OFF_SCE_PAD_READ
    };
    static const char *const fn_names[NUM_MONITORED_GOTS] = {
        "sceKernelOpen", "open", "sceKernelStat", "stat", "fopen",
        "sceKernelRead", "sceKernelPread", "read", "pread", "sceKernelClose", "close",
        "sceVideoOutSetFlipRate", "sceGnmSubmitAndFlipCommandBuffers",
        "scePadReadState", "scePadRead"
    };
    static const size_t orig_offs[NUM_MONITORED_GOTS] = {
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
        offsetof(OrbxNativeContext, orig_scePadRead)
    };

    memset(g_monitored_gots, 0, sizeof(g_monitored_gots));
    for (size_t i = 0; i < NUM_MONITORED_GOTS; i++) {
        g_monitored_gots[i].name         = fn_names[i];
        memcpy(g_monitored_gots[i].nid, queries[i].nid, 16);
        g_monitored_gots[i].got_slot     = queries[i].out_slot;
        g_monitored_gots[i].tramp_addr   = remote_code + entry_offsets[i];
        g_monitored_gots[i].ctx_orig_off = orig_offs[i];
        g_monitored_gots[i].is_fs        = (i < 11u) ? 1 : 0;
    }

    g_fps_last_flips    = 0;
    g_fps_last_ms       = now_mono_ms();
    g_first_flip_seen   = 0;
    g_first_pad_seen    = 0;
    g_first_fs_seen     = 0;
    g_native_reg_probed = 0;
    g_cached_mods       = ctx->mod_count;
    g_cached_calls      = 0;
    g_cached_redirects  = 0;
    g_last_log_seq      = 0;
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
                        fn_names[i],
                        (unsigned long long)queries[i].out_slot,
                        (unsigned long long)hook_target,
                        (unsigned long long)orig_addrs[i]);
                armed_count++;
                g_monitored_gots[i].armed = 1;
            } else {
                log_msg("[ORBX-NATIVE] Warning: Failed arming %-25s GOT 0x%llx (wr=%d, rb=0x%llx)\n",
                        fn_names[i],
                        (unsigned long long)queries[i].out_slot,
                        wr, (unsigned long long)verify);
            }
        } else if (queries[i].out_slot != 0 && i >= 11) {
            log_msg("[ORBX-NATIVE] Defer %-33s GOT 0x%llx (waiting for PRX resolution)\n",
                    fn_names[i], (unsigned long long)queries[i].out_slot);
        }
    }

    try_patch_v100_flip_interval(pid, eboot_base, unlock_60fps_cfg);
    try_patch_v100_pso_strs(pid, eboot_base, remote_code + ORBX_ENTRY_OFF_PSO_STRS);

    pt_detach(pid, 0);

    log_msg("[ORBX-NATIVE] Native Engine ACTIVE! Code=0x%llx (%zu B), Ctx=0x%llx, Hooks=%d, Mods=%u\n",
            (unsigned long long)remote_code, bin_size,
            (unsigned long long)remote_ctx, armed_count, g_cached_mods);

    /* 6. Read-only DMAP probe for RAGE v1.00 addresses (Phase 2 diagnostics) */
    run_rage_v100_probe(pid, eboot_base, eboot_size);

    return (armed_count > 0) ? 0 : -1;
}

static void try_arm_monitored_got(pid_t pid, MonitoredGotHook *h) {
    if (!h || !h->got_slot || !h->tramp_addr || !g_native_ctx_addr || !h->nid[0]) return;
    uint64_t cur_got = 0;
    if (kernel_proc_copyout(pid, (intptr_t)h->got_slot, &cur_got, sizeof(cur_got)) != 0) return;
    if (cur_got == h->tramp_addr) return;

    static const char *const kPrxs[4] = {
        "libSceVideoOut.sprx", "libSceGnmDriver.sprx", "libSceGnmDriverForNeoMode.sprx", "libScePad.sprx"
    };
    uint64_t real_fn = resolve_from_libs_extra(pid, h->nid, kPrxs, 4);
    if (!is_valid_prx_target(real_fn, g_eboot_base, g_eboot_size, g_code_addr)) {
        real_fn = 0;
    }
    if (!real_fn && is_valid_prx_target(cur_got, g_eboot_base, g_eboot_size, g_code_addr)) {
        real_fn = cur_got;
    }
    uint64_t prev_orig = 0;
    kernel_proc_copyout(pid, (intptr_t)(g_native_ctx_addr + h->ctx_orig_off), &prev_orig, sizeof(prev_orig));
    if (!real_fn) {
        if (h->armed && is_valid_prx_target(prev_orig, g_eboot_base, g_eboot_size, g_code_addr)) {
            real_fn = prev_orig;
        } else {
            return;
        }
    }

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
            h->armed = 1;
        }
    }
}

void poll_orbx_native_telemetry(pid_t pid) {
    if (!g_native_active || (uint64_t)pid != g_native_pid || !g_native_ctx_addr) return;
    if (kill(pid, 0) != 0) return; // Process died, abort immediately to prevent kernel fault

    for (size_t i = 0; i < NUM_MONITORED_GOTS; i++) {
        if (!g_monitored_gots[i].is_fs || g_monitored_gots[i].armed || g_monitored_gots[i].got_slot) {
            try_arm_monitored_got(pid, &g_monitored_gots[i]);
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
        if (!g_native_reg_probed && vstat.gnm_flip_count >= 120ULL && g_eboot_base) {
            int arm_rc = try_arm_v100_mod_menu(pid, g_eboot_base);
            if (arm_rc != 0) {
                g_native_reg_probed = 1;
            }
        }
        if (!g_first_pad_seen) {
            uint32_t pad_raw = 0, pad_calls = 0;
            kernel_proc_copyout(pid, (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, menu.pad_raw_seen)),
                                &pad_raw, sizeof(pad_raw));
            kernel_proc_copyout(pid, (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, menu.pad_calls)),
                                &pad_calls, sizeof(pad_calls));
            if (pad_raw != 0u) {
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
        uint64_t fd_bitmap[16];
        OrbxTrackedFd tracked_fds[ORBX_MAX_TRACKED_FDS];
        char     log_entries[ORBX_LOG_RING_SLOTS][ORBX_LOG_LINE_BYTES];
    } TelemetrySnapshot;

    _Static_assert(sizeof(TelemetrySnapshot) ==
                   (sizeof(OrbxNativeContext) - offsetof(OrbxNativeContext, total_calls) -
                    (sizeof(OrbxNativeContext) - offsetof(OrbxNativeContext, mod_count))),
                   "TelemetrySnapshot layout must match OrbxNativeContext");

    static TelemetrySnapshot snap;

    size_t off = offsetof(OrbxNativeContext, total_calls);
    if (kernel_proc_copyout(pid, (intptr_t)(g_native_ctx_addr + off), &snap, sizeof(snap)) != 0) {
        return;
    }

    g_cached_calls     = snap.total_calls;
    g_cached_redirects = snap.total_redirects;
    if (!g_first_fs_seen && snap.total_calls > 0u) {
        g_first_fs_seen = 1;
        log_msg("[GTA-FS] First FS hook active (calls=%u, redirects=%u)\n",
                snap.total_calls, snap.total_redirects);
    }

    uint32_t widx = snap.log_write_idx;
    if (widx > g_last_log_seq) {
        if (widx - g_last_log_seq > ORBX_LOG_RING_SLOTS) {
            log_msg("[GTA-FS] (Warning: ring buffer wrapped %u lines)\n",
                    (widx - g_last_log_seq) - ORBX_LOG_RING_SLOTS);
            g_last_log_seq = widx - ORBX_LOG_RING_SLOTS;
        }
        while (g_last_log_seq < widx) {
            uint32_t slot = g_last_log_seq % ORBX_LOG_RING_SLOTS;
            snap.log_entries[slot][ORBX_LOG_LINE_BYTES - 1] = '\0';
            if (snap.log_entries[slot][0] != '\0') {
                log_msg("[GTA-FS] %s\n", snap.log_entries[slot]);
            }
            g_last_log_seq++;
        }
    }

    /* Periodically report settled / milestone byte totals for still-open mod RPFs & XMLs */
    for (uint32_t i = 0; i < ORBX_MAX_TRACKED_FDS; i++) {
        OrbxTrackedFd *t = &snap.tracked_fds[i];
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
            if (g_tracked_idle_ticks[i] >= 6u) { /* ~1.5s settled */
                log_msg("[GTA-FS] [MOD-IO] %s (fd=%d): %llu B read (%u calls)\n",
                        t->name, t->fd, (unsigned long long)t->bytes_read, t->read_calls);
                g_tracked_last_bytes[i] = t->bytes_read;
                g_tracked_idle_ticks[i] = 0;
            }
        }
    }
}

void reset_orbx_native_state(void) {
    g_native_active    = 0;
    g_native_pid       = 0;
    g_native_ctx_addr  = 0;
    g_eboot_base       = 0;
    g_eboot_size       = 0;
    g_code_addr        = 0;
    memset(g_monitored_gots, 0, sizeof(g_monitored_gots));
    g_fps_last_flips   = 0;
    g_fps_last_ms      = 0;
    g_first_flip_seen  = 0;
    g_first_pad_seen   = 0;
    g_first_fs_seen    = 0;
    g_native_reg_probed = 0;
    g_last_log_seq     = 0;
    memset(g_tracked_last_bytes, 0, sizeof(g_tracked_last_bytes));
    memset(g_tracked_last_seq, 0, sizeof(g_tracked_last_seq));
    memset(g_tracked_idle_ticks, 0, sizeof(g_tracked_idle_ticks));
    memset(g_tracked_pending_bytes, 0, sizeof(g_tracked_pending_bytes));
}

void get_orbx_native_stats(uint32_t *out_mod_count, uint32_t *out_calls, uint32_t *out_redirects, int *out_active) {
    if (out_mod_count) *out_mod_count = g_cached_mods;
    if (out_calls)     *out_calls     = g_cached_calls;
    if (out_redirects) *out_redirects = g_cached_redirects;
    if (out_active)    *out_active    = g_native_active;
}

int get_orbx_menu_state(OrbxMenuState *out_menu) {
    if (!out_menu || !g_native_active || !g_native_pid || !g_native_ctx_addr) return -1;
    if (kernel_proc_copyout((pid_t)g_native_pid,
                            (intptr_t)(g_native_ctx_addr + offsetof(OrbxNativeContext, menu)),
                            out_menu, sizeof(*out_menu)) != 0) {
        return -1;
    }
    return 0;
}

int set_orbx_menu_toggle(const char *name, uint32_t val) {
    if (!name || !g_native_active || !g_native_pid || !g_native_ctx_addr) return -1;
    static const struct {
        const char *name;
        size_t      off;
        uint8_t     is_bool;
    } kMenuFields[] = {
        { "menu_open",       offsetof(OrbxNativeContext, menu.menu_open),       1 },
        { "godmode",         offsetof(OrbxNativeContext, menu.godmode),         1 },
        { "never_wanted",    offsetof(OrbxNativeContext, menu.never_wanted),    1 },
        { "super_jump",      offsetof(OrbxNativeContext, menu.super_jump),      1 },
        { "fast_run",        offsetof(OrbxNativeContext, menu.fast_run),        1 },
        { "no_ragdoll",      offsetof(OrbxNativeContext, menu.no_ragdoll),      1 },
        { "inf_ammo",        offsetof(OrbxNativeContext, menu.inf_ammo),        1 },
        { "explosive_ammo",  offsetof(OrbxNativeContext, menu.explosive_ammo),  1 },
        { "fire_ammo",       offsetof(OrbxNativeContext, menu.fire_ammo),       1 },
        { "explosive_melee", offsetof(OrbxNativeContext, menu.explosive_melee), 1 },
        { "veh_god",         offsetof(OrbxNativeContext, menu.veh_god),         1 },
        { "horn_boost",      offsetof(OrbxNativeContext, menu.horn_boost),      1 },
        { "density_pct",     offsetof(OrbxNativeContext, menu.density_pct),     0 },
        { "lock_hour",       offsetof(OrbxNativeContext, menu.lock_hour),       0 }
    };
    for (size_t i = 0; i < (sizeof(kMenuFields) / sizeof(kMenuFields[0])); i++) {
        if (strcmp(name, kMenuFields[i].name) == 0) {
            if (kMenuFields[i].is_bool) {
                val = val ? 1u : 0u;
            } else if (strcmp(name, "density_pct") == 0 && val > 100u) {
                val = 100u;
            }
            return (kernel_proc_copyin((pid_t)g_native_pid, &val,
                                       (intptr_t)(g_native_ctx_addr + kMenuFields[i].off),
                                       sizeof(val)) == 0) ? 0 : -1;
        }
    }
    return -1;
}

int trigger_orbx_menu_action(uint32_t action, int32_t arg0, uint32_t spawn_hash) {
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

int reload_orbx_addon_cars(void) {
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

int spawn_orbx_vehicle_by_name(const char *model_name, int save_to_list) {
    (void)save_to_list;
    if (!model_name || !model_name[0]) return -1;
    char clean[32];
    snprintf(clean, sizeof(clean), "%s", model_name);
    trim_ws(clean);
    if (!clean[0]) return -1;

    // Always auto-save to addon_cars.txt and reload so the in-game menu list updates immediately
    FILE *f = fopen("/data/orbx/gta/addon_cars.txt", "a");
    if (f) {
        fprintf(f, "%s\n", clean);
        fclose(f);
        chmod("/data/orbx/gta/addon_cars.txt", 0777);
    }
    reload_orbx_addon_cars();
    return trigger_orbx_menu_action(ORBX_ACT_SPAWN_VEH, 0, host_joaat_lower(clean));
}
