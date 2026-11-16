#pragma once

#include <stdint.h>
#include <stddef.h>
#include "orbx_menu_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBX_NATIVE_MAGIC             UINT64_C(0x4f52425850533532) // "ORBXPS52"
#define ORBX_NATIVE_MAGIC_PREFIX      UINT64_C(0x4f52425850533500) // "ORBXPS5?"
#define ORBX_NATIVE_MAGIC_PREFIX_MASK UINT64_C(0xffffffffffffff00)
#define ORBX_NATIVE_ABI_VERSION       5u
#define ORBX_MAX_MOD_ENTRIES   4096u
#define ORBX_STRING_POOL_BYTES (256u * 1024u)
#define ORBX_LOG_RING_SLOTS    512u
#define ORBX_LOG_LINE_BYTES    224u
#define ORBX_MAX_TRACKED_FDS   32u
#define ORBX_STAT_SIZE_OFFSET  72u

// Fixed entry stub offsets inside orbx_native.bin (.text.entry starting at +0x10)
#define ORBX_ENTRY_OFF_SCE_OPEN   0x10u
#define ORBX_ENTRY_OFF_OPEN       0x20u
#define ORBX_ENTRY_OFF_SCE_STAT   0x30u
#define ORBX_ENTRY_OFF_STAT       0x40u
#define ORBX_ENTRY_OFF_FOPEN      0x50u
#define ORBX_ENTRY_OFF_SCE_READ   0x60u
#define ORBX_ENTRY_OFF_SCE_PREAD  0x70u
#define ORBX_ENTRY_OFF_READ       0x80u
#define ORBX_ENTRY_OFF_PREAD      0x90u
#define ORBX_ENTRY_OFF_SCE_CLOSE         0xa0u
#define ORBX_ENTRY_OFF_CLOSE             0xb0u
#define ORBX_ENTRY_OFF_SCE_SET_FLIP_RATE 0xc0u
#define ORBX_ENTRY_OFF_SCE_GNM_FLIP      0xd0u
#define ORBX_ENTRY_OFF_PSO_STRS          0xe0u
#define ORBX_ENTRY_OFF_MENU_WAIT         0xf0u
#define ORBX_ENTRY_OFF_SCE_PAD_READ_STATE 0x100u
#define ORBX_ENTRY_OFF_SCE_PAD_READ      0x110u
#define ORBX_ENTRY_OFF_SCE_VIDEO_REG_BUFS 0x120u
#define ORBX_ENTRY_MIN_BIN_SIZE          0x130u

typedef struct {
    uint64_t magic;     // ORBX_NATIVE_MAGIC
    uint64_t ctx_addr;  // Pointer to OrbxNativeContext in RW anonymous memory
} OrbxNativeHeader;

typedef struct {
    uint8_t allow_rpf;
    uint8_t allow_xml;
    uint8_t allow_omt;
    uint8_t allow_dat;
    uint8_t allow_meta;
    uint8_t allow_rockstar_intro;
    uint8_t allow_gxt2;
    uint8_t allow_dlc_gxt2;
    uint8_t allow_gfx;
    uint8_t debug_rpf_info;
    uint8_t debug_dat_info;
    uint8_t avoid_crc;
    uint8_t unlock_60fps;
    uint8_t pad_cfg[3];
} OrbxNativeConfig;

typedef struct {
    uint32_t joaat_hash;   // Lowercase RAGE JOAAT of relative key
    uint32_t key_offset;   // Relative normalized key offset in string_pool (for collision verification)
    uint32_t path_offset;  // Full SSD destination path (/data/orbx/gta/...) in string_pool
} OrbxModEntry;

typedef struct {
    int32_t  fd;
    uint32_t open_seq;
    uint64_t bytes_read;
    uint32_t read_calls;
    uint32_t first_read_logged;
    char     name[48];
} OrbxTrackedFd;

/* GNM Graphics Pipeline & Crash Telemetry Probes (Phase 0 Read-Only) */
typedef struct {
    uint64_t backbuffers[4];
    uint32_t backbuffer_count;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t format;
    int32_t  handle;
    uint64_t last_reg_tick;
} GnmSwapchainProbe;

typedef struct {
    volatile uint32_t stage;              /* 0=IDLE, 1=PROBE_SWAPCHAIN, 2=IN_FLIP_HOOK, 3=TELEMETRY_READY */
    volatile uint32_t last_display_index; /* Active backbuffer flipped (0, 1, or 2) */
    volatile uint64_t last_backbuffer_va; /* Active target GPU VA */
    volatile uint32_t last_dcb_count;     /* Number of DCBs submitted */
    volatile uint32_t last_dcb_total_sz;  /* Total bytes across all DCBs */
    volatile uint64_t last_dcb_va;        /* First DCB GPU address */
    volatile uint64_t last_flip_tick;     /* Monotonic flip counter */
    volatile uint32_t stall_count;        /* Watchdog stall count */
} GnmCrashProbe;

/* Stage 1 Real-Time PSSL .sb Post-Processing Shader Telemetry & Tunables */
typedef struct {
    volatile uint32_t enabled;           /* 0=Pass-Through, 1=Active Post-Processing */
    volatile uint32_t loaded;            /* 0=None, 1=Valid .sb Loaded, 2=Validation Error */
    volatile uint32_t shader_type;       /* 1=CS_5_0 Compute Shader, 2=PS_5_0 Pixel Shader */
    volatile uint32_t shader_size;       /* Byte size of .sb file */
    volatile uint64_t shader_gpu_va;     /* Direct GPU Garlic VA where microcode is mapped */
    volatile uint32_t error_code;        /* 0=OK, 1=NOT_FOUND, 2=TOO_SMALL, 3=BAD_MAGIC, 4=BAD_ALIGN, 5=ALLOC_FAIL */
    volatile uint32_t dispatches;        /* Execution dispatch count */

    /* Live Parameters passed in CB0 constant buffer */
    volatile float    bloom_threshold;   /* Default ~0.85f */
    volatile float    bloom_intensity;   /* Default ~0.35f */
    volatile float    exposure;          /* Default ~1.00f */
    volatile float    contrast;          /* Default ~1.05f */
    volatile float    saturation;        /* Default ~1.10f */
    volatile float    vignette;          /* Default ~0.20f */
    volatile float    color_temp;        /* Default 6500.0f */
    volatile float    film_grain;        /* Default 0.00f */
    volatile float    anti_aliasing;     /* Default ~0.35f: Subpixel Edge Smoothing */
    volatile float    sharpness;         /* Default ~0.20f: Contrast-Adaptive Sharpness */

    char     shader_file[64];            /* "/data/orbx/shaders/postprocess.sb" */
    char     last_error_msg[64];         /* Status / error breadcrumb */
} GnmShaderProbe;

/* Stage 2 Depth Buffer, Normal G-Buffer & Screen-Space Telemetry (SSAO / SSR) */
typedef struct {
    volatile uint32_t stage;               /* 0=IDLE, 1=SCANNING_DCB, 2=DEPTH_FOUND, 3=SSAO_ACTIVE */
    volatile uint64_t depth_buffer_va;     /* GPU VA of active Z-Buffer (read-only detected) */
    volatile uint32_t depth_width;         /* Surface width e.g. 1920 */
    volatile uint32_t depth_height;        /* Surface height e.g. 1080 */
    volatile uint32_t depth_format;        /* Format e.g. 0=Z32_FLOAT, 1=Z24_UNORM_S8 */
    volatile uint32_t depth_samples;       /* MSAA samples (1, 2, 4) */
    volatile uint64_t depth_hits;          /* Number of depth bindings detected */
    volatile uint32_t ssao_mode;           /* 0=Off/Native, 1=QuantV High SSAO, 2=Ultra HBAO+ */
    volatile float    ssao_intensity;      /* Occlusion multiplier (e.g. 1.0f - 6.0f, default 5.80f) */
    volatile float    ssao_radius;         /* Sample radius (default 1.00f) */
    volatile float    ssao_cp_strength;    /* Contact point shadow strength (default 0.78f) */
    volatile float    ssr_intensity;       /* Reflection HDR multiplier (default 2.45f) */
    volatile float    ssr_distance;        /* Max reflection distance in meters (default 500.0f) */
} GnmDepthProbe;

typedef struct {
    uint64_t magic;
    uint64_t code_base;
    uint64_t ctx_base;
    uint32_t abi_version;
    uint32_t ctx_size;
    uint64_t code_alloc_size;
    uint64_t orig_wait;
    OrbxNativeConfig cfg;

    // Resolved real function targets in libkernel.sprx / libSceLibcInternal.sprx / libSceVideoOut.sprx
    uint64_t orig_sceKernelOpen;
    uint64_t orig_open;
    uint64_t orig_sceKernelStat;
    uint64_t orig_stat;
    uint64_t orig_fopen;
    uint64_t orig_sceKernelRead;
    uint64_t orig_sceKernelPread;
    uint64_t orig_read;
    uint64_t orig_pread;
    uint64_t orig_sceKernelClose;
    uint64_t orig_close;
    uint64_t orig_sceVideoOutSetFlipRate;
    uint64_t orig_sceGnmSubmitAndFlipCommandBuffers;
    uint64_t orig_scePadReadState;
    uint64_t orig_scePadRead;
    uint64_t orig_19cfd30;
    uint64_t orig_19cfd50;
    volatile uint32_t pso_strs_calls;
    volatile uint32_t pso_strs_sanitized;
    volatile uint32_t flip_rate_logged;
    volatile uint32_t flip_rate_enforced;
    volatile uint64_t gnm_flip_count;

    // Live telemetry polled safely via kernel_proc_copyout
    volatile uint32_t total_calls;
    volatile uint32_t total_redirects;
    volatile uint32_t trace_opens_logged;
    volatile uint32_t dlc_traces_logged;
    volatile uint32_t tracked_seq;
    volatile uint32_t log_write_idx;
    volatile uint64_t fd_bitmap[16]; // Fast-path bitset for tracked FDs 0..1023
    OrbxTrackedFd tracked_fds[ORBX_MAX_TRACKED_FDS];
    char log_entries[ORBX_LOG_RING_SLOTS][ORBX_LOG_LINE_BYTES];

    uint32_t mod_count;
    uint32_t pool_used;
    OrbxModEntry mods[ORBX_MAX_MOD_ENTRIES];
    char string_pool[ORBX_STRING_POOL_BYTES];

    OrbxMenuState menu;

    // Freestanding PIC scratch buffer in RW anonymous heap (avoids stack bloat in hooked WAIT)
    uint8_t menu_rows_scratch[64u * 128u];

    GnmSwapchainProbe swapchain_probe;
    GnmCrashProbe     gnm_probe;
    GnmShaderProbe    shader_probe;
    GnmDepthProbe     depth_probe;
    uint64_t          orig_sceVideoOutRegisterBuffers;
    uint64_t          fn_get_dmem_size;
    uint64_t          fn_alloc_dmem;
    uint64_t          fn_map_dmem;
} OrbxNativeContext;

#ifdef __cplusplus
}
#endif
