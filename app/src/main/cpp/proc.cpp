#include "proc.hpp"
#include "mem.hpp"
#include <dirent.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

static constexpr const char *kCodm = "com.garena.game.codm";

static std::string read_cmdline(pid_t pid) {
    std::ifstream f("/proc/" + std::to_string(pid) + "/cmdline", std::ios::binary);
    if (!f) return {};
    std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    for (char &c : all)
        if (c == '\0') c = ' ';
    while (!all.empty() && all.back() == ' ') all.pop_back();
    return all;
}

static bool maps_hits(pid_t pid, const char *tag) {
    std::ifstream maps("/proc/" + std::to_string(pid) + "/maps");
    std::string line;
    while (std::getline(maps, line)) {
        if (line.find(tag) != std::string::npos) return true;
    }
    return false;
}

static int score_pid(pid_t pid, int &unity, int &anogs) {
    unity = maps_hits(pid, "libunity.so") ? 1 : 0;
    anogs = maps_hits(pid, "libanogs.so") ? 1 : 0;
    const std::string cmd = read_cmdline(pid);
    if (cmd.find(kCodm) == std::string::npos) return 0;
    int s = 200;
    if (unity) s += 60;
    if (anogs) s += 80;
    if (cmd.find(":") != std::string::npos) s += 10;
    return s;
}

void proc_format_cmdline(pid_t pid, char *out, size_t outCap) {
    if (!out || !outCap) return;
    std::snprintf(out, outCap, "%s", read_cmdline(pid).c_str());
}

size_t proc_list_codm(ProcCandidate *out, size_t maxOut) {
    if (!out || !maxOut) return 0;
    DIR *d = opendir("/proc");
    if (!d) return 0;
    size_t n = 0;
    for (struct dirent *e = readdir(d); e; e = readdir(d)) {
        if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
        const pid_t pid = (pid_t)std::atoi(e->d_name);
        if (pid <= 0) continue;
        ProcCandidate c{};
        c.pid = pid;
        proc_format_cmdline(pid, c.cmdline, sizeof(c.cmdline));
        c.score = score_pid(pid, c.unity, c.anogs);
        if (c.score <= 0) continue;
        out[n++] = c;
        if (n >= maxOut) break;
    }
    closedir(d);
    return n;
}

pid_t proc_find_codm_best() {
    ProcCandidate buf[32];
    const size_t n = proc_list_codm(buf, 32);
    pid_t best = 0;
    int bestScore = 0;
    for (size_t i = 0; i < n; ++i) {
        if (buf[i].score > bestScore) {
            bestScore = buf[i].score;
            best = buf[i].pid;
        }
    }
    return best;
}

bool proc_alive(pid_t pid) { return pid > 0 && kill(pid, 0) == 0; }

bool proc_probe_read(pid_t pid) {
    std::vector<MapsReg> maps;
    if (!mem_maps(pid, maps)) return false;
    int tried = 0;
    for (const auto &r : maps) {
        if (r.perms[0] != 'r' || r.end <= r.start) continue;
        uint8_t scratch[8] = {};
        if (mem_read(pid, r.start, scratch, sizeof(scratch))) return true;
        if (++tried >= 64) break;
    }
    return false;
}