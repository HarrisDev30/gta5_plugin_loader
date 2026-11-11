/*
 * Copyright (C) 2024 John Törnblom
 * Adapted for PS5 GTA V modHEN Loader
 *
 * ptrace process control and remote invocation engine.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <machine/reg.h>

#include <ps5/kernel.h>
#include <ps5/nid.h>

#include "pt.h"
#include "log.h"

/*
 * Private stack window for PT_SETREGS calls.
 * Red zone is [rsp-128, rsp); 0x108 clears it and preserves 8 mod 16 alignment.
 */
static uintptr_t pt_private_entry_rsp(uintptr_t interrupted_rsp) {
    return (interrupted_rsp & ~(uintptr_t)0xf) - (uintptr_t)0x108;
}

static int sys_ptrace(int request, pid_t pid, caddr_t addr, int data) {
    return (int)syscall(SYS_ptrace, request, pid, addr, data);
}

intptr_t pt_resolve(pid_t pid, const char* nid) {
    uint32_t handle = 0;
    // 1. Try PS4 libkernel.sprx first for PS4 backward-compatibility processes
    if (kernel_dynlib_handle(pid, "libkernel.sprx", &handle) == 0 && handle != 0) {
        intptr_t addr = kernel_dynlib_resolve(pid, handle, nid);
        if (addr) return addr;
    }
    // 2. Try native PS5 libkernel_sys.sprx
    if (kernel_dynlib_handle(pid, "libkernel_sys.sprx", &handle) == 0 && handle != 0) {
        intptr_t addr = kernel_dynlib_resolve(pid, handle, nid);
        if (addr) return addr;
    }
    // 3. Fallback to fixed handles
    intptr_t addr;
    if ((addr = kernel_dynlib_resolve(pid, 0x1, nid))) {
        return addr;
    }
    return kernel_dynlib_resolve(pid, 0x2001, nid);
}

intptr_t pt_dlsym(pid_t pid, const char* sym) {
    uint32_t handle = 0;
    if (kernel_dynlib_handle(pid, "libkernel.sprx", &handle) == 0 && handle != 0) {
        intptr_t addr = kernel_dynlib_dlsym(pid, handle, sym);
        if (addr) return addr;
    }
    if (kernel_dynlib_handle(pid, "libkernel_sys.sprx", &handle) == 0 && handle != 0) {
        intptr_t addr = kernel_dynlib_dlsym(pid, handle, sym);
        if (addr) return addr;
    }
    intptr_t addr;
    if ((addr = kernel_dynlib_dlsym(pid, 0x1, sym))) {
        return addr;
    }
    if ((addr = kernel_dynlib_dlsym(pid, 0x2001, sym))) {
        return addr;
    }
    char enc[16] = {0};
    nid_encode(sym, enc);
    return pt_resolve(pid, enc);
}

int pt_attach(pid_t pid) {
    int status = 0;
    if (sys_ptrace(PT_ATTACH, pid, 0, 0) == -1) {
        log_msg("[PT] sys_ptrace(PT_ATTACH, %d) failed: errno %d\n", pid, errno);
        return -1;
    }
    if (waitpid(pid, &status, WUNTRACED) == -1) {
        log_msg("[PT] waitpid(%d) after attach failed: errno %d\n", pid, errno);
        return -1;
    }
    if (!WIFSTOPPED(status)) {
        log_msg("[PT] PID %d not stopped after attach (status=0x%x)\n", pid, status);
        errno = ESRCH;
        return -1;
    }
    return 0;
}

int pt_detach(pid_t pid, int sig) {
    if (sys_ptrace(PT_DETACH, pid, 0, sig) == -1) {
        log_msg("[PT] sys_ptrace(PT_DETACH, %d) failed: errno %d\n", pid, errno);
        return -1;
    }
    return 0;
}

int pt_step(pid_t pid) {
    if (sys_ptrace(PT_STEP, pid, (caddr_t)1, 0) != 0) {
        return -1;
    }
    if (waitpid(pid, 0, 0) < 0) {
        return -1;
    }
    return 0;
}

int pt_continue(pid_t pid, int sig) {
    if (sys_ptrace(PT_CONTINUE, pid, (caddr_t)1, sig) == -1) {
        return -1;
    }
    return 0;
}

int pt_getregs(pid_t pid, struct reg *r) {
    return sys_ptrace(PT_GETREGS, pid, (caddr_t)r, 0);
}

int pt_setregs(pid_t pid, const struct reg *r) {
    return sys_ptrace(PT_SETREGS, pid, (caddr_t)r, 0);
}

int pt_copyin(pid_t pid, const void* buf, intptr_t addr, size_t len) {
    struct ptrace_io_desc iod = {
        .piod_op = PIOD_WRITE_D,
        .piod_offs = (void*)addr,
        .piod_addr = (void*)buf,
        .piod_len = len
    };
    if (sys_ptrace(PT_IO, pid, (caddr_t)&iod, 0) == 0) {
        return 0;
    }
    return kernel_proc_copyin(pid, buf, addr, len);
}

int pt_copyout(pid_t pid, intptr_t addr, void* buf, size_t len) {
    struct ptrace_io_desc iod = {
        .piod_op = PIOD_READ_D,
        .piod_offs = (void*)addr,
        .piod_addr = buf,
        .piod_len = len
    };
    if (sys_ptrace(PT_IO, pid, (caddr_t)&iod, 0) == 0) {
        return 0;
    }
    return kernel_proc_copyout(pid, addr, buf, len);
}

long pt_call(pid_t pid, intptr_t addr, ...) {
    struct reg jmp_reg;
    struct reg bak_reg;
    uintptr_t entry_rsp;
    va_list ap;

    if (pt_getregs(pid, &bak_reg)) {
        log_msg("[PT] pt_call: failed to getregs\n");
        return -1;
    }

    memcpy(&jmp_reg, &bak_reg, sizeof(jmp_reg));
    jmp_reg.r_rip = addr;

    entry_rsp = pt_private_entry_rsp((uintptr_t)bak_reg.r_rsp);
    jmp_reg.r_rsp = entry_rsp;

    va_start(ap, addr);
    jmp_reg.r_rdi = va_arg(ap, uint64_t);
    jmp_reg.r_rsi = va_arg(ap, uint64_t);
    jmp_reg.r_rdx = va_arg(ap, uint64_t);
    jmp_reg.r_rcx = va_arg(ap, uint64_t);
    jmp_reg.r_r8  = va_arg(ap, uint64_t);
    jmp_reg.r_r9  = va_arg(ap, uint64_t);
    va_end(ap);

    if (pt_setregs(pid, &jmp_reg)) {
        log_msg("[PT] pt_call: failed to setregs\n");
        return -1;
    }

    /* Single-step until the remote function's final ret raises rsp past entry. */
    while ((uintptr_t)jmp_reg.r_rsp <= entry_rsp) {
        if (pt_step(pid)) {
            log_msg("[PT] pt_call: step failed\n");
            return -1;
        }
        if (pt_getregs(pid, &jmp_reg)) {
            log_msg("[PT] pt_call: getregs failed\n");
            return -1;
        }
    }

    // restore registers
    if (pt_setregs(pid, &bak_reg)) {
        log_msg("[PT] pt_call: failed to restore regs\n");
        return -1;
    }

    return jmp_reg.r_rax;
}

long pt_syscall(pid_t pid, int sysno, ...) {
    intptr_t addr = pt_resolve(pid, "HoLVWNanBBc"); // getpid stub
    struct reg jmp_reg;
    struct reg bak_reg;
    uintptr_t entry_rsp;
    va_list ap;

    if (!addr) {
        log_msg("[PT] pt_syscall: failed to resolve getpid stub (HoLVWNanBBc)\n");
        return -1;
    } else {
        addr += 0xa;
    }

    if (pt_getregs(pid, &bak_reg)) {
        return -1;
    }

    memcpy(&jmp_reg, &bak_reg, sizeof(jmp_reg));
    jmp_reg.r_rip = addr;
    jmp_reg.r_rax = sysno;

    entry_rsp = pt_private_entry_rsp((uintptr_t)bak_reg.r_rsp);
    jmp_reg.r_rsp = entry_rsp;

    va_start(ap, sysno);
    jmp_reg.r_rdi = va_arg(ap, uint64_t);
    jmp_reg.r_rsi = va_arg(ap, uint64_t);
    jmp_reg.r_rdx = va_arg(ap, uint64_t);
    jmp_reg.r_r10 = va_arg(ap, uint64_t);
    jmp_reg.r_r8  = va_arg(ap, uint64_t);
    jmp_reg.r_r9  = va_arg(ap, uint64_t);
    va_end(ap);

    if (pt_setregs(pid, &jmp_reg)) {
        return -1;
    }

    while ((uintptr_t)jmp_reg.r_rsp <= entry_rsp) {
        if (pt_step(pid)) {
            return -1;
        }
        if (pt_getregs(pid, &jmp_reg)) {
            return -1;
        }
    }

    if (pt_setregs(pid, &bak_reg)) {
        return -1;
    }

    return jmp_reg.r_rax;
}

intptr_t pt_mmap(pid_t pid, intptr_t addr, size_t len, int prot, int flags, int fd, off_t off) {
    return pt_syscall(pid, SYS_mmap, addr, len, prot, flags, fd, off);
}

int pt_mprotect(pid_t pid, intptr_t addr, size_t len, int prot) {
    return pt_syscall(pid, SYS_mprotect, addr, len, prot);
}

int pt_munmap(pid_t pid, intptr_t addr, size_t len) {
    return pt_syscall(pid, SYS_munmap, addr, len);
}

