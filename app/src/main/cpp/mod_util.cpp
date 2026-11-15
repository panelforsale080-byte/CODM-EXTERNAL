#include "mod_util.hpp"
#include "mem.hpp"
#include <cstring>

uintptr_t mod_base(pid_t pid, const char *libName, char *pathOut, size_t pathCap) {
    if (!pid || !libName) return 0;
    std::vector<MapsReg> maps;
    if (!mem_maps(pid, maps)) return 0;
    uintptr_t best = 0;
    bool have = false;
    for (const auto &r : maps) {
        if (r.path.find(libName) == std::string::npos) continue;
        if (r.fileOff == 0) {
            if (pathOut && pathCap) std::snprintf(pathOut, pathCap, "%s", r.path.c_str());
            return r.start;
        }
        if (!have || r.start < best) {
            best = r.start;
            have = true;
            if (pathOut && pathCap) std::snprintf(pathOut, pathCap, "%s", r.path.c_str());
        }
    }
    return have ? best : 0;
}