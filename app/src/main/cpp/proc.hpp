#pragma once
#include <cstddef>
#include <sys/types.h>

struct ProcCandidate {
    pid_t pid;
    char cmdline[256];
    int score;
    int unity;
    int anogs;
};

pid_t proc_find_codm_best();
void proc_format_cmdline(pid_t pid, char *out, size_t outCap);
size_t proc_list_codm(ProcCandidate *out, size_t maxOut);
bool proc_alive(pid_t pid);
bool proc_probe_read(pid_t pid);