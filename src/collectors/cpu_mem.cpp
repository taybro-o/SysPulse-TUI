#include "collectors/collectors.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

void collect_cpu_and_mem(SystemState* state, double dt_sec) {
    (void)dt_sec;

    // 1. Read /proc/stat
    FILE* stat_fp = fopen("/proc/stat", "r");
    if (stat_fp) {
        char line[512];
        // First line: aggregate cpu
        if (fgets(line, sizeof(line), stat_fp)) {
            char cpu_label[16];
            unsigned long long user = 0, nice = 0, system = 0, idle = 0;
            unsigned long long iowait = 0, irq = 0, softirq = 0, steal = 0;
            unsigned long long guest = 0, guest_nice = 0;

            int count = sscanf(line, "%15s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                               cpu_label, &user, &nice, &system, &idle,
                               &iowait, &irq, &softirq, &steal, &guest, &guest_nice);
            if (count >= 5) {
                unsigned long long active = user + nice + system + irq + softirq + steal;
                unsigned long long total = active + idle + iowait;

                if (state->cpu.prev_total_all > 0 && total >= state->cpu.prev_total_all) {
                    unsigned long long delta_active = (active >= state->cpu.prev_total_active) ? (active - state->cpu.prev_total_active) : 0;
                    unsigned long long delta_total = total - state->cpu.prev_total_all;
                    if (delta_total > 0) {
                        state->cpu.total_usage_pct = ((double)delta_active / (double)delta_total) * 100.0;
                        if (state->cpu.total_usage_pct > 100.0) state->cpu.total_usage_pct = 100.0;
                        if (state->cpu.total_usage_pct < 0.0) state->cpu.total_usage_pct = 0.0;
                    }
                }
                state->cpu.prev_total_active = active;
                state->cpu.prev_total_all = total;

                state->cpu.history.push_back(static_cast<int>(state->cpu.total_usage_pct + 0.5));
                if (state->cpu.history.size() > 100) {
                    state->cpu.history.pop_front();
                }
            }
        }

        // Per-core cpu lines (cpu0, cpu1, ...)
        int core_idx = 0;
        while (fgets(line, sizeof(line), stat_fp)) {
            if (strncmp(line, "cpu", 3) != 0) {
                break;
            }
            char cpu_label[16];
            unsigned long long user = 0, nice = 0, system = 0, idle = 0;
            unsigned long long iowait = 0, irq = 0, softirq = 0, steal = 0;

            int count = sscanf(line, "%15s %llu %llu %llu %llu %llu %llu %llu %llu",
                               cpu_label, &user, &nice, &system, &idle,
                               &iowait, &irq, &softirq, &steal);
            if (count >= 5) {
                int cid = -1;
                if (sscanf(cpu_label, "cpu%d", &cid) == 1) {
                    if (core_idx >= static_cast<int>(state->cpu.cores.size())) {
                        CoreUsage cu;
                        cu.core_id = cid;
                        cu.usage_pct = 0.0;
                        cu.prev_Active = 0;
                        cu.prev_Total = 0;
                        state->cpu.cores.push_back(cu);
                    }
                    CoreUsage& core = state->cpu.cores[core_idx];
                    core.core_id = cid;
                    unsigned long long active = user + nice + system + irq + softirq + steal;
                    unsigned long long total = active + idle + iowait;

                    if (core.prev_Total > 0 && total >= core.prev_Total) {
                        unsigned long long delta_active = (active >= core.prev_Active) ? (active - core.prev_Active) : 0;
                        unsigned long long delta_total = total - core.prev_Total;
                        if (delta_total > 0) {
                            core.usage_pct = ((double)delta_active / (double)delta_total) * 100.0;
                            if (core.usage_pct > 100.0) core.usage_pct = 100.0;
                            if (core.usage_pct < 0.0) core.usage_pct = 0.0;
                        }
                    }
                    core.prev_Active = active;
                    core.prev_Total = total;

                    core.history.push_back(static_cast<int>(core.usage_pct + 0.5));
                    if (core.history.size() > 50) {
                        core.history.pop_front();
                    }
                    core_idx++;
                }
            }
        }
        fclose(stat_fp);
    }

    // 2. Read /proc/uptime
    FILE* up_fp = fopen("/proc/uptime", "r");
    if (up_fp) {
        double up_sec = 0.0;
        double idle_sec = 0.0;
        if (fscanf(up_fp, "%lf %lf", &up_sec, &idle_sec) >= 1) {
            state->cpu.uptime_sec = static_cast<long>(up_sec);
        }
        fclose(up_fp);
    }

    // 3. Read /proc/loadavg
    FILE* load_fp = fopen("/proc/loadavg", "r");
    if (load_fp) {
        if (fscanf(load_fp, "%lf %lf %lf",
                   &state->cpu.load_avg[0],
                   &state->cpu.load_avg[1],
                   &state->cpu.load_avg[2]) != 3) {
            // Keep previous load avg if failed
        }
        fclose(load_fp);
    }

    // 4. Read /proc/meminfo
    FILE* mem_fp = fopen("/proc/meminfo", "r");
    if (mem_fp) {
        char key[64];
        unsigned long long val = 0;
        char unit[16];
        unsigned long long mem_total = 0, mem_free = 0, mem_avail = 0;
        unsigned long long cached = 0, swap_total = 0, swap_free = 0;
        char mline[256];

        while (fgets(mline, sizeof(mline), mem_fp)) {
            if (sscanf(mline, "%63s %llu %15s", key, &val, unit) >= 2) {
                if (strcmp(key, "MemTotal:") == 0) mem_total = val;
                else if (strcmp(key, "MemFree:") == 0) mem_free = val;
                else if (strcmp(key, "MemAvailable:") == 0) mem_avail = val;
                else if (strcmp(key, "Cached:") == 0) cached = val;
                else if (strcmp(key, "SwapTotal:") == 0) swap_total = val;
                else if (strcmp(key, "SwapFree:") == 0) swap_free = val;
            }
        }
        fclose(mem_fp);

        state->mem.total_kb = mem_total;
        state->mem.cached_kb = cached;
        state->mem.avail_kb = (mem_avail > 0) ? mem_avail : (mem_free + cached);
        state->mem.used_kb = (state->mem.total_kb > state->mem.avail_kb) ? (state->mem.total_kb - state->mem.avail_kb) : 0;
        state->mem.swap_total_kb = swap_total;
        state->mem.swap_used_kb = (swap_total > swap_free) ? (swap_total - swap_free) : 0;
        state->mem.ram_pct = (state->mem.total_kb > 0) ? ((double)state->mem.used_kb / (double)state->mem.total_kb) * 100.0 : 0.0;
        state->mem.swap_pct = (state->mem.swap_total_kb > 0) ? ((double)state->mem.swap_used_kb / (double)state->mem.swap_total_kb) * 100.0 : 0.0;
    }
}
