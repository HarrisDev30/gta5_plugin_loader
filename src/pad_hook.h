#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TAC_GAME_GATE_MAGIC UINT64_C(0x54414347414d4531)
#define TAC_GAME_MODULE_SLOTS 8
#define TAC_GAME_GATE_VERSION 2

enum {
    TAC_MODULE_PENDING  = 0,
    TAC_MODULE_STARTING = 1,
    TAC_MODULE_STARTED  = 2,
    TAC_MODULE_FAILED   = 3,
    TAC_MODULE_REJECTED = 4
};

enum {
    TAC_MODULE_PHASE_IDLE     = 0,
    TAC_MODULE_PHASE_THREAD   = 1,
    TAC_MODULE_PHASE_LOAD     = 2,
    TAC_MODULE_PHASE_EXPORTS  = 3,
    TAC_MODULE_PHASE_ACTIVATE = 4,
    TAC_MODULE_PHASE_COMPLETE = 5
};

typedef struct {
    uint32_t enabled, state;
    int32_t handle, startResult;
    char path[256];
    uint32_t phase, activation;
    int32_t threadResult, detachResult, activationResult, loadSymbolResult, unloadSymbolResult;
} TacGameModule;

typedef struct {
    uint64_t magic, cookie, loadStartModule;
    uint32_t version, bytes, enabled, busy;
    uint64_t callbacks[2];
    uint64_t threadCreate, threadDetach, dlsym;
    char sym_plugin_load[32];
    char sym_plugin_unload[32];
    char thread_name[32];
    TacGameModule modules[TAC_GAME_MODULE_SLOTS];
} TacGameGate;

typedef struct {
    const char *nid;
    uint64_t    out_slot;
    char        matched_sym[64];
} EbootGotQuery;

// Query eboot.bin map base and virtual map size via kernel dynlib object list
void get_eboot_module_bounds(pid_t pid, uint64_t *out_base, uint64_t *out_size);

// Batch-resolve GOT/JMP_SLOT addresses in eboot.bin (returns count found, or -1 on error)
int find_eboot_got_slots(pid_t pid, EbootGotQuery *queries, size_t count);

// Convenience single-NID JMP_SLOT lookup in eboot.bin
uint64_t find_eboot_got_slot_by_nid(pid_t pid, const char *nid);

// Hook target game's controller polling (scePadReadState) and schedule plugin loading
int install_pad_hook(pid_t pid, const char *plugin_path, uint32_t *out_module_handle);

// Check if the loaded module has completed its initialization
int query_module_status(pid_t pid, int *out_state, int *out_handle);

#ifdef __cplusplus
}
#endif
