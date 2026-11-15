#include "mem.hpp"
#include "external_esp_read.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

static std::mutex g_mapsMu;
static pid_t g_mapsPid = 0;
static std::vector<MapsReg> g_mapsCache;

bool mem_ptr_ok(uintptr_t p) {
    return p >= 0x10000ULL && p < 0x800000000000ULL && ((p & 7ULL) == 0);
}

bool mem_maps(pid_t pid, std::vector<MapsReg> &out) {
    out.clear();
    std::ifstream maps("/proc/" + std::to_string(pid) + "/maps");
    if (!maps) return false;
    std::string line;
    while (std::getline(maps, line)) {
        MapsReg r{};
        char path[512] = {};
        unsigned long off = 0;
        if (std::sscanf(line.c_str(), "%lx-%lx %7s %lx %*s %*s %511[^\n]", &r.start, &r.end,
                        r.perms, &off, path) < 4)
            continue;
        r.fileOff = off;
        r.path = path;
        while (!r.path.empty() && (r.path[0] == ' ' || r.path[0] == '\t')) r.path.erase(0, 1);
        if (r.path.empty() || r.path.find(".so") == std::string::npos) {
            char linkPath[96];
            char target[512];
            std::snprintf(linkPath, sizeof(linkPath), "/proc/%d/map_files/%lx-%lx", (int)pid, r.start, r.end);
            const ssize_t n = readlink(linkPath, target, sizeof(target) - 1);
            if (n > 0) {
                target[n] = '\0';
                r.path = target;
            }
        }
        out.push_back(r);
    }
    std::sort(out.begin(), out.end(),
              [](const MapsReg &a, const MapsReg &b) { return a.start < b.start; });
    return !out.empty();
}

void mem_invalidate_maps_cache(pid_t pid) {
    std::lock_guard<std::mutex> lock(g_mapsMu);
    if (pid <= 0 || g_mapsPid == pid) {
        g_mapsPid = 0;
        g_mapsCache.clear();
    }
}

bool mem_get_maps(pid_t pid, std::vector<MapsReg> &out) {
    if (pid <= 0) return false;
    std::lock_guard<std::mutex> lock(g_mapsMu);
    if (g_mapsPid != pid || g_mapsCache.empty()) {
        g_mapsCache.clear();
        if (!mem_maps(pid, g_mapsCache)) return false;
        g_mapsPid = pid;
    }
    out = g_mapsCache;
    return true;
}

bool mem_is_readable(pid_t pid, uintptr_t addr, size_t len) {
    std::vector<MapsReg> maps;
    if (!mem_get_maps(pid, maps)) return false;
    return ext_read::region_readable(maps, addr, len);
}

bool mem_read_batch(pid_t pid, const std::vector<std::pair<uintptr_t, size_t>> &reqs,
                    std::vector<std::vector<uint8_t>> &out) {
    if (!pid || reqs.empty()) return false;
    std::vector<MapsReg> maps;
    if (!mem_get_maps(pid, maps)) return false;
    for (const auto &r : reqs) {
        if (!ext_read::region_readable(maps, r.first, r.second)) {
            mem_invalidate_maps_cache(pid);
            if (!mem_get_maps(pid, maps)) return false;
            if (!ext_read::region_readable(maps, r.first, r.second)) return false;
        }
    }
    const ssize_t n = ext_read::read_batch(pid, reqs, out);
    if (n <= 0) return false;
    size_t need = 0;
    for (const auto &r : reqs) need += r.second;
    return (size_t)n >= need;
}

bool mem_read(pid_t pid, uintptr_t addr, void *buf, size_t n) {
    if (!pid || !addr || !buf || !n) return false;
    iovec local{(void *)buf, n};
    iovec remote{(void *)addr, n};
    const ssize_t ret = mem_vm_readv(pid, &local, 1, &remote, 1, 0);
    return ret == (ssize_t)n;
}

bool mem_read_ptrace(pid_t pid, uintptr_t addr, void *buf, size_t n) {
    if (!pid || !addr || !buf || !n) return false;
    if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) < 0) return false;
    int st = 0;
    if (waitpid(pid, &st, __WALL) < 0) {
        ptrace(PTRACE_DETACH, pid, nullptr, nullptr);
        return false;
    }
    bool ok = true;
    auto *dst = static_cast<uint8_t *>(buf);
    uintptr_t a = addr;
    size_t left = n;
    while (left && ok) {
        const uintptr_t aligned = a & ~7ULL;
        errno = 0;
        const long word = ptrace(PTRACE_PEEKDATA, pid, reinterpret_cast<void *>(aligned), nullptr);
        if (errno) {
            ok = false;
            break;
        }
        uint8_t bytes[8];
        std::memcpy(bytes, &word, 8);
        const size_t off = (size_t)(a - aligned);
        const size_t chunk = std::min(left, (size_t)8 - off);
        std::memcpy(dst, bytes + off, chunk);
        a += chunk;
        dst += chunk;
        left -= chunk;
    }
    ptrace(PTRACE_DETACH, pid, nullptr, nullptr);
    return ok;
}

static char g_writeErr[160] = "ok";

const char *mem_write_error() { return g_writeErr; }

static bool write_proc_mem(pid_t pid, uintptr_t addr, const void *buf, size_t n) {
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/mem", (int)pid);
    const int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) return false;
    const ssize_t w = pwrite(fd, buf, n, (off_t)addr);
    close(fd);
    return w == (ssize_t)n;
}

static bool write_ptrace(pid_t pid, uintptr_t addr, const void *buf, size_t n) {
    if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) < 0) return false;
    int st = 0;
    if (waitpid(pid, &st, __WALL) < 0) {
        ptrace(PTRACE_DETACH, pid, nullptr, nullptr);
        return false;
    }
    bool ok = true;
    const auto *src = static_cast<const uint8_t *>(buf);
    uintptr_t a = addr;
    size_t left = n;
    while (left && ok) {
        const uintptr_t aligned = a & ~7ULL;
        errno = 0;
        const long word = ptrace(PTRACE_PEEKTEXT, pid, reinterpret_cast<void *>(aligned), nullptr);
        if (errno) {
            ok = false;
            break;
        }
        uint8_t bytes[8];
        std::memcpy(bytes, &word, 8);
        const size_t off = (size_t)(a - aligned);
        const size_t chunk = std::min(left, (size_t)8 - off);
        std::memcpy(bytes + off, src, chunk);
        long out = 0;
        std::memcpy(&out, bytes, 8);
        if (ptrace(PTRACE_POKETEXT, pid, reinterpret_cast<void *>(aligned), reinterpret_cast<void *>(out)) < 0)
            ok = false;
        a += chunk;
        src += chunk;
        left -= chunk;
    }
    ptrace(PTRACE_DETACH, pid, nullptr, nullptr);
    return ok;
}

bool mem_write(pid_t pid, uintptr_t addr, const void *buf, size_t n) {
    g_writeErr[0] = '\0';
    if (!pid || !addr || !buf || !n) {
        std::snprintf(g_writeErr, sizeof(g_writeErr), "bad args");
        return false;
    }
    iovec local{(void *)buf, n};
    iovec remote{(void *)addr, n};
    errno = 0;
    if (mem_vm_writev(pid, &local, 1, &remote, 1, 0) == (ssize_t)n) {
        std::snprintf(g_writeErr, sizeof(g_writeErr), "vmwrite");
        return true;
    }
    const int eVm = errno ? errno : EIO;
    errno = 0;
    if (write_proc_mem(pid, addr, buf, n)) {
        std::snprintf(g_writeErr, sizeof(g_writeErr), "procmem");
        return true;
    }
    const int eProc = errno ? errno : EIO;
    errno = 0;
    if (write_ptrace(pid, addr, buf, n)) {
        std::snprintf(g_writeErr, sizeof(g_writeErr), "ptrace");
        return true;
    }
    const int ePtr = errno ? errno : EIO;
    char sVm[32], sProc[32], sPtr[32];
    std::snprintf(sVm, sizeof(sVm), "%s", std::strerror(eVm));
    std::snprintf(sProc, sizeof(sProc), "%s", std::strerror(eProc));
    std::snprintf(sPtr, sizeof(sPtr), "%s", std::strerror(ePtr));
    std::snprintf(g_writeErr, sizeof(g_writeErr), "vm=%s proc=%s ptrace=%s", sVm, sProc, sPtr);
    return false;
}
