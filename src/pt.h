#pragma once
#include <stdint.h>
#include <sys/types.h>
#include <machine/reg.h>

#ifdef __cplusplus
extern "C" {
#endif

int pt_attach(pid_t pid);
int pt_detach(pid_t pid, int sig);
int pt_step(pid_t pid);
int pt_continue(pid_t pid, int sig);
int pt_getregs(pid_t pid, struct reg *r);
int pt_setregs(pid_t pid, const struct reg *r);
int pt_copyin(pid_t pid, const void* buf, intptr_t addr, size_t len);
int pt_copyout(pid_t pid, intptr_t addr, void* buf, size_t len);
long pt_syscall(pid_t pid, int sysno, ...);
intptr_t pt_resolve(pid_t pid, const char* nid);
intptr_t pt_dlsym(pid_t pid, const char* sym);
intptr_t pt_mmap(pid_t pid, intptr_t addr, size_t len, int prot, int flags, int fd, off_t off);
int pt_mprotect(pid_t pid, intptr_t addr, size_t len, int prot);
int pt_munmap(pid_t pid, intptr_t addr, size_t len);
long pt_call(pid_t pid, intptr_t addr, ...);

#ifdef __cplusplus
}
#endif

