#pragma once
#include <sys/types.h>

/** NOP/ret patches from jnmaini hooks.h BYPASS() — applied via process_vm_writev. */
int anogs_apply_bypass(pid_t pid, uintptr_t libanogsBase, char *log, size_t logCap);