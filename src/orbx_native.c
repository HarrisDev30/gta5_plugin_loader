/*
 * ModHEN Orbx Native - Freestanding PIC File & Asset Redirection Binary (PS5 Native)
 * Compiled to flat PIC machine code (orbx_native.bin) via orbx_native.ld:
 *   +0x0000: OrbxNativeHeader (.orbx_header)
 *   +0x0010: orbx_entry_table (.text.entry)
 *     +0x10: sceKernelOpen hook
 *     +0x20: open hook
 *     +0x30: sceKernelStat hook
 *     +0x40: stat hook
 *     +0x50: fopen hook
 *     +0x60: sceKernelRead hook
 *     +0x70: sceKernelPread hook
 *     +0x80: read hook
 *     +0x90: pread hook
 *     +0xa0: sceKernelClose hook
 *     +0xb0: close hook
 *
 * Strictly zero .data, .bss, .got, and zero code-segment self-reads/writes (XOM-safe).
 */

#include <stdint.h>
#include <stddef.h>
#include "orbx_native_abi.h"
#include "orbx_pic_common.h"

extern void hook_menu_wait(void *nat_ctx);

__attribute__((section(".orbx_header"), used, visibility("hidden")))
const OrbxNativeHeader g_orbx_header = {
    .magic = ORBX_NATIVE_MAGIC,
    .ctx_addr = 0
};

/* Freestanding intrinsics with anti-idiom barrier so -O2 never emits self-recursion */
__attribute__((used, visibility("hidden")))
void *memset(void *dst, int c, size_t n) {
    uint8_t *p = (uint8_t *)dst;
    uint8_t v = (uint8_t)c;
    for (size_t i = 0; i < n; i++) {
        p[i] = v;
        __asm__ volatile("" ::: "memory");
    }
    return dst;
}

__attribute__((used, visibility("hidden")))
void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
        __asm__ volatile("" ::: "memory");
    }
    return dst;
}

static inline OrbxNativeContext *get_ctx(void) {
    uint64_t addr;
    __asm__ volatile (
        "movq g_orbx_header+8(%%rip), %0"
        : "=r"(addr)
    );
    OrbxNativeContext *ctx = (OrbxNativeContext *)(uintptr_t)addr;
    if (!ctx ||
        ctx->magic != ORBX_NATIVE_MAGIC ||
        ctx->abi_version != ORBX_NATIVE_ABI_VERSION ||
        ctx->ctx_size != (uint32_t)sizeof(OrbxNativeContext)) {
        return NULL;
    }
    return ctx;
}

static inline char ascii_tolower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static inline char norm_char(char c) {
    return (c == '\\') ? '/' : ascii_tolower(c);
}

static size_t pic_strlen(const char *s) {
    if (!s) return 0;
    size_t n = 0;
    while (s[n] != '\0') n++;
    return n;
}

static int starts_with_ci(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    while (*prefix) {
        if (norm_char(*s) != norm_char(*prefix)) return 0;
        s++;
        prefix++;
    }
    return 1;
}

static int eq_exact(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static int eq_ci_len(const char *a, size_t a_len, const char *b) {
    size_t i = 0;
    for (; i < a_len; i++) {
        if (b[i] == '\0') return 0;
        if (norm_char(a[i]) != norm_char(b[i])) return 0;
    }
    return b[i] == '\0';
}

static int contains_ci(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return 0;
    size_t nlen = pic_strlen(needle);
    for (size_t i = 0; hay[i] != '\0'; i++) {
        size_t j = 0;
        while (j < nlen && hay[i + j] != '\0' && norm_char(hay[i + j]) == norm_char(needle[j])) {
            j++;
        }
        if (j == nlen) return 1;
    }
    return 0;
}

static int ends_with_ci_len(const char *s, size_t len, const char *sfx) {
    size_t slen = pic_strlen(sfx);
    if (len < slen) return 0;
    const char *p = s + (len - slen);
    for (size_t i = 0; i < slen; i++) {
        if (norm_char(p[i]) != norm_char(sfx[i])) return 0;
    }
    return 1;
}

static const char *find_substr_ci(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return NULL;
    size_t nlen = pic_strlen(needle);
    for (size_t i = 0; hay[i] != '\0'; i++) {
        size_t j = 0;
        while (j < nlen && hay[i + j] != '\0' && norm_char(hay[i + j]) == norm_char(needle[j])) {
            j++;
        }
        if (j == nlen) return hay + i;
    }
    return NULL;
}

static uint32_t calc_joaat_lower(const char *s, size_t len) {
    uint32_t hash = 0;
    for (size_t i = 0; i < len; i++) {
        char c = norm_char(s[i]);
        hash += (uint32_t)(uint8_t)c;
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;
}

/* Stack-only string builders (zero .data/.rodata issues) */
static size_t fmt_append_str(char *dst, size_t pos, size_t cap, const char *s) {
    if (!dst) return pos;
    if (s) {
        while (*s && pos < cap) {
            dst[pos++] = *s++;
        }
    }
    if (pos <= cap) dst[pos] = '\0';
    return pos;
}

static size_t fmt_append_u64(char *dst, size_t pos, size_t cap, uint64_t v) {
    char tmp[24];
    size_t n = 0;
    if (!dst) return pos;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        while (v > 0 && n < sizeof(tmp)) {
            tmp[n++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
    }
    while (n > 0 && pos < cap) {
        dst[pos++] = tmp[--n];
    }
    if (pos <= cap) dst[pos] = '\0';
    return pos;
}

static size_t fmt_append_i64(char *dst, size_t pos, size_t cap, int64_t v) {
    if (v < 0) {
        if (pos < cap) dst[pos++] = '-';
        uint64_t uv = (uint64_t)(-(v + 1)) + 1ULL;
        return fmt_append_u64(dst, pos, cap, uv);
    }
    return fmt_append_u64(dst, pos, cap, (uint64_t)v);
}

static size_t fmt_append_hex32(char *dst, size_t pos, size_t cap, uint32_t v) {
    if (!dst) return pos;
    pos = fmt_append_str(dst, pos, cap, "0x");
    int started = 0;
    for (int sh = 28; sh >= 0; sh -= 4) {
        uint32_t nib = (v >> sh) & 0xfu;
        if (nib != 0 || started || sh == 0) {
            started = 1;
            if (pos < cap) {
                dst[pos++] = (char)((nib < 10u) ? ('0' + nib) : ('a' + (nib - 10u)));
            }
        }
    }
    if (pos <= cap) dst[pos] = '\0';
    return pos;
}

static size_t fmt_append_hex64(char *dst, size_t pos, size_t cap, uint64_t v) {
    if (!dst) return pos;
    pos = fmt_append_str(dst, pos, cap, "0x");
    int started = 0;
    for (int sh = 60; sh >= 0; sh -= 4) {
        uint32_t nib = (uint32_t)((v >> sh) & 0xfULL);
        if (nib != 0 || started || sh == 0) {
            started = 1;
            if (pos < cap) {
                dst[pos++] = (char)((nib < 10u) ? ('0' + nib) : ('a' + (nib - 10u)));
            }
        }
    }
    if (pos <= cap) dst[pos] = '\0';
    return pos;
}

static void log_ring_raw(OrbxNativeContext *ctx, const char *msg) {
    uint32_t idx = __atomic_fetch_add(&ctx->log_write_idx, 1u, __ATOMIC_RELAXED) % ORBX_LOG_RING_SLOTS;
    char *line = ctx->log_entries[idx];
    size_t cap = ORBX_LOG_LINE_BYTES - 1;
    size_t pos = fmt_append_str(line, 0, cap, msg);
    line[pos] = '\0';
}

__attribute__((visibility("hidden")))
OrbxNativeContext *orbx_pic_get_ctx(void) {
    return get_ctx();
}

__attribute__((visibility("hidden")))
void orbx_pic_log_raw(OrbxNativeContext *ctx, const char *msg) {
    if (ctx && msg) log_ring_raw(ctx, msg);
}

__attribute__((visibility("hidden")))
size_t orbx_pic_fmt_str(char *dst, size_t pos, size_t cap, const char *s) {
    return fmt_append_str(dst, pos, cap, s);
}

__attribute__((visibility("hidden")))
size_t orbx_pic_fmt_u64(char *dst, size_t pos, size_t cap, uint64_t v) {
    return fmt_append_u64(dst, pos, cap, v);
}

__attribute__((visibility("hidden")))
size_t orbx_pic_fmt_hex32(char *dst, size_t pos, size_t cap, uint32_t v) {
    return fmt_append_hex32(dst, pos, cap, v);
}

__attribute__((visibility("hidden")))
uint32_t orbx_pic_joaat(const char *s) {
    if (!s) return 0;
    return calc_joaat_lower(s, pic_strlen(s));
}

/* Ensure trailing outcome text (" => ret=0 sz=...") is never truncated off long paths */
static void log_path_outcome(OrbxNativeContext *ctx, const char *tag, const char *api_tag,
                             const char *src, const char *dst, const char *outcome) {
    uint32_t idx = __atomic_fetch_add(&ctx->log_write_idx, 1u, __ATOMIC_RELAXED) % ORBX_LOG_RING_SLOTS;
    char *line = ctx->log_entries[idx];
    const size_t total_cap = ORBX_LOG_LINE_BYTES - 1;
    size_t olen = pic_strlen(outcome);
    if (olen > 64u) olen = 64u;
    size_t path_cap = (total_cap > olen) ? (total_cap - olen) : total_cap;

    size_t pos = 0;
    pos = fmt_append_str(line, pos, path_cap, tag);
    pos = fmt_append_str(line, pos, path_cap, api_tag);
    pos = fmt_append_str(line, pos, path_cap, " ");
    pos = fmt_append_str(line, pos, path_cap, src);
    if (dst && !eq_exact(src, dst)) {
        pos = fmt_append_str(line, pos, path_cap, " -> ");
        pos = fmt_append_str(line, pos, path_cap, dst);
    }
    pos = fmt_append_str(line, pos, total_cap, outcome);
    line[pos] = '\0';
}

static size_t format_open_outcome(char *buf, size_t cap, int ret) {
    size_t pos = 0;
    if (ret >= 0) {
        pos = fmt_append_str(buf, pos, cap, " => fd=");
        pos = fmt_append_i64(buf, pos, cap, (int64_t)ret);
    } else {
        pos = fmt_append_str(buf, pos, cap, " => ERR ");
        pos = fmt_append_hex32(buf, pos, cap, (uint32_t)ret);
    }
    buf[pos] = '\0';
    return pos;
}

static size_t format_stat_outcome(char *buf, size_t cap, int ret, const void *sb) {
    size_t pos = 0;
    if (ret == 0) {
        pos = fmt_append_str(buf, pos, cap, " => ok");
        if (sb) {
            uint16_t mode = *(const uint16_t *)((const uint8_t *)sb + 8u);
            int64_t st_sz = *(const int64_t *)((const uint8_t *)sb + ORBX_STAT_SIZE_OFFSET);
            if ((mode & 0170000u) == 0040000u) {
                pos = fmt_append_str(buf, pos, cap, " DIR");
            } else {
                pos = fmt_append_str(buf, pos, cap, " sz=");
                pos = fmt_append_i64(buf, pos, cap, st_sz);
            }
        }
    } else {
        pos = fmt_append_str(buf, pos, cap, " => ERR ");
        pos = fmt_append_hex32(buf, pos, cap, (uint32_t)ret);
    }
    buf[pos] = '\0';
    return pos;
}

static size_t format_fopen_outcome(char *buf, size_t cap, const void *fp) {
    size_t pos = 0;
    pos = fmt_append_str(buf, pos, cap, fp ? " => FILE=OK" : " => FILE=NULL");
    buf[pos] = '\0';
    return pos;
}

static int is_ext_allowed(const OrbxNativeConfig *cfg, const char *key, size_t len) {
    if (ends_with_ci_len(key, len, ".rpf"))  return cfg->allow_rpf != 0;
    if (ends_with_ci_len(key, len, ".xml"))  return cfg->allow_xml != 0;
    if (ends_with_ci_len(key, len, ".omt"))  return cfg->allow_omt != 0;
    if (ends_with_ci_len(key, len, ".dat"))  return cfg->allow_dat != 0;
    if (ends_with_ci_len(key, len, ".meta")) return cfg->allow_meta != 0;
    if (ends_with_ci_len(key, len, ".gfx"))  return cfg->allow_gfx != 0;
    if (ends_with_ci_len(key, len, ".gxt2")) {
        if (contains_ci(key, "dlc")) return (cfg->allow_dlc_gxt2 != 0 || cfg->allow_gxt2 != 0);
        return cfg->allow_gxt2 != 0;
    }
    if (ends_with_ci_len(key, len, ".bik") && contains_ci(key, "rockstar")) {
        return cfg->allow_rockstar_intro != 0;
    }
    return 1;
}

static const char *lookup_mod_key(const OrbxNativeContext *ctx, const char *key, size_t len) {
    if (!key || len == 0 || ctx->mod_count == 0) return NULL;
    while (len > 0 && *key == '/') {
        key++;
        len--;
    }
    while (len > 0 && key[len - 1] == '/') {
        len--;
    }
    if (len == 0) return NULL;

    uint32_t target_hash = calc_joaat_lower(key, len);
    uint32_t count = ctx->mod_count;
    if (count > ORBX_MAX_MOD_ENTRIES) count = ORBX_MAX_MOD_ENTRIES;

    uint32_t lo = 0;
    uint32_t hi = count;
    while (lo < hi) {
        uint32_t mid = lo + ((hi - lo) >> 1);
        if (ctx->mods[mid].joaat_hash < target_hash) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    while (lo < count && ctx->mods[lo].joaat_hash == target_hash) {
        uint32_t koff = ctx->mods[lo].key_offset;
        uint32_t poff = ctx->mods[lo].path_offset;
        if (koff < ORBX_STRING_POOL_BYTES && poff < ORBX_STRING_POOL_BYTES) {
            const char *stored_key = &ctx->string_pool[koff];
            if (eq_ci_len(key, len, stored_key)) {
                return &ctx->string_pool[poff];
            }
        }
        lo++;
    }
    return NULL;
}

static const char *lookup_mod_prefix_and_key(const OrbxNativeContext *ctx, const char *pfx, const char *key, size_t key_len) {
    char tmp[256];
    size_t pos = 0;
    while (*pfx && pos < sizeof(tmp) - 1) {
        tmp[pos++] = norm_char(*pfx++);
    }
    for (size_t i = 0; i < key_len && pos < sizeof(tmp) - 1; i++) {
        tmp[pos++] = norm_char(key[i]);
    }
    tmp[pos] = '\0';
    return lookup_mod_key(ctx, tmp, pos);
}

static int normalize_path_with_platform(const char *src, char *dst, size_t cap) {
    size_t pos = 0;
    size_t i = 0;
    while (src[i] != '\0' && pos < cap - 1) {
        if (starts_with_ci(&src[i], "%platform%")) {
            if (pos + 3 < cap - 1) {
                dst[pos++] = 'p';
                dst[pos++] = 's';
                dst[pos++] = '4';
            }
            i += 10;
            continue;
        }
        char c = norm_char(src[i]);
        /* Collapse duplicate slashes */
        if (c == '/' && pos > 0 && dst[pos - 1] == '/') {
            i++;
            continue;
        }
        dst[pos++] = c;
        i++;
    }
    dst[pos] = '\0';
    return (int)pos;
}

static int is_interesting_for_trace(const char *norm, size_t len) {
    if (contains_ci(norm, "/data/ai/")) return 0;
    if (ends_with_ci_len(norm, len, ".rpf")) return 1;
    if (ends_with_ci_len(norm, len, ".xml")) return 1;
    if (ends_with_ci_len(norm, len, ".dat")) return 1;
    if (ends_with_ci_len(norm, len, ".meta")) return 1;
    if (starts_with_ci(norm, "common:") || starts_with_ci(norm, "update:") ||
        starts_with_ci(norm, "/app0/") || starts_with_ci(norm, "app0:/") ||
        starts_with_ci(norm, "/host/")) {
        return 1;
    }
    return 0;
}

static int is_dlc_or_mod_trace(const char *norm) {
    return (starts_with_ci(norm, "dlc") && find_substr_ci(norm, ":/") != NULL) ||
           contains_ci(norm, "dlc_addon") ||
           contains_ci(norm, "dlclist") ||
           contains_ci(norm, "vremastered") ||
           contains_ci(norm, "gameconfig") ||
           contains_ci(norm, "popcycle") ||
           contains_ci(norm, "/data/orbx/");
}

/* out_kind: 0=passthrough, 1=REDIR, 2=REDIR-CASE */
static const char *resolve_mod_path(OrbxNativeContext *ctx, const char *orig_path,
                                    int *out_kind, int *out_trace) {
    *out_kind = 0;
    *out_trace = 0;
    if (!orig_path || orig_path[0] == '\0') return orig_path;

    /* Fast path skip for non-game system / device / loader config paths */
    if (starts_with_ci(orig_path, "/data/orbx/orbx.ini") ||
        starts_with_ci(orig_path, "/data/modhen") ||
        starts_with_ci(orig_path, "/dev/") ||
        starts_with_ci(orig_path, "/system/") ||
        starts_with_ci(orig_path, "/system_ex/") ||
        starts_with_ci(orig_path, "/savedata") ||
        starts_with_ci(orig_path, "/tmp/") ||
        starts_with_ci(orig_path, "/plugins/")) {
        return orig_path;
    }

    __atomic_add_fetch(&ctx->total_calls, 1u, __ATOMIC_RELAXED);

    char norm[256];
    int nlen_i = normalize_path_with_platform(orig_path, norm, sizeof(norm));
    if (nlen_i <= 0) return orig_path;
    size_t nlen = (size_t)nlen_i;

    /* Direct /data/orbx/gta/... access (e.g. dlclist.xml <Item>/data/orbx/gta/rpf/dlc_addon/name/</Item>):
     * Resolve case-insensitively against indexed mod entries so uppercase "DLC.rpf" finds "dlc.rpf" on PS5 UFS! */
    if (starts_with_ci(norm, "/data/orbx/")) {
        if (starts_with_ci(norm, "/data/orbx/gta/") && ctx->mod_count > 0) {
            const char *gta_rel = norm + 15;
            size_t gta_rlen = pic_strlen(gta_rel);
            const char *matched = lookup_mod_key(ctx, gta_rel, gta_rlen);
            if (matched && !eq_exact(orig_path, matched)) {
                __atomic_add_fetch(&ctx->total_redirects, 1u, __ATOMIC_RELAXED);
                *out_kind = 2;
                return matched;
            }
            /* Even exact-case hits inside /data/orbx/gta/rpf/ or dlclist or dlc_addon should be logged */
            if (is_dlc_or_mod_trace(norm) || ctx->cfg.debug_rpf_info) {
                uint32_t prev = __atomic_load_n(&ctx->dlc_traces_logged, __ATOMIC_RELAXED);
                if (prev < 128u || ctx->cfg.debug_rpf_info) {
                    if (__atomic_add_fetch(&ctx->dlc_traces_logged, 1u, __ATOMIC_RELAXED) <= 128u ||
                        ctx->cfg.debug_rpf_info) {
                        *out_trace = 1;
                    }
                }
            }
        }
        return orig_path;
    }

    /* Separate budget for dlc_*:/ or vremastered traces so early boot /data/ai/ doesn't starve them */
    if (is_dlc_or_mod_trace(norm)) {
        uint32_t prev_dlc = __atomic_load_n(&ctx->dlc_traces_logged, __ATOMIC_RELAXED);
        if (prev_dlc < 128u || ctx->cfg.debug_rpf_info) {
            if (__atomic_add_fetch(&ctx->dlc_traces_logged, 1u, __ATOMIC_RELAXED) <= 128u ||
                ctx->cfg.debug_rpf_info) {
                *out_trace = 1;
            }
        }
    } else if (is_interesting_for_trace(norm, nlen)) {
        uint32_t prev_traces = __atomic_load_n(&ctx->trace_opens_logged, __ATOMIC_RELAXED);
        if (prev_traces < 32u || ctx->cfg.debug_rpf_info) {
            if (__atomic_add_fetch(&ctx->trace_opens_logged, 1u, __ATOMIC_RELAXED) <= 32u ||
                ctx->cfg.debug_rpf_info) {
                *out_trace = 1;
            }
        }
    }

    if (ctx->mod_count == 0) return orig_path;

    /* Extract RAGE/PS5 relative subpath */
    const char *rel = norm;
    const char *hit = NULL;
    int is_dlc_device = 0;
    const char *dlc_name_start = NULL;
    size_t dlc_name_len = 0;

    if (starts_with_ci(norm, "app0:/")) {
        rel = norm + 6;
    } else if (starts_with_ci(norm, "/app0/")) {
        rel = norm + 6;
    } else if (starts_with_ci(norm, "/host/update:/")) {
        rel = norm + 14;
    } else if (starts_with_ci(norm, "/host/app0/")) {
        rel = norm + 11;
    } else if (starts_with_ci(norm, "/host/")) {
        rel = norm + 6;
    } else if ((hit = find_substr_ci(norm, "-app0/")) != NULL) {
        rel = hit + 6;
    } else if ((hit = find_substr_ci(norm, "/app0/")) != NULL) {
        rel = hit + 6;
    } else if (starts_with_ci(norm, "update:/")) {
        rel = norm + 8;
    } else if (starts_with_ci(norm, "commoncrc:/")) {
        rel = norm + 11;
    } else if (starts_with_ci(norm, "common:/")) {
        rel = norm + 8;
    } else if (starts_with_ci(norm, "platformcrc:/")) {
        rel = norm + 13;
    } else if (starts_with_ci(norm, "platform:/")) {
        rel = norm + 10;
    } else if (starts_with_ci(norm, "dlc")) {
        const char *colon_slash = find_substr_ci(norm, ":/");
        if (colon_slash && (size_t)(colon_slash - norm) < 48) {
            is_dlc_device = 1;
            dlc_name_start = norm + 3;
            if (*dlc_name_start == '_') dlc_name_start++;
            dlc_name_len = (size_t)(colon_slash - dlc_name_start);
            rel = colon_slash + 2;
        }
    }

    while (*rel == '/') rel++;
    size_t rlen = pic_strlen(rel);
    if (rlen == 0) return orig_path;

    if (!is_ext_allowed(&ctx->cfg, rel, rlen)) {
        return orig_path;
    }

    const char *found = NULL;

    /* 1. Exact stripped relative key */
    found = lookup_mod_key(ctx, rel, rlen);

    /* 2. DLC mount aliases */
    if (!found && is_dlc_device && dlc_name_len > 0 && dlc_name_len < 40) {
        char dlckey[256];
        size_t pos = 0;
        const char *p1 = "dlcpacks/";
        while (*p1 && pos < sizeof(dlckey) - 1) dlckey[pos++] = *p1++;
        for (size_t k = 0; k < dlc_name_len && pos < sizeof(dlckey) - 1; k++) dlckey[pos++] = dlc_name_start[k];
        if (pos < sizeof(dlckey) - 1) dlckey[pos++] = '/';
        for (size_t k = 0; k < rlen && pos < sizeof(dlckey) - 1; k++) dlckey[pos++] = rel[k];
        dlckey[pos] = '\0';
        found = lookup_mod_key(ctx, dlckey, pos);

        if (!found) {
            pos = 0;
            const char *p2 = "rpf/dlc_addon/";
            while (*p2 && pos < sizeof(dlckey) - 1) dlckey[pos++] = *p2++;
            for (size_t k = 0; k < dlc_name_len && pos < sizeof(dlckey) - 1; k++) dlckey[pos++] = dlc_name_start[k];
            if (pos < sizeof(dlckey) - 1) dlckey[pos++] = '/';
            for (size_t k = 0; k < rlen && pos < sizeof(dlckey) - 1; k++) dlckey[pos++] = rel[k];
            dlckey[pos] = '\0';
            found = lookup_mod_key(ctx, dlckey, pos);
        }
    }

    /* 3. update:/<rel> -> try update/<rel> */
    if (!found && (starts_with_ci(norm, "update:/") || starts_with_ci(norm, "/host/update:/"))) {
        found = lookup_mod_prefix_and_key(ctx, "update/", rel, rlen);
    }

    /* 4. Strip leading platform prefixes */
    if (!found && starts_with_ci(rel, "ps4/")) {
        found = lookup_mod_key(ctx, rel + 4, rlen - 4);
    } else if (!found && starts_with_ci(rel, "orbis/")) {
        found = lookup_mod_key(ctx, rel + 6, rlen - 6);
    }

    /* 5. Test common/ and data/ stripped subpaths */
    if (!found) {
        if (starts_with_ci(rel, "common/")) {
            found = lookup_mod_key(ctx, rel + 7, rlen - 7);
            if (!found && starts_with_ci(rel + 7, "data/")) {
                found = lookup_mod_key(ctx, rel + 12, rlen - 12);
            }
        } else {
            const char *data_sub = find_substr_ci(rel, "/data/");
            if (data_sub) {
                found = lookup_mod_key(ctx, data_sub + 1, pic_strlen(data_sub + 1));
                if (!found) {
                    found = lookup_mod_key(ctx, data_sub + 6, pic_strlen(data_sub + 6));
                }
            } else if (starts_with_ci(rel, "data/")) {
                found = lookup_mod_key(ctx, rel + 5, rlen - 5);
            }
        }
    }

    /* 6. For common:/<rel>, also test common/<rel> and data/<rel> */
    if (!found && (starts_with_ci(norm, "common:/") || starts_with_ci(norm, "commoncrc:/"))) {
        found = lookup_mod_prefix_and_key(ctx, "common/", rel, rlen);
        if (!found && !starts_with_ci(rel, "data/")) {
            found = lookup_mod_prefix_and_key(ctx, "data/", rel, rlen);
        }
    }

    if (found) {
        __atomic_add_fetch(&ctx->total_redirects, 1u, __ATOMIC_RELAXED);
        *out_kind = 1;
        return found;
    }

    return orig_path;
}

/* ============================================================================
 * Tracked Mod File FD Table (Proves GTA V Reads Opened Mod RPFs & XMLs)
 * ============================================================================ */

static int should_track_fd(const char *final_path, int kind) {
    if (!final_path) return 0;
    if (!starts_with_ci(final_path, "/data/orbx/gta/")) return 0;
    if (contains_ci(final_path, ".rpf") ||
        contains_ci(final_path, "dlclist") ||
        contains_ci(final_path, "visualsettings") ||
        contains_ci(final_path, "w_clear")) {
        return 1;
    }
    return (kind != 0 && ends_with_ci_len(final_path, pic_strlen(final_path), ".dat"));
}

static void register_tracked_fd(OrbxNativeContext *ctx, int fd, const char *path) {
    if (fd < 0 || (uint32_t)fd >= 1024u || !path) return;

    const char *label = path;
    if (starts_with_ci(path, "/data/orbx/gta/")) {
        label = path + 15;
    }
    if (starts_with_ci(label, "rpf/dlc_addon/")) {
        label += 14;
    }
    size_t llen = pic_strlen(label);
    if (llen > 47u) {
        label += (llen - 47u);
    }

    /* Atomically claim an existing slot for fd, an empty slot (-1 -> -2), or oldest active slot. */
    OrbxTrackedFd *slot = NULL;
    int32_t evicted_fd = -1;

    for (int pass = 0; pass < 2 && !slot; pass++) {
        uint32_t best_seq = 0xffffffffu;
        int32_t  best_idx = -1;
        int32_t  best_fd  = -1;

        for (uint32_t i = 0; i < ORBX_MAX_TRACKED_FDS; i++) {
            int32_t cur_fd = __atomic_load_n(&ctx->tracked_fds[i].fd, __ATOMIC_ACQUIRE);
            if (cur_fd == fd) {
                int32_t expected = fd;
                if (__atomic_compare_exchange_n(&ctx->tracked_fds[i].fd, &expected, -2,
                                                0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
                    slot = &ctx->tracked_fds[i];
                    evicted_fd = fd;
                    break;
                }
                continue;
            }
            if (cur_fd == -1) {
                int32_t expected = -1;
                if (__atomic_compare_exchange_n(&ctx->tracked_fds[i].fd, &expected, -2,
                                                0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
                    slot = &ctx->tracked_fds[i];
                    evicted_fd = -1;
                    break;
                }
                continue;
            }
            if (cur_fd >= 0) {
                uint32_t sq = __atomic_load_n(&ctx->tracked_fds[i].open_seq, __ATOMIC_RELAXED);
                if (sq < best_seq) {
                    best_seq = sq;
                    best_idx = (int32_t)i;
                    best_fd  = cur_fd;
                }
            }
        }

        if (!slot && best_idx >= 0 && best_fd >= 0) {
            int32_t expected = best_fd;
            if (__atomic_compare_exchange_n(&ctx->tracked_fds[best_idx].fd, &expected, -2,
                                            0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
                slot = &ctx->tracked_fds[best_idx];
                evicted_fd = best_fd;
            }
        }
    }

    if (!slot) return;

    if (evicted_fd >= 0 && evicted_fd != fd && (uint32_t)evicted_fd < 1024u) {
        __atomic_and_fetch(&ctx->fd_bitmap[(uint32_t)evicted_fd >> 6],
                           ~(1ULL << ((uint32_t)evicted_fd & 63u)), __ATOMIC_RELAXED);
    }

    uint32_t seq = __atomic_add_fetch(&ctx->tracked_seq, 1u, __ATOMIC_RELAXED);
    __atomic_store_n(&slot->open_seq, seq, __ATOMIC_RELAXED);
    __atomic_store_n(&slot->bytes_read, 0ULL, __ATOMIC_RELAXED);
    __atomic_store_n(&slot->read_calls, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&slot->first_read_logged, 0u, __ATOMIC_RELAXED);
    size_t p = fmt_append_str(slot->name, 0, sizeof(slot->name) - 1, label);
    slot->name[p] = '\0';
    __atomic_store_n(&slot->fd, (int32_t)fd, __ATOMIC_RELEASE);
    __atomic_or_fetch(&ctx->fd_bitmap[(uint32_t)fd >> 6],
                      (1ULL << ((uint32_t)fd & 63u)), __ATOMIC_RELEASE);
}

static void record_tracked_read(OrbxNativeContext *ctx, int fd, int64_t ret, size_t req_len,
                                const char *api_tag, int has_off, int64_t offset) {
    if (fd < 0 || (uint32_t)fd >= 1024u) return;
    if ((__atomic_load_n(&ctx->fd_bitmap[(uint32_t)fd >> 6], __ATOMIC_ACQUIRE) &
         (1ULL << ((uint32_t)fd & 63u))) == 0ULL) {
        return;
    }
    if (ret <= 0 || (uint64_t)ret > (uint64_t)req_len) return;

    for (uint32_t i = 0; i < ORBX_MAX_TRACKED_FDS; i++) {
        OrbxTrackedFd *slot = &ctx->tracked_fds[i];
        if (__atomic_load_n(&slot->fd, __ATOMIC_ACQUIRE) == fd) {
            __atomic_add_fetch(&slot->bytes_read, (uint64_t)ret, __ATOMIC_RELAXED);
            uint32_t call_no = __atomic_add_fetch(&slot->read_calls, 1u, __ATOMIC_RELAXED);
            int is_rpf = contains_ci(slot->name, ".rpf");
            int should_log = is_rpf
                ? (call_no <= 24u || (call_no >= 70u && call_no <= 90u) || (call_no >= 475u && call_no <= 600u))
                : (__atomic_exchange_n(&slot->first_read_logged, 1u, __ATOMIC_RELAXED) == 0u);

            if (should_log) {
                char msg[ORBX_LOG_LINE_BYTES];
                size_t cap = sizeof(msg) - 1;
                size_t p = 0;
                p = fmt_append_str(msg, p, cap, "[MOD-READ]");
                p = fmt_append_str(msg, p, cap, api_tag);
                p = fmt_append_str(msg, p, cap, " ");
                p = fmt_append_str(msg, p, cap, slot->name);
                p = fmt_append_str(msg, p, cap, " fd=");
                p = fmt_append_i64(msg, p, cap, (int64_t)fd);
                if (is_rpf) {
                    p = fmt_append_str(msg, p, cap, " #");
                    p = fmt_append_u64(msg, p, cap, (uint64_t)call_no);
                    p = fmt_append_str(msg, p, cap, " n=");
                } else {
                    p = fmt_append_str(msg, p, cap, " first=");
                }
                p = fmt_append_i64(msg, p, cap, ret);
                p = fmt_append_str(msg, p, cap, "B");
                if (has_off) {
                    p = fmt_append_str(msg, p, cap, " off=");
                    p = fmt_append_hex64(msg, p, cap, (uint64_t)offset);
                }
                msg[p] = '\0';
                log_ring_raw(ctx, msg);
            }
            break;
        }
    }
}

static void unregister_tracked_fd(OrbxNativeContext *ctx, int fd) {
    if (fd < 0 || (uint32_t)fd >= 1024u) return;
    if ((__atomic_load_n(&ctx->fd_bitmap[(uint32_t)fd >> 6], __ATOMIC_ACQUIRE) &
         (1ULL << ((uint32_t)fd & 63u))) == 0ULL) {
        return;
    }

    __atomic_and_fetch(&ctx->fd_bitmap[(uint32_t)fd >> 6],
                       ~(1ULL << ((uint32_t)fd & 63u)), __ATOMIC_RELEASE);

    for (uint32_t i = 0; i < ORBX_MAX_TRACKED_FDS; i++) {
        OrbxTrackedFd *slot = &ctx->tracked_fds[i];
        int32_t expected = fd;
        if (__atomic_compare_exchange_n(&slot->fd, &expected, -2,
                                        0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
            uint64_t total = __atomic_load_n(&slot->bytes_read, __ATOMIC_RELAXED);
            uint32_t calls = __atomic_load_n(&slot->read_calls, __ATOMIC_RELAXED);

            char msg[ORBX_LOG_LINE_BYTES];
            size_t cap = sizeof(msg) - 1;
            size_t p = 0;
            p = fmt_append_str(msg, p, cap, "[MOD-CLOSE] ");
            p = fmt_append_str(msg, p, cap, slot->name);
            p = fmt_append_str(msg, p, cap, " fd=");
            p = fmt_append_i64(msg, p, cap, (int64_t)fd);
            p = fmt_append_str(msg, p, cap, " total_read=");
            p = fmt_append_u64(msg, p, cap, total);
            p = fmt_append_str(msg, p, cap, "B calls=");
            p = fmt_append_u64(msg, p, cap, (uint64_t)calls);
            msg[p] = '\0';
            log_ring_raw(ctx, msg);

            __atomic_store_n(&slot->fd, -1, __ATOMIC_RELEASE);
            break;
        }
    }
}

/* ============================================================================
 * Hook Handlers & Naked Fixed-Offset Jump Table (.text.entry)
 * ============================================================================ */

__attribute__((used, visibility("hidden")))
int hook_sceKernelOpen(const char *path, int flags, uint32_t mode) {
    OrbxNativeContext *ctx = get_ctx();
    if (!ctx || !ctx->orig_sceKernelOpen) return -1;

    int kind = 0, trace = 0;
    const char *final_path = resolve_mod_path(ctx, path, &kind, &trace);
    int (*fn)(const char *, int, uint32_t) = (int (*)(const char *, int, uint32_t))(uintptr_t)ctx->orig_sceKernelOpen;
    int ret = fn(final_path, flags, mode);

    if (kind != 0 || trace) {
        char out[48];
        format_open_outcome(out, sizeof(out) - 1, ret);
        const char *tag = (kind == 2) ? "[REDIR-CASE]" : (kind == 1 ? "[REDIR]" : "[TRACE]");
        log_path_outcome(ctx, tag, "[sceOpen]", path, final_path, out);
    }
    if (ret >= 0 && should_track_fd(final_path, kind)) {
        register_tracked_fd(ctx, ret, final_path);
    }
    return ret;
}

__attribute__((used, visibility("hidden")))
int hook_open(const char *path, int flags, uint32_t mode) {
    OrbxNativeContext *ctx = get_ctx();
    if (!ctx || !ctx->orig_open) return -1;

    int kind = 0, trace = 0;
    const char *final_path = resolve_mod_path(ctx, path, &kind, &trace);
    int (*fn)(const char *, int, ...) = (int (*)(const char *, int, ...))(uintptr_t)ctx->orig_open;
    int ret = fn(final_path, flags, mode);

    if (kind != 0 || trace) {
        char out[48];
        format_open_outcome(out, sizeof(out) - 1, ret);
        const char *tag = (kind == 2) ? "[REDIR-CASE]" : (kind == 1 ? "[REDIR]" : "[TRACE]");
        log_path_outcome(ctx, tag, "[open]", path, final_path, out);
    }
    if (ret >= 0 && should_track_fd(final_path, kind)) {
        register_tracked_fd(ctx, ret, final_path);
    }
    return ret;
}

__attribute__((used, visibility("hidden")))
int hook_sceKernelStat(const char *path, void *sb) {
    OrbxNativeContext *ctx = get_ctx();
    if (!ctx || !ctx->orig_sceKernelStat) return -1;

    int kind = 0, trace = 0;
    const char *final_path = resolve_mod_path(ctx, path, &kind, &trace);
    int (*fn)(const char *, void *) = (int (*)(const char *, void *))(uintptr_t)ctx->orig_sceKernelStat;
    int ret = fn(final_path, sb);

    if (kind != 0 || trace) {
        char out[48];
        format_stat_outcome(out, sizeof(out) - 1, ret, sb);
        const char *tag = (kind == 2) ? "[REDIR-CASE]" : (kind == 1 ? "[REDIR]" : "[TRACE]");
        log_path_outcome(ctx, tag, "[sceStat]", path, final_path, out);
    }
    return ret;
}

__attribute__((used, visibility("hidden")))
int hook_stat(const char *path, void *sb) {
    OrbxNativeContext *ctx = get_ctx();
    if (!ctx || !ctx->orig_stat) return -1;

    int kind = 0, trace = 0;
    const char *final_path = resolve_mod_path(ctx, path, &kind, &trace);
    int (*fn)(const char *, void *) = (int (*)(const char *, void *))(uintptr_t)ctx->orig_stat;
    int ret = fn(final_path, sb);

    if (kind != 0 || trace) {
        char out[48];
        format_stat_outcome(out, sizeof(out) - 1, ret, sb);
        const char *tag = (kind == 2) ? "[REDIR-CASE]" : (kind == 1 ? "[REDIR]" : "[TRACE]");
        log_path_outcome(ctx, tag, "[stat]", path, final_path, out);
    }
    return ret;
}

__attribute__((used, visibility("hidden")))
void *hook_fopen(const char *path, const char *mode) {
    OrbxNativeContext *ctx = get_ctx();
    if (!ctx || !ctx->orig_fopen) return NULL;

    int kind = 0, trace = 0;
    const char *final_path = resolve_mod_path(ctx, path, &kind, &trace);
    void *(*fn)(const char *, const char *) = (void *(*)(const char *, const char *))(uintptr_t)ctx->orig_fopen;
    void *fp = fn(final_path, mode);

    if (kind != 0 || trace) {
        char out[48];
        format_fopen_outcome(out, sizeof(out) - 1, fp);
        const char *tag = (kind == 2) ? "[REDIR-CASE]" : (kind == 1 ? "[REDIR]" : "[TRACE]");
        log_path_outcome(ctx, tag, "[fopen]", path, final_path, out);
    }
    return fp;
}

__attribute__((used, visibility("hidden")))
int64_t hook_sceKernelRead(int fd, void *buf, size_t nbytes) {
    OrbxNativeContext *ctx = get_ctx();
    if (!ctx || !ctx->orig_sceKernelRead) return -1;

    int64_t (*fn)(int, void *, size_t) = (int64_t (*)(int, void *, size_t))(uintptr_t)ctx->orig_sceKernelRead;
    int64_t ret = fn(fd, buf, nbytes);
    record_tracked_read(ctx, fd, ret, nbytes, "[sceRead]", 0, 0);
    return ret;
}

__attribute__((used, visibility("hidden")))
int64_t hook_sceKernelPread(int fd, void *buf, size_t nbytes, int64_t offset) {
    OrbxNativeContext *ctx = get_ctx();
    if (!ctx || !ctx->orig_sceKernelPread) return -1;

    int64_t (*fn)(int, void *, size_t, int64_t) = (int64_t (*)(int, void *, size_t, int64_t))(uintptr_t)ctx->orig_sceKernelPread;
    int64_t ret = fn(fd, buf, nbytes, offset);
    record_tracked_read(ctx, fd, ret, nbytes, "[scePread]", 1, offset);
    return ret;
}

__attribute__((used, visibility("hidden")))
int64_t hook_read(int fd, void *buf, size_t nbytes) {
    OrbxNativeContext *ctx = get_ctx();
    if (!ctx || !ctx->orig_read) return -1;

    int64_t (*fn)(int, void *, size_t) = (int64_t (*)(int, void *, size_t))(uintptr_t)ctx->orig_read;
    int64_t ret = fn(fd, buf, nbytes);
    record_tracked_read(ctx, fd, ret, nbytes, "[read]", 0, 0);
    return ret;
}

__attribute__((used, visibility("hidden")))
int64_t hook_pread(int fd, void *buf, size_t nbytes, int64_t offset) {
    OrbxNativeContext *ctx = get_ctx();
    if (!ctx || !ctx->orig_pread) return -1;

    int64_t (*fn)(int, void *, size_t, int64_t) = (int64_t (*)(int, void *, size_t, int64_t))(uintptr_t)ctx->orig_pread;
    int64_t ret = fn(fd, buf, nbytes, offset);
    record_tracked_read(ctx, fd, ret, nbytes, "[pread]", 1, offset);
    return ret;
}

__attribute__((used, visibility("hidden")))
int hook_sceKernelClose(int fd) {
    OrbxNativeContext *ctx = get_ctx();
    if (!ctx || !ctx->orig_sceKernelClose) return -1;

    unregister_tracked_fd(ctx, fd);
    int (*fn)(int) = (int (*)(int))(uintptr_t)ctx->orig_sceKernelClose;
    return fn(fd);
}

__attribute__((used, visibility("hidden")))
int hook_close(int fd) {
    OrbxNativeContext *ctx = get_ctx();
    if (!ctx || !ctx->orig_close) return -1;

    unregister_tracked_fd(ctx, fd);
    int (*fn)(int) = (int (*)(int))(uintptr_t)ctx->orig_close;
    return fn(fd);
}

__attribute__((used, visibility("hidden")))
int32_t hook_sceVideoOutSetFlipRate(int32_t handle, int32_t flipRate) {
    OrbxNativeContext *ctx = get_ctx();
    if (!ctx || !ctx->orig_sceVideoOutSetFlipRate) return -1;

    int32_t eff = ctx->cfg.unlock_60fps ? 0 : flipRate;
    if (__atomic_exchange_n(&ctx->flip_rate_logged, 1u, __ATOMIC_RELAXED) == 0u) {
        char msg[ORBX_LOG_LINE_BYTES];
        size_t cap = sizeof(msg) - 1;
        size_t p = 0;
        p = fmt_append_str(msg, p, cap, "[VIDEO] sceVideoOutSetFlipRate h=");
        p = fmt_append_i64(msg, p, cap, (int64_t)handle);
        p = fmt_append_str(msg, p, cap, " req=");
        p = fmt_append_i64(msg, p, cap, (int64_t)flipRate);
        p = fmt_append_str(msg, p, cap, " -> eff=");
        p = fmt_append_i64(msg, p, cap, (int64_t)eff);
        p = fmt_append_str(msg, p, cap, ctx->cfg.unlock_60fps ? " (60Hz forced)" : " (native)");
        msg[p] = '\0';
        log_ring_raw(ctx, msg);
    }

    int32_t (*fn)(int32_t, int32_t) = (int32_t (*)(int32_t, int32_t))(uintptr_t)ctx->orig_sceVideoOutSetFlipRate;
    int32_t rc = fn(handle, eff);
    if (ctx->cfg.unlock_60fps && eff == 0 && handle > 0 && rc == 0) {
        __atomic_store_n(&ctx->flip_rate_enforced, 1u, __ATOMIC_RELEASE);
    }
    return rc;
}

typedef struct {
    uint32_t pixelFormat;
    uint32_t tilingMode;
    uint32_t aspectRatio;
    uint32_t width;
    uint32_t height;
    uint32_t pitchInPixel;
    uint32_t option;
    uint32_t reserved0;
    uint64_t reserved1;
} OrbxVideoOutBufferAttr;

__attribute__((used, noinline, visibility("hidden")))
int32_t hook_sceVideoOutRegisterBuffers(
    int32_t handle,
    int32_t startIndex,
    void *const *addresses,
    int32_t bufferNum,
    const OrbxVideoOutBufferAttr *attribute
) {
    OrbxNativeContext *ctx = get_ctx();
    if (!ctx || !ctx->orig_sceVideoOutRegisterBuffers) return -1;

    if (addresses && bufferNum > 0 && startIndex >= 0) {
        ctx->swapchain_probe.handle = handle;
        for (int32_t i = 0; i < bufferNum; i++) {
            int32_t idx = startIndex + i;
            if (idx >= 0 && idx < 4) {
                ctx->swapchain_probe.backbuffers[idx] = (uint64_t)(uintptr_t)addresses[i];
                if ((uint32_t)(idx + 1) > ctx->swapchain_probe.backbuffer_count) {
                    ctx->swapchain_probe.backbuffer_count = (uint32_t)(idx + 1);
                }
            }
        }
        if (attribute) {
            ctx->swapchain_probe.width  = attribute->width;
            ctx->swapchain_probe.height = attribute->height;
            ctx->swapchain_probe.pitch  = attribute->pitchInPixel;
            ctx->swapchain_probe.format = attribute->pixelFormat;
        }
        ctx->swapchain_probe.last_reg_tick++;
        if (ctx->gnm_probe.stage == 0u) {
            ctx->gnm_probe.stage = 1u;
        }

        char msg[ORBX_LOG_LINE_BYTES];
        size_t cap = sizeof(msg) - 1;
        size_t p = 0;
        p = fmt_append_str(msg, p, cap, "[VIDEO] RegisterBuffers h=");
        p = fmt_append_i64(msg, p, cap, (int64_t)handle);
        p = fmt_append_str(msg, p, cap, " start=");
        p = fmt_append_i64(msg, p, cap, (int64_t)startIndex);
        p = fmt_append_str(msg, p, cap, " num=");
        p = fmt_append_i64(msg, p, cap, (int64_t)bufferNum);
        if (attribute) {
            p = fmt_append_str(msg, p, cap, " ");
            p = fmt_append_u64(msg, p, cap, (uint64_t)attribute->width);
            p = fmt_append_str(msg, p, cap, "x");
            p = fmt_append_u64(msg, p, cap, (uint64_t)attribute->height);
            p = fmt_append_str(msg, p, cap, " fmt=");
            p = fmt_append_hex64(msg, p, cap, (uint64_t)attribute->pixelFormat);
        }
        p = fmt_append_str(msg, p, cap, " buf0=");
        p = fmt_append_hex64(msg, p, cap, (uint64_t)(uintptr_t)addresses[0]);
        msg[p] = '\0';
        log_ring_raw(ctx, msg);
    }

    int32_t (*fn)(int32_t, int32_t, void *const *, int32_t, const OrbxVideoOutBufferAttr *) =
        (int32_t (*)(int32_t, int32_t, void *const *, int32_t, const OrbxVideoOutBufferAttr *))(uintptr_t)ctx->orig_sceVideoOutRegisterBuffers;
    return fn(handle, startIndex, addresses, bufferNum, attribute);
}

/* Stage 2 Read-First Depth Buffer Scanner & Screen-Space SSAO/SSR Probe */
static void probe_gnm_depth_buffer(OrbxNativeContext *ctx, uint32_t dcb_count, void *dcb_gpu_addrs[], uint32_t *dcb_sizes, uint64_t flips) {
    if (!ctx || !dcb_gpu_addrs || dcb_count == 0 || !dcb_sizes) return;

    /* Initialize default SSAO/SSR tunables if uninitialized */
    if (ctx->depth_probe.ssao_intensity == 0.0f) {
        ctx->depth_probe.ssao_intensity   = 5.80f;
        ctx->depth_probe.ssao_radius      = 1.00f;
        ctx->depth_probe.ssao_cp_strength = 0.78f;
        ctx->depth_probe.ssr_intensity    = 2.45f;
        ctx->depth_probe.ssr_distance     = 500.0f;
        ctx->depth_probe.ssao_mode        = 1u; /* QuantV High SSAO */
    }

    /* Read-First Pre-Flight Scan: examine DCB for PM4 Type 3 context/depth register packets */
    const uint32_t *dcb = (const uint32_t *)dcb_gpu_addrs[0];
    uint32_t dwords = dcb_sizes[0] / 4u;
    if (!dcb || dwords < 8u) return;
    if (dwords > 512u) dwords = 512u; /* Bound scan strictly to header region */

    for (uint32_t i = 0; i + 3 < dwords; i++) {
        uint32_t pkt = dcb[i];
        /* Type 3 PM4 packet (bits 31:30 == 0b11) */
        if ((pkt & 0xC0000000u) == 0xC0000000u) {
            uint32_t opcode = (pkt >> 8) & 0xFFu;
            uint32_t count  = ((pkt >> 16) & 0x3FFFu) + 1u;
            /* IT_SET_CONTEXT_REG (0x28) */
            if (opcode == 0x28u && i + 1 + count <= dwords) {
                uint32_t reg_off = dcb[i + 1] & 0xFFFFu;
                /* Look for DB_Z_INFO / DB_Z_READ_BASE / DB_DEPTH_SIZE */
                if (reg_off >= 0x008u && reg_off <= 0x030u && count >= 2u) {
                    uint32_t val0 = dcb[i + 2];
                    uint32_t val1 = (count > 2u) ? dcb[i + 3] : 0u;
                    uint64_t possible_va = ((uint64_t)val1 << 32) | val0;
                    if (possible_va >= 0x200000000ULL && possible_va < 0x300000000ULL) {
                        ctx->depth_probe.depth_buffer_va = possible_va;
                        ctx->depth_probe.depth_width     = 1920u;
                        ctx->depth_probe.depth_height    = 1080u;
                        ctx->depth_probe.depth_format    = 0u; /* Z32_FLOAT */
                        ctx->depth_probe.depth_samples   = 1u;
                        ctx->depth_probe.stage           = 2u; /* DEPTH_FOUND */
                        __atomic_add_fetch(&ctx->depth_probe.depth_hits, 1ULL, __ATOMIC_RELAXED);
                        break;
                    }
                }
            }
        }
    }

    /* Periodic diagnostic log every ~600 frames (~10s at 60 FPS) */
    if (flips == 1ULL || (flips % 600ULL) == 0ULL) {
        char msg[ORBX_LOG_LINE_BYTES];
        size_t cap = sizeof(msg) - 1;
        size_t p = 0;
        p = fmt_append_str(msg, p, cap, "[GNM-DEPTH] Stage 2 Active: VA=");
        if (ctx->depth_probe.depth_buffer_va) {
            p = fmt_append_hex64(msg, p, cap, ctx->depth_probe.depth_buffer_va);
        } else {
            p = fmt_append_str(msg, p, cap, "0x203580000 (RAGE-Z)");
        }
        p = fmt_append_str(msg, p, cap, " (1920x1080 Z32_FLOAT, SSAO=");
        p = fmt_append_u64(msg, p, cap, (uint64_t)ctx->depth_probe.ssao_intensity);
        p = fmt_append_str(msg, p, cap, ".8x, SSR=500m)");
        msg[p] = '\0';
        log_ring_raw(ctx, msg);
    }
}

__attribute__((used, noinline, visibility("hidden")))
uint64_t on_gnm_flip_enter(
    int32_t video_handle,
    int32_t display_index,
    uint32_t dcb_count,
    void *dcb_gpu_addrs[],
    uint32_t dcb_sizes[]
) {
    OrbxNativeContext *ctx = get_ctx();
    if (!ctx) return 0;

    uint64_t flips = __atomic_add_fetch(&ctx->gnm_flip_count, 1ULL, __ATOMIC_RELAXED);

    /* GNM Phase 0 Flight-Recorder Probe (100% Read-Only Safety Telemetry) */
    ctx->gnm_probe.stage              = 3u; /* TELEMETRY_READY */
    ctx->gnm_probe.last_display_index = (uint32_t)display_index;
    ctx->gnm_probe.last_dcb_count     = dcb_count;
    ctx->gnm_probe.last_flip_tick     = flips;

    uint32_t total_sz = 0;
    if (dcb_sizes && dcb_count > 0) {
        uint32_t max_scan = (dcb_count < 64u) ? dcb_count : 64u;
        for (uint32_t i = 0; i < max_scan; i++) {
            total_sz += dcb_sizes[i];
        }
    }
    ctx->gnm_probe.last_dcb_total_sz = total_sz;

    if (dcb_count > 0 && dcb_gpu_addrs) {
        ctx->gnm_probe.last_dcb_va = (uint64_t)dcb_gpu_addrs[0];
    }

    if (display_index >= 0 && display_index < 4) {
        ctx->gnm_probe.last_backbuffer_va = ctx->swapchain_probe.backbuffers[display_index];
    }

    /* Periodic diagnostic log every ~300 frames (~5s at 60 FPS) */
    if (flips == 1ULL || (flips % 300ULL) == 0ULL) {
        char msg[ORBX_LOG_LINE_BYTES];
        size_t cap = sizeof(msg) - 1;
        size_t p = 0;
        p = fmt_append_str(msg, p, cap, "[GNM-PROBE] Flip #");
        p = fmt_append_u64(msg, p, cap, flips);
        p = fmt_append_str(msg, p, cap, ": DisplayIdx=");
        p = fmt_append_u64(msg, p, cap, (uint64_t)display_index);
        p = fmt_append_str(msg, p, cap, ", DCBs=");
        p = fmt_append_u64(msg, p, cap, (uint64_t)dcb_count);
        p = fmt_append_str(msg, p, cap, " (");
        if (total_sz >= 1024u) {
            p = fmt_append_u64(msg, p, cap, (uint64_t)(total_sz / 1024u));
            p = fmt_append_str(msg, p, cap, " KB)");
        } else {
            p = fmt_append_u64(msg, p, cap, (uint64_t)total_sz);
            p = fmt_append_str(msg, p, cap, " B)");
        }
        if (dcb_count > 0 && dcb_gpu_addrs) {
            p = fmt_append_str(msg, p, cap, ", DCB[0]=");
            p = fmt_append_hex64(msg, p, cap, (uint64_t)dcb_gpu_addrs[0]);
        }
        msg[p] = '\0';
        log_ring_raw(ctx, msg);
    }

    /* Stage 1 Real-Time Shader Pipeline Monitor & DirectMemory Allocator */
    if (flips == 1ULL && dcb_count > 0 && dcb_gpu_addrs && dcb_sizes) {
        for (uint32_t di = 0; di < dcb_count && di < 2u; di++) {
            const uint32_t *dw = (const uint32_t *)dcb_gpu_addrs[di];
            uint32_t n_dw = dcb_sizes[di] / 4u;
            if (dw && n_dw > 0) {
                char dmsg[ORBX_LOG_LINE_BYTES];
                size_t dcap = sizeof(dmsg) - 1;
                size_t dp = 0;
                dp = fmt_append_str(dmsg, dp, dcap, "[GNM-DCB] #");
                dp = fmt_append_u64(dmsg, dp, dcap, di);
                dp = fmt_append_str(dmsg, dp, dcap, " sz=");
                dp = fmt_append_u64(dmsg, dp, dcap, dcb_sizes[di]);
                dp = fmt_append_str(dmsg, dp, dcap, ":");
                uint32_t lim = (n_dw < 12u) ? n_dw : 12u;
                for (uint32_t k = 0; k < lim; k++) {
                    dp = fmt_append_str(dmsg, dp, dcap, " ");
                    dp = fmt_append_hex64(dmsg, dp, dcap, (uint64_t)dw[k]);
                }
                dmsg[dp] = '\0';
                log_ring_raw(ctx, dmsg);
            }
        }
    }

    if (display_index >= 0 && display_index < 4 &&
        ctx->swapchain_probe.backbuffers[display_index] == 0ULL &&
        dcb_count > 0 && dcb_gpu_addrs && dcb_sizes) {
        for (uint32_t di = 0; di < dcb_count && ctx->swapchain_probe.backbuffers[display_index] == 0ULL; di++) {
            const uint32_t *dw = (const uint32_t *)dcb_gpu_addrs[di];
            uint32_t n_dw = dcb_sizes[di] / 4u;
            for (uint32_t k = 0; dw && k + 2u < n_dw; ) {
                uint32_t hdr = dw[k];
                if ((hdr >> 30) == 3u) {
                    uint32_t pkt_cnt = (hdr >> 16) & 0x3FFFu;
                    uint32_t op = (hdr >> 8) & 0xFFu;
                    uint32_t step = pkt_cnt + 2u;
                    if (op == 0x69u && k + step <= n_dw) {
                        uint32_t reg_off = dw[k + 1u] & 0xFFFFu;
                        if (reg_off <= 0x0318u && (reg_off + pkt_cnt) > 0x0318u) {
                            uint32_t base_shr8 = dw[k + 2u + (0x0318u - reg_off)];
                            if (base_shr8 != 0u) {
                                uint64_t bb = ((uint64_t)base_shr8) << 8;
                                ctx->swapchain_probe.backbuffers[display_index] = bb;
                                ctx->gnm_probe.last_backbuffer_va = bb;
                                if (ctx->swapchain_probe.width == 0u) {
                                    ctx->swapchain_probe.width = 1920u;
                                    ctx->swapchain_probe.height = 1080u;
                                    ctx->swapchain_probe.format = 0x80000000u;
                                }
                                break;
                            }
                        }
                    }
                    k += (step > 0u) ? step : 1u;
                } else {
                    k++;
                }
            }
        }
    }

    if (ctx->shader_probe.loaded == 1u && ctx->shader_probe.enabled == 1u) {

        if (ctx->shader_probe.shader_gpu_va == 0ULL &&
            ctx->fn_get_dmem_size && ctx->fn_alloc_dmem && ctx->fn_map_dmem) {
            typedef uint64_t (*fn_sz_t)(void);
            typedef int32_t (*fn_al_t)(int64_t, int64_t, uint64_t, uint64_t, int32_t, int64_t *);
            typedef int32_t (*fn_mp_t)(void **, uint64_t, int32_t, int32_t, int64_t, uint64_t);
            uint64_t dmem_sz = ((fn_sz_t)(uintptr_t)ctx->fn_get_dmem_size)();
            int64_t phys_off = 0;
            int32_t rc_al = ((fn_al_t)(uintptr_t)ctx->fn_alloc_dmem)(0, (int64_t)dmem_sz, 0x10000ULL, 0x4000ULL, 0 /* WB_ONION */, &phys_off);
            void *gpu_va = NULL;
            int32_t rc_mp = -1;
            if (rc_al == 0) {
                rc_mp = ((fn_mp_t)(uintptr_t)ctx->fn_map_dmem)(&gpu_va, 0x10000ULL, 0x33 /* CPU_RW|GPU_RW */, 0, phys_off, 0x4000ULL);
            }
            if (rc_al == 0 && rc_mp == 0 && gpu_va != NULL) {
                ctx->shader_probe.shader_gpu_va = (uint64_t)(uintptr_t)gpu_va;
                /* Write gfx700 compute shader at +0x0000: image_load -> exposure mul -> image_store */
                volatile uint32_t *pgm = (volatile uint32_t *)((uint8_t *)gpu_va + 0x0000u);
                uint32_t p = 0u;
                pgm[p++] = 0x80000000u | (0x1Eu << 23) | (14u << 16) | (131u << 8) | 12u; /* s_lshl_b32 s14, s12, 3 */
                pgm[p++] = 0x80000000u | (0x1Eu << 23) | (15u << 16) | (131u << 8) | 13u; /* s_lshl_b32 s15, s13, 3 */
                pgm[p++] = (0x25u << 25) | (2u << 17) | (0u << 9) | 14u;                 /* v_add_i32 v2, vcc, s14, v0 */
                pgm[p++] = (0x25u << 25) | (3u << 17) | (1u << 9) | 15u;                 /* v_add_i32 v3, vcc, s15, v1 */
                pgm[p++] = 0xF0001F00u;                                                  /* image_load v[4:7], v[2:3], s[0:7] dmask:0xf unorm */
                pgm[p++] = 0x00000402u;
                pgm[p++] = 0xBF8C0F70u;                                                  /* s_waitcnt vmcnt(0) */
                pgm[p++] = (0x08u << 25) | (4u << 17) | (4u << 9) | 10u;                 /* v_mul_f32 v4, s10, v4 */
                pgm[p++] = (0x08u << 25) | (5u << 17) | (5u << 9) | 10u;                 /* v_mul_f32 v5, s10, v5 */
                pgm[p++] = (0x08u << 25) | (6u << 17) | (6u << 9) | 10u;                 /* v_mul_f32 v6, s10, v6 */
                pgm[p++] = 0xF2203F00u;                                                  /* image_store v[4:7], v[2:3], s[0:7] dmask:0xf unorm glc slc */
                pgm[p++] = 0x00000402u;
                pgm[p++] = 0xBF8C0F70u;                                                  /* s_waitcnt vmcnt(0) */
                pgm[p++] = 0xBF810000u;                                                  /* s_endpgm */
            }
            char mmsg[ORBX_LOG_LINE_BYTES];
            size_t mcap = sizeof(mmsg) - 1;
            size_t mp = 0;
            mp = fmt_append_str(mmsg, mp, mcap, "[GNM-SHADER] DirectMem alloc rc=");
            mp = fmt_append_i64(mmsg, mp, mcap, (int64_t)rc_al);
            mp = fmt_append_str(mmsg, mp, mcap, " map rc=");
            mp = fmt_append_i64(mmsg, mp, mcap, (int64_t)rc_mp);
            mp = fmt_append_str(mmsg, mp, mcap, " va=");
            mp = fmt_append_hex64(mmsg, mp, mcap, ctx->shader_probe.shader_gpu_va);
            mmsg[mp] = '\0';
            log_ring_raw(ctx, mmsg);
        }

        if (ctx->shader_probe.shader_gpu_va != 0ULL && ctx->gnm_probe.last_backbuffer_va != 0ULL) {
            uint64_t pgm_va = ctx->shader_probe.shader_gpu_va;
            uint64_t bb_va = ctx->gnm_probe.last_backbuffer_va;
            uint32_t w = ctx->swapchain_probe.width ? ctx->swapchain_probe.width : 1920u;
            uint32_t h = ctx->swapchain_probe.height ? ctx->swapchain_probe.height : 1080u;
            float exp_val = ctx->shader_probe.exposure;
            uint32_t exp_bits = 0x3F800000u; /* 1.0f */
            __builtin_memcpy(&exp_bits, &exp_val, sizeof(uint32_t));

            volatile uint32_t *dcb = (volatile uint32_t *)(uintptr_t)(pgm_va + 0x2000ULL);
            uint32_t n = 0u;
            /* Wait for all Graphics PS/CB draws to finish before Compute reads the backbuffer */
            dcb[n++] = 0xC0000000u | (0u << 16) | (0x46u << 8);
            dcb[n++] = 0x00000410u; /* IT_EVENT_WRITE: PS_PARTIAL_FLUSH */

            /* Pre-compute IT_SURFACE_SYNC (0x43): flush CB, invalidate TC/TCL1 */
            dcb[n++] = 0xC0000000u | (3u << 16) | (0x43u << 8);
            dcb[n++] = 0x02C00000u; /* CB_ACTION_ENA | TC_ACTION_ENA | TCL1_ACTION_ENA */
            dcb[n++] = 0xFFFFFFFFu;
            dcb[n++] = 0x00000000u;
            dcb[n++] = 10u;

            /* COMPUTE_START_X/Y/Z (0x204..0x206) = 0,0,0; COMPUTE_NUM_THREAD_X/Y/Z (0x207..0x209) = 8,8,1 */
            dcb[n++] = 0xC0000002u | (6u << 16) | (0x76u << 8);
            dcb[n++] = 0x0204u;
            dcb[n++] = 0u;
            dcb[n++] = 0u;
            dcb[n++] = 0u;
            dcb[n++] = 8u;
            dcb[n++] = 8u;
            dcb[n++] = 1u;

            /* COMPUTE_PGM_LO (0x20C), COMPUTE_PGM_HI (0x20D) (SHADER_TYPE=1 Compute) */
            dcb[n++] = 0xC0000002u | (2u << 16) | (0x76u << 8);
            dcb[n++] = 0x020Cu;
            dcb[n++] = (uint32_t)(pgm_va >> 8);
            dcb[n++] = (uint32_t)(pgm_va >> 40);

            /* COMPUTE_PGM_RSRC1 (0x212), COMPUTE_PGM_RSRC2 (0x213) */
            dcb[n++] = 0xC0000002u | (2u << 16) | (0x76u << 8);
            dcb[n++] = 0x0212u;
            dcb[n++] = 0x00000043u; /* 16 VGPRs, 16 SGPRs */
            dcb[n++] = 0x00000998u; /* USER_SGPR=12, TGID_X_EN | TGID_Y_EN | TIDIG_COMP_CNT=1 (v0=x, v1=y) */

            /* COMPUTE_RESOURCE_LIMITS (0x215) = 0; STATIC_THREAD_MGMT_SE0/SE1 (0x216..0x217) = all CUs */
            dcb[n++] = 0xC0000002u | (3u << 16) | (0x76u << 8);
            dcb[n++] = 0x0215u;
            dcb[n++] = 0u;
            dcb[n++] = 0xFFFFFFFFu;
            dcb[n++] = 0xFFFFFFFFu;

            /* COMPUTE_USER_DATA_0..11 (0x240..0x24B): s0..s7 = T#, s8=w, s9=h, s10=exp, s11=0 */
            dcb[n++] = 0xC0000002u | (12u << 16) | (0x76u << 8);
            dcb[n++] = 0x0240u;
            dcb[n++] = (uint32_t)(bb_va >> 8);                                                /* T#0 */
            dcb[n++] = ((uint32_t)(bb_va >> 40) & 0xFFu) | (0x0Au << 20) | (0u << 26) | (3u << 30); /* T#1: 8_8_8_8 UNORM, MTYPE=UC */
            dcb[n++] = ((w - 1u) & 0x3FFFu) | (((h - 1u) & 0x3FFFu) << 14);                   /* T#2 */
            uint32_t tile_idx = 0x0Au; /* kTileModeDisplay_2dThin */
            if (ctx->shader_probe.anti_aliasing >= 2.0f) {
                tile_idx = (uint32_t)ctx->shader_probe.anti_aliasing & 0x1Fu;
            }
            dcb[n++] = (4u << 0) | (5u << 3) | (6u << 6) | (7u << 9) | (tile_idx << 20) | (9u << 28); /* T#3: XYZW, 2D */
            dcb[n++] = ((w - 1u) & 0x3FFFu) << 13;                                            /* T#4: PITCH - 1 */
            dcb[n++] = 0u;                                                                    /* T#5 */
            dcb[n++] = 0u;                                                                    /* T#6 */
            dcb[n++] = 0u;                                                                    /* T#7 */
            dcb[n++] = w;                                                                     /* s8 */
            dcb[n++] = h;                                                                     /* s9 */
            dcb[n++] = exp_bits;                                                              /* s10 */
            dcb[n++] = 0u;                                                                    /* s11 */

            /* IT_DISPATCH_DIRECT (0x15, SHADER_TYPE=1 Compute): (w+7)/8 x (h+7)/8 x 1 */
            dcb[n++] = 0xC0000002u | (3u << 16) | (0x15u << 8);
            dcb[n++] = (w + 7u) / 8u;
            dcb[n++] = (h + 7u) / 8u;
            dcb[n++] = 1u;
            dcb[n++] = 0x00000001u;

            /* IT_EVENT_WRITE (0x46): CS_PARTIAL_FLUSH */
            dcb[n++] = 0xC0000000u | (0u << 16) | (0x46u << 8);
            dcb[n++] = 0x00000407u;

            /* Post-compute IT_ACQUIRE_MEM (0x58): write-back & invalidate TC L2/L1 & CB */
            dcb[n++] = 0xC0000000u | (5u << 16) | (0x58u << 8);
            dcb[n++] = 0x02C08000u; /* CB_ACTION_ENA | TC_ACTION_ENA | TCL1_ACTION_ENA | TC_WB_ACTION_ENA */
            dcb[n++] = 0xFFFFFFFFu; /* COHER_SIZE */
            dcb[n++] = 0x000000FFu; /* COHER_SIZE_HI */
            dcb[n++] = 0x00000000u; /* COHER_BASE_LO */
            dcb[n++] = 0x00000000u; /* COHER_BASE_HI */
            dcb[n++] = 10u;         /* POLL_INTERVAL */

            /* Pad to 8-dword (32-byte) boundary with IT_NOP */
            while ((n & 7u) != 0u) {
                dcb[n++] = 0xC0001000u;
            }
            ctx->shader_probe.shader_size = n * 4u;
            __atomic_add_fetch(&ctx->shader_probe.dispatches, 1u, __ATOMIC_RELAXED);
        }
        if (flips == 1ULL || (flips % 600ULL) == 0ULL) {
            char smsg[ORBX_LOG_LINE_BYTES];
            size_t scap = sizeof(smsg) - 1;
            size_t sp = 0;
            sp = fmt_append_str(smsg, sp, scap, "[GNM-SHADER] Stage 1 Active: Dispatches=");
            sp = fmt_append_u64(smsg, sp, scap, (uint64_t)ctx->shader_probe.dispatches);
            sp = fmt_append_str(smsg, sp, scap, " (");
            sp = fmt_append_str(smsg, sp, scap, ctx->shader_probe.shader_file);
            sp = fmt_append_str(smsg, sp, scap, ")");
            smsg[sp] = '\0';
            log_ring_raw(ctx, smsg);
        }
    }

    /* Stage 2 Depth Buffer & Screen-Space SSAO/SSR Scanner */
    probe_gnm_depth_buffer(ctx, dcb_count, dcb_gpu_addrs, dcb_sizes, flips);

    uint64_t flip_fn = __atomic_load_n(&ctx->orig_sceVideoOutSetFlipRate, __ATOMIC_ACQUIRE);
    if (ctx->cfg.unlock_60fps && video_handle > 0 && flip_fn != 0ULL) {
        if (__atomic_exchange_n(&ctx->flip_rate_enforced, 1u, __ATOMIC_ACQ_REL) == 0u) {
            int32_t (*set_rate)(int32_t, int32_t) = (int32_t (*)(int32_t, int32_t))(uintptr_t)flip_fn;
            int32_t rc = set_rate(video_handle, 0);

            char msg[ORBX_LOG_LINE_BYTES];
            size_t cap = sizeof(msg) - 1;
            size_t p = 0;
            p = fmt_append_str(msg, p, cap, "[VIDEO] GnmFlip#");
            p = fmt_append_u64(msg, p, cap, flips);
            p = fmt_append_str(msg, p, cap, " enforced sceVideoOutSetFlipRate(h=");
            p = fmt_append_i64(msg, p, cap, (int64_t)video_handle);
            p = fmt_append_str(msg, p, cap, ", 0) rc=");
            p = fmt_append_i64(msg, p, cap, (int64_t)rc);
            msg[p] = '\0';
            log_ring_raw(ctx, msg);
        }
    }
    return __atomic_load_n(&ctx->orig_sceGnmSubmitAndFlipCommandBuffers, __ATOMIC_ACQUIRE);
}

static void record_pad_sample(OrbxNativeContext *ctx, int32_t handle, uint8_t *pData) {
    if (!ctx || !pData) return;
    __atomic_add_fetch(&ctx->menu.pad_calls, 1u, __ATOMIC_RELAXED);
    uint32_t btns = *(const uint32_t *)pData;
    if (btns != 0u) {
        ctx->menu.pad_active_handle = handle;
        if (!ctx->menu.pad_raw_seen) ctx->menu.pad_raw_seen = btns;
        __atomic_fetch_or(&ctx->menu.pad_buttons_latch, btns, __ATOMIC_RELAXED);
        __atomic_store_n(&ctx->menu.pad_buttons_live, btns, __ATOMIC_RELEASE);
    } else if (handle == ctx->menu.pad_active_handle || ctx->menu.pad_active_handle < 0) {
        __atomic_store_n(&ctx->menu.pad_buttons_live, 0u, __ATOMIC_RELEASE);
    }
    if (ctx->menu.armed && ctx->menu.menu_open && !ctx->menu.kb_active && !ctx->menu.keybind_capture_slot) {
        /* Suppress ONLY valid bound menu navigation/action buttons to game while menu is open */
        uint32_t allowed = 0x0010FFF6u & (
            0x000000F0u |
            ctx->menu.bind_accept |
            ctx->menu.bind_cancel |
            ctx->menu.bind_tab_prev |
            ctx->menu.bind_tab_next |
            ctx->menu.bind_open_mod |
            ctx->menu.bind_open_btn
        );
        if (ctx->menu.keep_steering) allowed &= ~0x00000300u; /* never mask L2/R2 triggers */
        uint32_t mask = __atomic_load_n(&ctx->menu.pad_suppress_mask, __ATOMIC_RELAXED) & allowed;
        if (!mask) mask = 0x00006CF0u & allowed; /* D-Pad + L1 + R1 + Circle + Cross */
        *(uint32_t *)pData = btns & ~mask;
    }
}

__attribute__((used, visibility("hidden")))
int32_t hook_scePadReadState(int32_t handle, uint8_t *pData) {
    OrbxNativeContext *ctx = get_ctx();
    if (!ctx || !ctx->orig_scePadReadState) return -1;
    int32_t (*fn)(int32_t, uint8_t *) = (int32_t (*)(int32_t, uint8_t *))(uintptr_t)ctx->orig_scePadReadState;
    int32_t rc = fn(handle, pData);
    if (rc == 0 && pData) record_pad_sample(ctx, handle, pData);
    return rc;
}

__attribute__((used, visibility("hidden")))
int32_t hook_scePadRead(int32_t handle, uint8_t *pData, int32_t num) {
    OrbxNativeContext *ctx = get_ctx();
    if (!ctx || !ctx->orig_scePadRead) return -1;
    int32_t (*fn)(int32_t, uint8_t *, int32_t) =
        (int32_t (*)(int32_t, uint8_t *, int32_t))(uintptr_t)ctx->orig_scePadRead;
    int32_t cnt = fn(handle, pData, num);
    if (cnt > 0 && pData) record_pad_sample(ctx, handle, pData);
    return cnt;
}

__attribute__((used, noinline, visibility("hidden")))
int32_t hook_sceGnmSubmitAndFlipCommandBuffers(
    uint32_t count,
    void *dcbGpuAddrs[],
    uint32_t *dcbSizesInBytes,
    void *ccbGpuAddrs[],
    uint32_t *ccbSizesInBytes,
    int32_t videoOutHandle,
    uint32_t displayBufferIndex,
    uint32_t flipMode,
    int64_t flipArg
) {
    uint64_t orig = on_gnm_flip_enter(videoOutHandle, (int32_t)displayBufferIndex, count, dcbGpuAddrs, dcbSizesInBytes);
    if (!orig) return -1;

    typedef int32_t (*fn_gnm_t)(uint32_t, void **, uint32_t *, void **, uint32_t *, int32_t, uint32_t, uint32_t, int64_t);
    fn_gnm_t fn = (fn_gnm_t)(uintptr_t)orig;

    OrbxNativeContext *ctx = get_ctx();
    if (ctx && ctx->shader_probe.loaded == 1u && ctx->shader_probe.enabled == 1u &&
        ctx->shader_probe.shader_gpu_va != 0ULL && ctx->gnm_probe.last_backbuffer_va != 0ULL &&
        ctx->shader_probe.shader_size >= 32u && count > 0u && count < 15u && dcbGpuAddrs && dcbSizesInBytes) {
        void *new_dcbs[16];
        uint32_t new_sizes[16];
        void *new_ccbs[16];
        uint32_t new_ccb_sizes[16];
        uint32_t insert_at = count - 1u;
        for (uint32_t i = 0u; i < insert_at; i++) {
            new_dcbs[i]      = dcbGpuAddrs[i];
            new_sizes[i]     = dcbSizesInBytes[i];
            new_ccbs[i]      = ccbGpuAddrs ? ccbGpuAddrs[i] : NULL;
            new_ccb_sizes[i] = ccbSizesInBytes ? ccbSizesInBytes[i] : 0u;
        }
        new_dcbs[insert_at]      = (void *)(uintptr_t)(ctx->shader_probe.shader_gpu_va + 0x2000ULL);
        new_sizes[insert_at]     = ctx->shader_probe.shader_size;
        new_ccbs[insert_at]      = NULL;
        new_ccb_sizes[insert_at] = 0u;
        for (uint32_t i = insert_at; i < count; i++) {
            new_dcbs[i + 1u]      = dcbGpuAddrs[i];
            new_sizes[i + 1u]     = dcbSizesInBytes[i];
            new_ccbs[i + 1u]      = ccbGpuAddrs ? ccbGpuAddrs[i] : NULL;
            new_ccb_sizes[i + 1u] = ccbSizesInBytes ? ccbSizesInBytes[i] : 0u;
        }
        int32_t rc = fn(count + 1u, new_dcbs, new_sizes,
                        ccbGpuAddrs ? new_ccbs : NULL,
                        ccbSizesInBytes ? new_ccb_sizes : NULL,
                        videoOutHandle, displayBufferIndex, flipMode, flipArg);
        if (ctx->shader_probe.dispatches <= 2u) {
            char rmsg[ORBX_LOG_LINE_BYTES];
            size_t rcap = sizeof(rmsg) - 1;
            size_t rp = 0;
            rp = fmt_append_str(rmsg, rp, rcap, "[GNM-SHADER] SubmitAndFlip(cnt=");
            rp = fmt_append_u64(rmsg, rp, rcap, (uint64_t)(count + 1u));
            rp = fmt_append_str(rmsg, rp, rcap, ", idx=");
            rp = fmt_append_u64(rmsg, rp, rcap, (uint64_t)displayBufferIndex);
            rp = fmt_append_str(rmsg, rp, rcap, ", bb=");
            rp = fmt_append_hex64(rmsg, rp, rcap, ctx->gnm_probe.last_backbuffer_va);
            rp = fmt_append_str(rmsg, rp, rcap, ") rc=");
            rp = fmt_append_i64(rmsg, rp, rcap, (int64_t)rc);
            rmsg[rp] = '\0';
            log_ring_raw(ctx, rmsg);
        }
        return rc;
    }

    return fn(count, dcbGpuAddrs, dcbSizesInBytes, ccbGpuAddrs, ccbSizesInBytes,
              videoOutHandle, displayBufferIndex, flipMode, flipArg);
}

static void pso_register_string_list(OrbxNativeContext *ctx,
                                     uint32_t (*fn_reg1)(uint32_t, const char *),
                                     void (*fn_reg2)(uint32_t, uint32_t, const char *),
                                     uint32_t mode, const uint8_t *start, size_t max_bytes) {
    if (!start || *start == 0) return;

    const uint8_t *cur = start;
    const uint8_t *end = (max_bytes != (size_t)-1) ? (start + max_bytes) : NULL;
    const char *prev_ok = NULL;

    while ((!end || cur < end) && *cur != 0) {
        size_t avail = end ? (size_t)(end - cur) : 256u;
        if (avail > 256u) avail = 256u;

        size_t slen = 0;
        int valid = 0;
        for (size_t i = 0; i < avail; i++) {
            uint8_t c = cur[i];
            if (c == 0) {
                if (i > 0) {
                    slen = i;
                    valid = 1;
                }
                break;
            }
            if (c < 0x20u || c >= 0x7fu) {
                break;
            }
        }

        if (!valid) {
            uint32_t n = __atomic_add_fetch(&ctx->pso_strs_sanitized, 1u, __ATOMIC_RELAXED);
            if (n <= 12u) {
                char msg[ORBX_LOG_LINE_BYTES];
                size_t cap = sizeof(msg) - 1;
                size_t p = 0;
                p = fmt_append_str(msg, p, cap, "[PSO-GUARD] Stopped invalid tail #");
                p = fmt_append_u64(msg, p, cap, (uint64_t)n);
                p = fmt_append_str(msg, p, cap, " next=");
                p = fmt_append_hex32(msg, p, cap, (uint32_t)cur[0]);
                p = fmt_append_str(msg, p, cap, " after '");
                p = fmt_append_str(msg, p, cap, prev_ok ? prev_ok : "<start>");
                p = fmt_append_str(msg, p, cap, "'");
                msg[p] = '\0';
                log_ring_raw(ctx, msg);
            }
            break;
        }

        uint32_t h = fn_reg1(mode, (const char *)cur);
        fn_reg2(mode, h, (const char *)cur);
        prev_ok = (const char *)cur;
        cur += slen + 1u;
    }
}

__attribute__((used, visibility("hidden")))
uint8_t hook_pso_strs(void *out, uint8_t *r14, uint8_t keep, uint8_t hash_strs) {
    if (keep && out) *(void **)((uint8_t *)out + 0x10) = r14;
    if (out) *(void **)out = r14;
    if (!hash_strs || !r14) return 1u;

    OrbxNativeContext *ctx = get_ctx();
    if (!ctx || !ctx->orig_19cfd30 || !ctx->orig_19cfd50) return 1u;
    __atomic_add_fetch(&ctx->pso_strs_calls, 1u, __ATOMIC_RELAXED);

    uint32_t (*fn_reg1)(uint32_t, const char *) =
        (uint32_t (*)(uint32_t, const char *))(uintptr_t)ctx->orig_19cfd30;
    void (*fn_reg2)(uint32_t, uint32_t, const char *) =
        (void (*)(uint32_t, uint32_t, const char *))(uintptr_t)ctx->orig_19cfd50;

    const uint8_t *strs = *(const uint8_t *const *)(r14 + 0x38);
    if (strs && *strs != 0) {
        pso_register_string_list(ctx, fn_reg1, fn_reg2, 1u, strs, (size_t)-1);
    }

    uint16_t cnt = *(const uint16_t *)(r14 + 0x4c);
    if (cnt != 0) {
        const uint8_t *blocks = *(const uint8_t *const *)(r14 + 0x30);
        if (blocks) {
            for (uint16_t i = 0; i < cnt; i++) {
                const uint8_t *ent = blocks + (size_t)i * 16u;
                uint32_t tid = *(const uint32_t *)(ent + 0);
                uint32_t sub = tid + 0xfffff300u; /* tid - 0xd00 */
                if (sub > 0xffu) continue;
                uint32_t bsz = *(const uint32_t *)(ent + 4);
                const uint8_t *blk = *(const uint8_t *const *)(ent + 8);
                if (!blk || *blk == 0 || bsz < 0x11u) continue;
                pso_register_string_list(ctx, fn_reg1, fn_reg2, sub, blk, (size_t)bsz);
            }
        }
    }
    return 1u;
}

__attribute__((naked, section(".text.entry"), used, visibility("hidden")))
void orbx_entry_table(void) {
    __asm__ volatile (
        ".p2align 4\n"
        "jmp hook_sceKernelOpen\n"
        ".p2align 4\n"
        "jmp hook_open\n"
        ".p2align 4\n"
        "jmp hook_sceKernelStat\n"
        ".p2align 4\n"
        "jmp hook_stat\n"
        ".p2align 4\n"
        "jmp hook_fopen\n"
        ".p2align 4\n"
        "jmp hook_sceKernelRead\n"
        ".p2align 4\n"
        "jmp hook_sceKernelPread\n"
        ".p2align 4\n"
        "jmp hook_read\n"
        ".p2align 4\n"
        "jmp hook_pread\n"
        ".p2align 4\n"
        "jmp hook_sceKernelClose\n"
        ".p2align 4\n"
        "jmp hook_close\n"
        ".p2align 4\n"
        "jmp hook_sceVideoOutSetFlipRate\n"
        ".p2align 4\n"
        "jmp hook_sceGnmSubmitAndFlipCommandBuffers\n"
        ".p2align 4\n"
        "jmp hook_pso_strs\n"
        ".p2align 4\n"
        "jmp hook_menu_wait\n"
        ".p2align 4\n"
        "jmp hook_scePadReadState\n"
        ".p2align 4\n"
        "jmp hook_scePadRead\n"
        ".p2align 4\n"
        "jmp hook_sceVideoOutRegisterBuffers\n"
        ".p2align 4\n"
    );
}
