/*
 * HyperHEN-Style Native Game-Owned Plugin Injector for PS5
 *
 * Implements John Törnblom's ptrace injection mechanics:
 * 1. Allocates anonymous memory pages in game address space via pt_mmap (Bypasses PS5 XOM)
 * 2. Stages position-independent 222-byte worker blob (PROT_READ|PROT_EXEC)
 * 3. Spawns game-owned thread via scePthreadCreate to load module in-process
 * 4. Detaches ptrace immediately - zero eboot.bin / GOT modifications
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

#include <ps5/kernel.h>
#include <ps5/nid.h>

#include "pt.h"
#include "log.h"
#include "hyperhen_injector.h"

#define PTRACE_AUTHID 0x4800000000010003ULL
#define PAGE_SZ       0x4000

// Authentic HyperHEN position-independent 222-byte worker blob
static const uint8_t g_hh_worker_bin[222] = {
    0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x48, 0x83, 0xEC, 0x18, 0x48, 0x89, 0xFB,
    0x48, 0x83, 0xC7, 0x24, 0x4C, 0x8D, 0x4D, 0xE4, 0x31, 0xF6, 0x31, 0xD2, 0x31, 0xC9, 0x45, 0x31,
    0xC0, 0xC7, 0x45, 0xE4, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x13, 0x89, 0x43, 0x10, 0x41, 0xBE, 0x02,
    0x00, 0x00, 0x00, 0x8B, 0x4D, 0xE4, 0x89, 0x4B, 0x14, 0x09, 0xC1, 0x78, 0x74, 0x48, 0xC7, 0x45,
    0xD8, 0x00, 0x00, 0x00, 0x00, 0x48, 0xC7, 0x45, 0xD0, 0x00, 0x00, 0x00, 0x00, 0x41, 0xBE, 0x01,
    0x00, 0x00, 0x00, 0x4C, 0x8B, 0x7B, 0x08, 0x4D, 0x85, 0xFF, 0x74, 0x55, 0x48, 0x8D, 0x35, 0x6F,
    0x00, 0x00, 0x00, 0x48, 0x8D, 0x55, 0xD8, 0x89, 0xC7, 0x41, 0xFF, 0xD7, 0x85, 0xC0, 0x75, 0x41,
    0x48, 0x83, 0x7D, 0xD8, 0x00, 0x74, 0x3A, 0x8B, 0x7B, 0x10, 0x48, 0x8D, 0x35, 0x43, 0x00, 0x00,
    0x00, 0x48, 0x8D, 0x55, 0xD0, 0x41, 0xFF, 0xD7, 0x85, 0xC0, 0x75, 0x25, 0x48, 0x83, 0x7D, 0xD0,
    0x00, 0x74, 0x1E, 0x31, 0xFF, 0x31, 0xF6, 0xC7, 0x43, 0x1C, 0x01, 0x00, 0x00, 0x00, 0xFF, 0x55,
    0xD8, 0x83, 0xF8, 0x01, 0x41, 0xBE, 0x02, 0x00, 0x00, 0x00, 0x89, 0x43, 0x18, 0x41, 0x83, 0xDE,
    0x00, 0x44, 0x89, 0x73, 0x20, 0x31, 0xC0, 0x48, 0x83, 0xC4, 0x18, 0x5B, 0x41, 0x5E, 0x41, 0x5F,
    0x5D, 0xC3, 0xCC, 0xCC, 0x70, 0x6C, 0x75, 0x67, 0x69, 0x6E, 0x5F, 0x75, 0x6E, 0x6C, 0x6F, 0x61,
    0x64, 0x00, 0x70, 0x6C, 0x75, 0x67, 0x69, 0x6E, 0x5F, 0x6C, 0x6F, 0x61, 0x64, 0x00
};

enum {
    HH_PLUGIN_PENDING = 0,
    HH_PLUGIN_STARTED = 1,
    HH_PLUGIN_FAILED  = 2
};

typedef struct {
    uint64_t load_start_module; // 0x00
    uint64_t dlsym;             // 0x08
    int32_t  handle;            // 0x10
    int32_t  start_result;      // 0x14
    int32_t  activation_result; // 0x18
    uint32_t activated;         // 0x1c
    uint32_t state;             // 0x20
    char     path[512];         // 0x24
} __attribute__((packed)) hyperhen_worker_ctx_t;

static intptr_t resolve_sym(pid_t pid, const char *nid, const char *name) {
    intptr_t addr = 0;
    if (nid) {
        addr = pt_resolve(pid, nid);
        if (addr) return addr;
    }
    if (name) {
        addr = pt_dlsym(pid, name);
        if (addr) return addr;
    }
    return 0;
}

int inject_plugin_hyperhen(pid_t pid, const char *plugin_path, uint32_t *out_handle) {
    log_msg("[HYPERHEN] Starting Authentic HyperHEN ptrace injection for PID %d...\n", pid);
    log_msg("[HYPERHEN] Target plugin: %s\n", plugin_path);

    // 1. Ensure our ucred has PTRACE_AUTHID
    uint64_t old_auth = kernel_get_ucred_authid(getpid());
    kernel_set_ucred_authid(getpid(), PTRACE_AUTHID);

    // 2. Attach to target process via ptrace
    if (pt_attach(pid) != 0) {
        log_msg("[HYPERHEN] Error: pt_attach(PID %d) failed!\n", pid);
        if (old_auth) kernel_set_ucred_authid(getpid(), old_auth);
        return -1;
    }
    log_msg("[HYPERHEN] Attached to PID %d successfully!\n", pid);

    // 3. Resolve required dynamic symbols in game process
    intptr_t load_fn   = resolve_sym(pid, "wzvqT4UqKX8", "sceKernelLoadStartModule");
    intptr_t create_fn = resolve_sym(pid, "6UgtwV+0zb4", "scePthreadCreate");
    if (!create_fn) {
        create_fn = resolve_sym(pid, "OxhIB8LB-PQ", "pthread_create");
    }
    intptr_t dlsym_fn  = resolve_sym(pid, "LwG8g3niqwA", "sceKernelDlsym");
    intptr_t detach_fn = resolve_sym(pid, "4qGrR6eoP9Y", "scePthreadDetach");
    if (!detach_fn) {
        detach_fn = resolve_sym(pid, NULL, "pthread_detach");
    }

    log_msg("[HYPERHEN] Resolved sceKernelLoadStartModule @ 0x%lx\n", (unsigned long)load_fn);
    log_msg("[HYPERHEN] Resolved scePthreadCreate         @ 0x%lx\n", (unsigned long)create_fn);
    log_msg("[HYPERHEN] Resolved sceKernelDlsym           @ 0x%lx\n", (unsigned long)dlsym_fn);
    log_msg("[HYPERHEN] Resolved scePthreadDetach         @ 0x%lx\n", (unsigned long)detach_fn);

    if (!load_fn || !create_fn) {
        log_msg("[HYPERHEN] Error: Critical symbols failed to resolve!\n");
        pt_detach(pid, 0);
        if (old_auth) kernel_set_ucred_authid(getpid(), old_auth);
        return -2;
    }

    // 4. Allocate anonymous memory pages in target process
    // Anonymous pages allocated via mmap in userland DO NOT trigger PS5 hardware XOM traps!
    intptr_t data_page = pt_mmap(pid, 0, PAGE_SZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    intptr_t code_page = pt_mmap(pid, 0, PAGE_SZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    log_msg("[HYPERHEN] Allocated Data Page @ 0x%lx (RW)\n", (unsigned long)data_page);
    log_msg("[HYPERHEN] Allocated Code Page @ 0x%lx (RWX)\n", (unsigned long)code_page);

    if (data_page <= 0 || code_page <= 0) {
        log_msg("[HYPERHEN] Error: pt_mmap failed in target PID %d!\n", pid);
        pt_detach(pid, 0);
        if (old_auth) kernel_set_ucred_authid(getpid(), old_auth);
        return -3;
    }

    // 5. Setup worker context
    hyperhen_worker_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.load_start_module = (uint64_t)load_fn;
    ctx.dlsym = (uint64_t)dlsym_fn;
    ctx.handle = -1;
    ctx.state = HH_PLUGIN_PENDING;
    strncpy(ctx.path, plugin_path, sizeof(ctx.path) - 1);

    const char worker_name[] = "modHEN plugin";

    // Stage context, thread name, and 222-byte position-independent worker blob
    if (pt_copyin(pid, &ctx, data_page, sizeof(ctx)) != 0 ||
        pt_copyin(pid, worker_name, data_page + 0x800, sizeof(worker_name)) != 0 ||
        pt_copyin(pid, g_hh_worker_bin, code_page, sizeof(g_hh_worker_bin)) != 0 ||
        pt_mprotect(pid, code_page, PAGE_SZ, PROT_READ | PROT_EXEC) != 0) {
        log_msg("[HYPERHEN] Error: Staging worker code/data into target failed!\n");
        pt_detach(pid, 0);
        if (old_auth) kernel_set_ucred_authid(getpid(), old_auth);
        return -4;
    }

    log_msg("[HYPERHEN] Staged 222-byte worker blob @ 0x%lx (PROT_READ|PROT_EXEC)\n", (unsigned long)code_page);
    log_msg("[HYPERHEN] Context initialized with module path: %s\n", ctx.path);

    // 6. Spawn game-owned worker thread via scePthreadCreate
    // Prototype: int scePthreadCreate(void **thread, const void *attr, void *(*entry)(void *), void *arg, const char *name)
    long created = pt_call(pid, create_fn, data_page + 0x1000, 0, code_page, data_page, data_page + 0x800, 0);
    if (created != 0) {
        log_msg("[HYPERHEN] Error: scePthreadCreate returned %ld\n", created);
        pt_detach(pid, 0);
        if (old_auth) kernel_set_ucred_authid(getpid(), old_auth);
        return -5;
    }

    uint64_t thread = 0;
    pt_copyout(pid, data_page + 0x1000, &thread, sizeof(thread));
    log_msg("[HYPERHEN] Spawned game worker thread: 0x%lx\n", (unsigned long)thread);

    if (thread != 0 && detach_fn != 0) {
        pt_call(pid, detach_fn, thread, 0, 0, 0, 0, 0);
        log_msg("[HYPERHEN] Detached worker thread: 0x%lx\n", (unsigned long)thread);
    }

    // 7. Detach ptrace immediately so game continues normal execution
    if (pt_detach(pid, 0) != 0) {
        log_msg("[HYPERHEN] Warning: pt_detach returned non-zero\n");
    } else {
        log_msg("[HYPERHEN] Detached ptrace. Game running freely.\n");
    }

    if (old_auth) kernel_set_ucred_authid(getpid(), old_auth);

    // 8. Poll worker completion via kernel memory read (up to 5 seconds)
    log_msg("[HYPERHEN] Polling module load completion in game thread...\n");
    int finished = 0;
    for (int i = 0; i < 100; i++) {
        usleep(50000); // 50ms
        memset(&ctx, 0, sizeof(ctx));
        if (kernel_proc_copyout(pid, data_page, &ctx, sizeof(ctx)) == 0) {
            if (ctx.state == HH_PLUGIN_STARTED || ctx.state == HH_PLUGIN_FAILED) {
                finished = 1;
                break;
            }
        }
    }

    if (!finished) {
        log_msg("[HYPERHEN] Timeout: Worker did not complete within 5 seconds (state: %u, handle: %d)\n",
                ctx.state, ctx.handle);
        return -6;
    }

    if (ctx.state == HH_PLUGIN_STARTED) {
        log_msg("[HYPERHEN] SUCCESS! Module Loaded & Initialized! Handle: 0x%08x (%d), Start Result: %d\n",
                (unsigned int)ctx.handle, ctx.handle, ctx.start_result);
        if (ctx.activated) {
            log_msg("[HYPERHEN] Extra Activation (plugin_load) Result: %d\n", ctx.activation_result);
        }
        if (out_handle) *out_handle = (uint32_t)ctx.handle;
        return 0;
    } else {
        log_msg("[HYPERHEN] Module Load Failed! Handle: %d, Result: %d\n", ctx.handle, ctx.start_result);
        return -7;
    }
}

