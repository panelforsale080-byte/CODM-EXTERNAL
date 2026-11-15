#pragma once
#include <cstdint>
#include <sys/types.h>

uintptr_t mod_base(pid_t pid, const char *libName, char *pathOut, size_t pathCap);