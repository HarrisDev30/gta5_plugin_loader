#pragma once

#include <stdint.h>
#include <stddef.h>
#include "orbx_menu_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBX_NATIVE_MAGIC      UINT64_C(0x4f52425850533531) // "ORBXPS51"
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
#define ORBX_ENTRY_MIN_BIN_SIZE          0x120u

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

typedef struct {
    uint64_t magic;
    uint64_t code_base;
    uint64_t ctx_base;
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
} OrbxNativeContext;

#ifdef __cplusplus
}
#endif
