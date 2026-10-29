#include "collectors/collectors.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>
#include <unordered_map>
#include <algorithm>

static std::unordered_map<pid_t, unsigned long long> s_prev_proc_ticks;
static unsigned long long s_prev_system_ticks = 0;

int send_process_signal(pid_t pid, int sig, char* status_buf, size_t buf_len) {
    if (pid <= 1) {
        if (status_buf && buf_len > 0) {
            snprintf(status_buf, buf_len, "Cannot send signal %d to PID %d (system critical)", sig, pid);
        }
        return -1;
    }

    if (kill(pid, sig) == 0) {
        if (status_buf && buf_len > 0) {
            const char* sig_name = (sig == SIGKILL) ? "SIGKILL (9)" : (sig == SIGTERM ? "SIGTERM (15)" : "SIGNAL");
            snprintf(status_buf, buf_len, "Sent %s to PID %d", sig_name, pid);
        }
        return 0;
    } else {
        if (status_buf && buf_len > 0) {
            snprintf(status_buf, buf_len, "Failed signal %d to PID %d: %s", sig, pid, strerror(errno));
        }
        return -1;
    }
}

void collect_processes(SystemState* state, double dt_sec) {
    DIR* proc_dir = opendir("/proc");
    if (!proc_dir) return;

    std::vector<ProcessInfo> list;
    list.reserve(256);

    std::unordered_map<pid_t, unsigned long long> curr_proc_ticks;
    curr_proc_ticks.reserve(512);

    long page_size_kb = sysconf(_SC_PAGESIZE) / 1024;
    if (page_size_kb <= 0) page_size_kb = 4;

    long clk_tck = sysconf(_SC_CLK_TCK);
    if (clk_tck <= 0) clk_tck = 100;

    unsigned long long curr_sys_ticks = state->cpu.prev_total_all;
    unsigned long long sys_ticks_delta = (curr_sys_ticks > s_prev_system_ticks) ? (curr_sys_ticks - s_prev_system_ticks) : 0;
    s_prev_system_ticks = curr_sys_ticks;

    size_t num_cores = state->cpu.cores.size();
    if (num_cores == 0) num_cores = 1;

    struct dirent* ent;
    while ((ent = readdir(proc_dir)) != nullptr) {
        if (!isdigit(static_cast<unsigned char>(ent->d_name[0]))) {
            continue;
        }

        pid_t pid = static_cast<pid_t>(atoi(ent->d_name));
        if (pid <= 0) continue;

        char stat_path[64];
        snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);

        FILE* fp = fopen(stat_path, "r");
        if (!fp) continue;

        char buf[1024];
        if (!fgets(buf, sizeof(buf), fp)) {
            fclose(fp);
            continue;
        }
        fclose(fp);

        char* open_paren = strchr(buf, '(');
        char* close_paren = strrchr(buf, ')');
        if (!open_paren || !close_paren || close_paren <= open_paren) {
            continue;
        }

        ProcessInfo info;
        memset(&info, 0, sizeof(info));
        info.pid = pid;

        size_t name_len = close_paren - (open_paren + 1);
        if (name_len >= sizeof(info.name)) name_len = sizeof(info.name) - 1;
        memcpy(info.name, open_paren + 1, name_len);
        info.name[name_len] = '\0';

        char state_c = '?';
        unsigned long long utime = 0, stime = 0;
        long rss_pages = 0;

        int matched = sscanf(close_paren + 1,
                             " %c"
                             " %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s"
                             " %llu %llu"
                             " %*s %*s %*s %*s %*s %*s %*s %*s"
                             " %ld",
                             &state_c, &utime, &stime, &rss_pages);

        if (matched >= 1) {
            info.state = state_c;
        }
        if (rss_pages > 0) {
            info.rss_kb = rss_pages * page_size_kb;
        }

        unsigned long long total_proc_ticks = utime + stime;
        curr_proc_ticks[pid] = total_proc_ticks;

        auto it = s_prev_proc_ticks.find(pid);
        if (it != s_prev_proc_ticks.end()) {
            info.prev_proc_ticks = it->second;
            unsigned long long proc_ticks_diff = (total_proc_ticks >= it->second) ? (total_proc_ticks - it->second) : 0;
            if (sys_ticks_delta > 0) {
                info.cpu_pct = (static_cast<double>(proc_ticks_diff) / static_cast<double>(sys_ticks_delta)) * 100.0 * static_cast<double>(num_cores);
            } else if (dt_sec > 0.0) {
                info.cpu_pct = (static_cast<double>(proc_ticks_diff) / (dt_sec * static_cast<double>(clk_tck))) * 100.0;
            }
            if (info.cpu_pct > 100.0 * num_cores) {
                info.cpu_pct = 100.0 * num_cores;
            }
            if (info.cpu_pct < 0.0) {
                info.cpu_pct = 0.0;
            }
        } else {
            info.prev_proc_ticks = total_proc_ticks;
            info.cpu_pct = 0.0;
        }

        list.push_back(info);
    }
    closedir(proc_dir);

    s_prev_proc_ticks = std::move(curr_proc_ticks);

    // Sort according to state->sort_mode
    int mode = state->sort_mode;
    std::sort(list.begin(), list.end(), [mode](const ProcessInfo& a, const ProcessInfo& b) {
        switch (mode) {
            case SORT_CPU:
                if (a.cpu_pct != b.cpu_pct) return a.cpu_pct > b.cpu_pct;
                return a.rss_kb > b.rss_kb;
            case SORT_MEM:
                if (a.rss_kb != b.rss_kb) return a.rss_kb > b.rss_kb;
                return a.cpu_pct > b.cpu_pct;
            case SORT_PID:
                return a.pid < b.pid;
            case SORT_NAME: {
                int cmp = strcasecmp(a.name, b.name);
                if (cmp != 0) return cmp < 0;
                return a.pid < b.pid;
            }
            default:
                return a.cpu_pct > b.cpu_pct;
        }
    });

    state->procs = std::move(list);
}
