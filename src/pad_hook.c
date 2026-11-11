#define _GNU_SOURCE
#include "pad_hook.h"
#include "orbx_compat.h"
#include "worker_blob.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>

#include <ps5/kernel.h>

extern const uint8_t _binary_orbx_prx_start[];
extern const uint8_t _binary_orbx_prx_end[];

static uint64_t g_gate_control_addr = 0;
static uint64_t g_got_slot_addr = 0;
static uint64_t g_orig_pad_func = 0;

// NIDs for dynamic resolution
#define NID_LOAD_START_MODULE "wzvqT4UqKX8"
#define NID_PTHREAD_CREATE    "6UgtwV+0zb4"
#define NID_PTHREAD_DETACH    "4qGrR6eoP9Y"
#define NID_DLSYM             "LwG8g3niqwA"
#define NID_PAD_READ_STATE    "YndgXqQVV7c"
#define NID_PAD_READ          "q1cHNfGycLI"

static intptr_t resolve_symbol(pid_t pid, const char *module_name, const char *nid) {
    uint32_t handle = 0;
    if (kernel_dynlib_handle(pid, module_name, &handle) != 0 || handle == 0) {
        return 0;
    }
    return kernel_dynlib_resolve(pid, handle, nid);
}

// Find eboot base and size
void get_eboot_module_bounds(pid_t pid, uint64_t *out_base, uint64_t *out_size) {
    *out_base = 0x400000;
    *out_size = 0x27ac000;

    intptr_t kproc = kernel_get_proc(pid);
    if (!kproc) return;

    intptr_t p_dynlib = 0;
    kernel_copyout(kproc + 0x3e8, &p_dynlib, sizeof(p_dynlib));
    if (!p_dynlib) return;

    intptr_t obj = 0;
    kernel_copyout(p_dynlib, &obj, sizeof(obj));

    int safety = 0;
    while (obj && safety++ < 256) {
        intptr_t next_obj = 0, path_ptr = 0;
        uint64_t mapbase = 0, mapsize = 0;
        kernel_copyout(obj + 0x00, &next_obj, sizeof(next_obj));
        kernel_copyout(obj + 0x08, &path_ptr, sizeof(path_ptr));
        kernel_copyout(obj + 0x30, &mapbase, sizeof(mapbase));
        kernel_copyout(obj + 0x38, &mapsize, sizeof(mapsize));

        char mod_path[256] = {0};
        if (path_ptr) kernel_copyout(path_ptr, mod_path, sizeof(mod_path) - 1);

        if (strstr(mod_path, "eboot") != NULL) {
            *out_base = mapbase;
            *out_size = mapsize;
            break;
        }
        obj = next_obj;
    }
}

static void get_eboot_bounds(pid_t pid, uint64_t *out_base, uint64_t *out_size) {
    get_eboot_module_bounds(pid, out_base, out_size);
}

int find_eboot_got_slots(pid_t pid, EbootGotQuery *queries, size_t count) {
    if (!queries || count == 0) return 0;
    for (size_t q = 0; q < count; q++) {
        queries[q].out_slot = 0;
        queries[q].matched_sym[0] = '\0';
    }

    intptr_t kproc = kernel_get_proc(pid);
    if (!kproc) return -1;

    intptr_t p_dynlib = 0;
    kernel_copyout(kproc + 0x3e8, &p_dynlib, sizeof(p_dynlib));
    if (!p_dynlib) return -1;

    intptr_t obj = 0;
    kernel_copyout(p_dynlib, &obj, sizeof(obj));

    int found = 0;
    int safety = 0;
    while (obj && safety++ < 256) {
        intptr_t next_obj = 0, path_ptr = 0;
        uint64_t mapbase = 0, meta_ptr = 0;
        kernel_copyout(obj + 0x00, &next_obj, sizeof(next_obj));
        kernel_copyout(obj + 0x08, &path_ptr, sizeof(path_ptr));
        kernel_copyout(obj + 0x30, &mapbase, sizeof(mapbase));
        kernel_copyout(obj + 0x148, &meta_ptr, sizeof(meta_ptr));

        char mod_path[256] = {0};
        if (path_ptr) kernel_copyout(path_ptr, mod_path, sizeof(mod_path) - 1);

        if (strstr(mod_path, "eboot") != NULL && meta_ptr) {
            unsigned char header[0x58];
            if (kernel_copyout((intptr_t)meta_ptr, header, sizeof(header)) == 0) {
                uint64_t sy  = *(uint64_t*)(header + 0x28); // symtab
                uint64_t syn = *(uint64_t*)(header + 0x30); // symtab bytes
                uint64_t st  = *(uint64_t*)(header + 0x38); // strtab
                uint64_t stn = *(uint64_t*)(header + 0x40); // strtab bytes
                uint64_t pl  = *(uint64_t*)(header + 0x48); // jmprel
                uint64_t pln = *(uint64_t*)(header + 0x50); // jmprel bytes

                if (syn > 0 && syn < 24 * 65536 && stn > 0 && stn < 4 * 1024 * 1024 &&
                    pln > 0 && pln < 24 * 65536) {
                    unsigned char *symbols = malloc(syn);
                    unsigned char *strings = malloc(stn);
                    unsigned char *relocs  = malloc(pln);
                    if (symbols && strings && relocs) {
                        if (kernel_copyout((intptr_t)sy, symbols, syn) == 0 &&
                            kernel_copyout((intptr_t)st, strings, stn) == 0 &&
                            kernel_copyout((intptr_t)pl, relocs, pln) == 0) {
                            for (size_t i = 0; i < pln; i += 24) {
                                uint64_t r_offset = *(uint64_t*)(relocs + i);
                                uint64_t r_info   = *(uint64_t*)(relocs + i + 8);
                                if ((uint32_t)r_info != 7) continue; // R_X86_64_JUMP_SLOT
                                uint32_t sym_idx = (uint32_t)(r_info >> 32);
                                if (sym_idx >= syn / 24) continue;
                                uint32_t str_idx = *(uint32_t*)(symbols + sym_idx * 24);
                                if (str_idx >= stn) continue;
                                const char *sname = (const char*)(strings + str_idx);

                                for (size_t q = 0; q < count; q++) {
                                    if (queries[q].out_slot != 0 || !queries[q].nid) continue;
                                    size_t nlen = strlen(queries[q].nid);
                                    if (strncmp(sname, queries[q].nid, nlen) == 0 &&
                                        (sname[nlen] == '\0' || sname[nlen] == '#')) {
                                        queries[q].out_slot = mapbase + r_offset;
                                        snprintf(queries[q].matched_sym, sizeof(queries[q].matched_sym), "%s", sname);
                                        found++;
                                        break;
                                    }
                                }
                                if ((size_t)found == count) break;
                            }
                        }
                    }
                    if (symbols) free(symbols);
                    if (strings) free(strings);
                    if (relocs)  free(relocs);
                }
            }
            break;
        }
        obj = next_obj;
    }

    return found;
}

uint64_t find_eboot_got_slot_by_nid(pid_t pid, const char *nid) {
    EbootGotQuery q = { .nid = nid, .out_slot = 0, .matched_sym = {0} };
    if (find_eboot_got_slots(pid, &q, 1) > 0) {
        return q.out_slot;
    }
    return 0;
}

// Locate the GOT/import table slot in eboot.bin data segment that points to pad_func
static uint64_t find_pad_got_slot(pid_t pid, uint64_t pad_func, uint64_t eboot_base, uint64_t eboot_size) {
    // Method 1: Dynamically parse eboot.bin JMP_SLOT relocations via shared dynlib scanner
    EbootGotQuery pad_queries[2] = {
        { .nid = NID_PAD_READ_STATE, .out_slot = 0, .matched_sym = {0} },
        { .nid = NID_PAD_READ,       .out_slot = 0, .matched_sym = {0} }
    };
    if (find_eboot_got_slots(pid, pad_queries, 2) > 0) {
        for (size_t i = 0; i < 2; i++) {
            if (pad_queries[i].out_slot != 0) {
                log_msg("[HOOK] Dynamically discovered Pad GOT slot via JMP_SLOT @ 0x%llx (%s)\n",
                        (unsigned long long)pad_queries[i].out_slot, pad_queries[i].matched_sym);
                return pad_queries[i].out_slot;
            }
        }
    }

    // Method 2: Verified proven GOT slot for GTA V CUSA00419
    uint64_t known_slot = 0x2c3fb48;
    uint64_t test_val = 0;
    if (kernel_proc_copyout(pid, (intptr_t)known_slot, &test_val, sizeof(test_val)) == 0) {
        log_msg("[HOOK] Checking proven GTA V GOT slot 0x%llx (contains 0x%llx, pad_func=0x%llx)\n",
                (unsigned long long)known_slot, (unsigned long long)test_val, (unsigned long long)pad_func);
        return known_slot;
    }

    // Method 3: Scan eboot.bin data segment for pad_func pointer
    uint64_t scan_start = eboot_base + 0x2000000;
    uint64_t scan_end   = eboot_base + 0x2f00000;
    static uint64_t chunk[8192];
    size_t chunk_bytes = sizeof(chunk);

    log_msg("[HOOK] Scanning eboot data segment (0x%llx - 0x%llx) for pad_func pointer...\n",
            (unsigned long long)scan_start, (unsigned long long)scan_end);

    for (uint64_t cur = scan_start; cur < scan_end; cur += chunk_bytes) {
        size_t to_read = (cur + chunk_bytes <= scan_end) ? chunk_bytes : (scan_end - cur);
        if (kernel_proc_copyout(pid, (intptr_t)cur, chunk, to_read) != 0) continue;
        size_t count = to_read / 8;
        for (size_t i = 0; i < count; i++) {
            if (chunk[i] == pad_func) {
                uint64_t slot = cur + (i * 8);
                log_msg("[HOOK] Found exact pad GOT slot @ 0x%llx -> 0x%llx\n",
                        (unsigned long long)slot, (unsigned long long)pad_func);
                return slot;
            }
        }
    }

    return 0;
}

// Find a code cave in eboot.bin text segment for the 16KB arena
static uint64_t find_eboot_cave(pid_t pid, uint64_t eboot_base, uint64_t eboot_size) {
    uint64_t text_end = eboot_base + 0x27ac000;
    static uint8_t page[4096];

    for (uint64_t addr = text_end - 0x10000; addr >= text_end - 0x60000 && addr >= eboot_base; addr -= 0x1000) {
        if (kernel_proc_copyout(pid, (intptr_t)addr, page, sizeof(page)) != 0) continue;
        int empty = 1;
        for (int i = 0; i < 4096; i++) {
            if (page[i] != 0x00 && page[i] != 0x90 && page[i] != 0xcc) {
                empty = 0;
                break;
            }
        }
        if (empty) {
            uint64_t cave = (addr + 0xfff) & ~0xfffULL;
            log_msg("[HOOK] Found 4KB code cave in eboot @ 0x%llx\n", (unsigned long long)cave);
            return cave;
        }
    }

    uint64_t fallback_cave = (text_end - 0x10000 + 0xfff) & ~0xfffULL;
    log_msg("[HOOK] Using standard code cave in eboot @ 0x%llx\n", (unsigned long long)fallback_cave);
    return fallback_cave;
}

int install_pad_hook(pid_t pid, const char *plugin_path, uint32_t *out_module_handle) {
    if (pid <= 1 || !plugin_path) return -1;

    log_msg("[HOOK] Starting HyperHEN-Style Pad Hook Session for PID %d...\n", pid);

    uint64_t eboot_base = 0, eboot_size = 0;
    get_eboot_bounds(pid, &eboot_base, &eboot_size);
    log_msg("[HOOK] Eboot mapped @ 0x%llx (size: 0x%llx)\n",
            (unsigned long long)eboot_base, (unsigned long long)eboot_size);

    // Make eboot text segment writable so Orbx detour hooks can install without page faulting
    kernel_set_vmem_protection(pid, (intptr_t)eboot_base, eboot_size, PROT_READ | PROT_WRITE | PROT_EXEC);

    // 1. Resolve required symbols in game
    uint64_t load_module = (uint64_t)resolve_symbol(pid, "libkernel.sprx", NID_LOAD_START_MODULE);
    if (!load_module) load_module = (uint64_t)resolve_symbol(pid, "libkernel_sys.sprx", NID_LOAD_START_MODULE);

    uint64_t pthread_create = (uint64_t)resolve_symbol(pid, "libkernel.sprx", NID_PTHREAD_CREATE);
    if (!pthread_create) pthread_create = (uint64_t)resolve_symbol(pid, "libkernel_sys.sprx", NID_PTHREAD_CREATE);

    uint64_t pthread_detach = (uint64_t)resolve_symbol(pid, "libkernel.sprx", NID_PTHREAD_DETACH);
    if (!pthread_detach) pthread_detach = (uint64_t)resolve_symbol(pid, "libkernel_sys.sprx", NID_PTHREAD_DETACH);

    uint64_t dlsym_func = (uint64_t)resolve_symbol(pid, "libkernel.sprx", NID_DLSYM);
    if (!dlsym_func) dlsym_func = (uint64_t)resolve_symbol(pid, "libkernel_sys.sprx", NID_DLSYM);

    if (!load_module || !pthread_create || !pthread_detach || !dlsym_func) {
        log_msg("[HOOK] Error: Failed to resolve libkernel symbols (load=%llx create=%llx detach=%llx dlsym=%llx)\n",
                (unsigned long long)load_module, (unsigned long long)pthread_create,
                (unsigned long long)pthread_detach, (unsigned long long)dlsym_func);
        return -1;
    }

    uint64_t pad_func = (uint64_t)resolve_symbol(pid, "libScePad.sprx", NID_PAD_READ_STATE);
    if (!pad_func) pad_func = (uint64_t)resolve_symbol(pid, "libScePad.sprx", NID_PAD_READ);
    if (!pad_func) {
        log_msg("[HOOK] Error: Failed to resolve scePadReadState in libScePad.sprx\n");
        return -1;
    }

    g_orig_pad_func = pad_func;
    log_msg("[HOOK] Resolved scePadReadState @ 0x%llx\n", (unsigned long long)pad_func);

    // 2. Locate GOT slot in eboot.bin data segment pointing to pad_func
    uint64_t got_slot = find_pad_got_slot(pid, pad_func, eboot_base, eboot_size);
    if (!got_slot) {
        got_slot = 0x2c3fb48;
        log_msg("[HOOK] Warning: Using fallback GOT slot 0x%llx\n", (unsigned long long)got_slot);
    }
    g_got_slot_addr = got_slot;

    // 3. Separate Code Arena (executable cave) vs Control Arena (DATA SEGMENT)
    uint64_t code_arena = find_eboot_cave(pid, eboot_base, eboot_size);
    uint64_t stub_addr           = code_arena + 0x1000;
    uint64_t sys_sdk_bridge_addr = code_arena + 0x800;
    uint64_t syscall_bridge_addr = code_arena + 0x880;

    kernel_set_vmem_protection(pid, (intptr_t)code_arena, 0x4000, PROT_READ | PROT_WRITE | PROT_EXEC);

    // Control arena holds TacGameGate, module paths & symbol names in DATA SEGMENT (PROT_READ | PROT_WRITE, NEVER XOM!)
    // eboot_base (0x400000) + 0x2e00000 = 0x3200000 (safely inside data/bss region past GOT)
    uint64_t ctrl_arena = eboot_base + 0x2e00000;
    uint64_t info_addr   = ctrl_arena + 0x500;
    kernel_set_vmem_protection(pid, (intptr_t)ctrl_arena, 0x4000, PROT_READ | PROT_WRITE);

    // 4. Resolve authorized libkernel syscall gate
    uint64_t native_gate = find_native_syscall_gate(pid);
    if (!native_gate) {
        log_msg("[HOOK] Critical: Native syscall gate not found! Aborting hook to protect game stability.\n");
        return -1;
    }

    // 5. Stage Orbx sys_sdk bridge at code_arena + 0x800
    uint8_t sdk_bridge[ORBX_SYS_SDK_BRIDGE_BYTES];
    if (build_orbx_sys_sdk_bridge(sdk_bridge, sizeof(sdk_bridge), info_addr) == 0) {
        kernel_proc_copyin(pid, sdk_bridge, (intptr_t)sys_sdk_bridge_addr, sizeof(sdk_bridge));
        log_msg("[HOOK] Staged Orbx sys_sdk bridge (%u bytes) @ 0x%llx\n",
                ORBX_SYS_SDK_BRIDGE_BYTES, (unsigned long long)sys_sdk_bridge_addr);
    }

    // 6. Stage Orbx native syscall bridge at code_arena + 0x880
    uint8_t sc_bridge[ORBX_SYSCALL_BRIDGE_BYTES];
    if (build_orbx_syscall_bridge(sc_bridge, sizeof(sc_bridge), native_gate) == 0) {
        kernel_proc_copyin(pid, sc_bridge, (intptr_t)syscall_bridge_addr, sizeof(sc_bridge));
        log_msg("[HOOK] Staged Orbx syscall bridge (%u bytes) @ 0x%llx (Gate: 0x%llx)\n",
                ORBX_SYSCALL_BRIDGE_BYTES, (unsigned long long)syscall_bridge_addr, (unsigned long long)native_gate);
    }

    // 7. Stage HhpRaiInfo at info_addr (ctrl_arena + 0x500)
    HhpRaiInfo rai_info;
    memset(&rai_info, 0, sizeof(rai_info));
    rai_info.pid = pid;
    strncpy(rai_info.name, "eboot.bin", sizeof(rai_info.name) - 1);
    strncpy(rai_info.path, "/app0/eboot.bin", sizeof(rai_info.path) - 1);
    strncpy(rai_info.title, "CUSA00419", sizeof(rai_info.title) - 1);
    strncpy(rai_info.content, "UP1004-CUSA00419_00-GTAV000000000001", sizeof(rai_info.content) - 1);
    strncpy(rai_info.version, "01.00", sizeof(rai_info.version) - 1);
    rai_info.base = eboot_base; // 0x400000
    kernel_proc_copyin(pid, &rai_info, (intptr_t)info_addr, sizeof(rai_info));
    log_msg("[HOOK] Staged HhpRaiInfo (%zu bytes) @ 0x%llx (base: 0x%llx, title: %s)\n",
            sizeof(rai_info), (unsigned long long)info_addr, (unsigned long long)rai_info.base, rai_info.title);

    // 8. Patch embedded orbx.prx in memory and deploy to disk before game loads it
    size_t embedded_prx_sz = (size_t)(_binary_orbx_prx_end - _binary_orbx_prx_start);
    deploy_patched_orbx(_binary_orbx_prx_start, embedded_prx_sz,
                        "/data/modHEN/plugins/orbx.prx",
                        sys_sdk_bridge_addr, syscall_bridge_addr);

    // 9. Write hyperhen_worker_blob (pure CPU instructions, zero data reads from .text)
    if (kernel_proc_copyin(pid, hyperhen_worker_blob, (intptr_t)code_arena, hyperhen_worker_blob_size) != 0) {
        log_msg("[HOOK] Error writing worker blob into arena @ 0x%llx\n", (unsigned long long)code_arena);
        return -1;
    }
    log_msg("[HOOK] Staged worker blob (%zu bytes) @ 0x%llx\n", hyperhen_worker_blob_size, (unsigned long long)code_arena);

    // 10. Initialize TacGameGate control structure in DATA memory
    uint64_t cookie = 0x4d4f4448454e3031ULL; // "MODHEN01"
    TacGameGate gate;
    memset(&gate, 0, sizeof(gate));
    gate.magic = TAC_GAME_GATE_MAGIC;
    gate.cookie = cookie;
    gate.loadStartModule = load_module;
    gate.version = TAC_GAME_GATE_VERSION;
    gate.bytes = sizeof(TacGameGate);
    gate.enabled = 1;
    gate.threadCreate = pthread_create;
    gate.threadDetach = pthread_detach;
    gate.dlsym = dlsym_func;

    // String constants stored in DATA memory (completely XOM-safe!)
    strncpy(gate.sym_plugin_load, "plugin_load", sizeof(gate.sym_plugin_load) - 1);
    strncpy(gate.sym_plugin_unload, "plugin_unload", sizeof(gate.sym_plugin_unload) - 1);
    strncpy(gate.thread_name, "modHEN module", sizeof(gate.thread_name) - 1);

    // Primary target in slot 0
    gate.modules[0].enabled = 1;
    gate.modules[0].state = TAC_MODULE_PENDING;
    strncpy(gate.modules[0].path, plugin_path, sizeof(gate.modules[0].path) - 1);

    // Alternative candidate paths in slots 1..4 (all stored in DATA memory)
    const char *fallbacks[] = {
        "/plugins/orbx.prx",
        "/data/modHEN/plugins/orbx.prx",
        "/data/plugins/orbx.prx",
        "/mnt/sandbox/CUSA00419_000/plugins/orbx.prx",
        "/mnt/sandbox/CUSA00419_001/plugins/orbx.prx"
    };
    unsigned slot_idx = 1;
    for (int f = 0; f < 5 && slot_idx < TAC_GAME_MODULE_SLOTS; f++) {
        if (strcmp(fallbacks[f], plugin_path) != 0) {
            gate.modules[slot_idx].enabled = 1;
            gate.modules[slot_idx].state = TAC_MODULE_PENDING;
            strncpy(gate.modules[slot_idx].path, fallbacks[f], sizeof(gate.modules[slot_idx].path) - 1);
            slot_idx++;
        }
    }

    if (kernel_proc_copyin(pid, &gate, (intptr_t)ctrl_arena, sizeof(gate)) != 0) {
        log_msg("[HOOK] Error writing gate control struct @ 0x%llx\n", (unsigned long long)ctrl_arena);
        return -1;
    }
    g_gate_control_addr = ctrl_arena;
    log_msg("[HOOK] Initialized TacGameGate in DATA segment @ 0x%llx (primary: %s)\n",
            (unsigned long long)ctrl_arena, plugin_path);

    // 11. Build HyperHEN SysV-Compliant Import Trampoline (43 bytes)
    // 100% XOM-Safe: calls scePadReadState via register (r11), preserves return code in ebx,
    // dispatches gate via register (rax), restores return code, and cleanly returns with ret.
    uint8_t stub[64];
    size_t pos = 0;

    // push rbx (aligns stack to 16 bytes for SysV AMD64 ABI)
    stub[pos++] = 0x53;

    // mov r11, pad_func
    stub[pos++] = 0x49; stub[pos++] = 0xbb;
    memcpy(stub + pos, &pad_func, 8); pos += 8;

    // call r11 (invokes scePadReadState directly)
    stub[pos++] = 0x41; stub[pos++] = 0xff; stub[pos++] = 0xd3;

    // mov ebx, eax (preserves original return value)
    stub[pos++] = 0x89; stub[pos++] = 0xc3;

    // mov rdi, ctrl_arena (arg1)
    stub[pos++] = 0x48; stub[pos++] = 0xbf;
    memcpy(stub + pos, &ctrl_arena, 8); pos += 8;

    // mov rsi, cookie (arg2)
    stub[pos++] = 0x48; stub[pos++] = 0xbe;
    memcpy(stub + pos, &cookie, 8); pos += 8;

    // mov edx, 0 (arg3: lane = 0)
    stub[pos++] = 0xba;
    uint32_t lane = 0;
    memcpy(stub + pos, &lane, 4); pos += 4;

    // mov rax, code_arena (tac_game_gate entry point)
    stub[pos++] = 0x48; stub[pos++] = 0xb8;
    memcpy(stub + pos, &code_arena, 8); pos += 8;

    // call rax
    stub[pos++] = 0xff; stub[pos++] = 0xd0;

    // mov eax, ebx (restore original scePadReadState return value)
    stub[pos++] = 0x89; stub[pos++] = 0xd8;

    // pop rbx
    stub[pos++] = 0x5b;

    // ret (return to GTA V caller)
    stub[pos++] = 0xc3;

    // Write stub to arena
    if (kernel_proc_copyin(pid, stub, (intptr_t)stub_addr, pos) != 0) {
        log_msg("[HOOK] Error writing trampoline stub to 0x%llx\n", (unsigned long long)stub_addr);
        return -1;
    }
    log_msg("[HOOK] Staged SysV Import Trampoline (%zu bytes) @ 0x%llx\n", pos, (unsigned long long)stub_addr);

    // 12. Arm Hook: Overwrite GOT slot in DATA segment
    kernel_set_vmem_protection(pid, (intptr_t)got_slot, 8, PROT_READ | PROT_WRITE);
    if (kernel_proc_copyin(pid, &stub_addr, (intptr_t)got_slot, sizeof(stub_addr)) != 0) {
        log_msg("[HOOK] Error overwriting GOT slot 0x%llx\n", (unsigned long long)got_slot);
        return -1;
    }
    log_msg("[HOOK] Arming Complete! GOT 0x%llx -> Trampoline 0x%llx\n",
            (unsigned long long)got_slot, (unsigned long long)stub_addr);

    // 13. Poll for module worker completion (up to 30 seconds for game pad initialization)
    for (int i = 0; i < 300; i++) {
        usleep(100000); // 100ms
        TacGameModule slot;
        if (kernel_proc_copyout(pid, (intptr_t)(ctrl_arena + offsetof(TacGameGate, modules[0])), &slot, sizeof(slot)) == 0) {
            if (slot.state == TAC_MODULE_STARTED) {
                log_msg("[HOOK] Module Loaded Successfully! Handle: %d, Result: %d (Active Path: %s)\n",
                        slot.handle, slot.startResult, slot.path);
                
                // Restore original GOT slot so controller polling returns to native speed
                kernel_proc_copyin(pid, &g_orig_pad_func, (intptr_t)got_slot, sizeof(g_orig_pad_func));
                log_msg("[HOOK] Restored GOT slot 0x%llx -> 0x%llx (Native Pad Performance Restored)\n",
                        (unsigned long long)got_slot, (unsigned long long)g_orig_pad_func);

                notify("GTA V: Injected orbx.prx! Press L1+[]");
                if (out_module_handle) *out_module_handle = (uint32_t)slot.handle;
                return 0;
            } else if (slot.state == TAC_MODULE_FAILED) {
                log_msg("[HOOK] Module Load Failed! Handle: %d, Result: %d (Phase: %u, Last Path: %s)\n",
                        slot.handle, slot.startResult, slot.phase, slot.path);
                return -1;
            } else if (slot.state == TAC_MODULE_REJECTED) {
                log_msg("[HOOK] Module Load Rejected! State: %u (Phase: %u, ThreadRes: %d, Path: %s)\n",
                        slot.state, slot.phase, slot.threadResult, slot.path);
                return -1;
            }
        }
        if ((i + 1) % 50 == 0) {
            log_msg("[HOOK] Waiting for game pad initialization... (%d/30s)\n", (i + 1) / 10);
        }
    }

    log_msg("[HOOK] Hook armed and active (pending first pad poll by game thread)\n");
    return 0;
}

int query_module_status(pid_t pid, int *out_state, int *out_handle) {
    if (!g_gate_control_addr || pid <= 1) return -1;

    TacGameModule slot;
    if (kernel_proc_copyout(pid, (intptr_t)(g_gate_control_addr + offsetof(TacGameGate, modules[0])), &slot, sizeof(slot)) != 0) {
        return -1;
    }

    if (out_state)  *out_state  = (int)slot.state;
    if (out_handle) *out_handle = slot.handle;
    return 0;
}
