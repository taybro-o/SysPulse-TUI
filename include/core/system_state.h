#pragma once

#include <pthread.h>
#include <sys/types.h>
#include <deque>
#include <vector>

enum SortMode {
    SORT_CPU = 0,
    SORT_MEM = 1,
    SORT_PID = 2,
    SORT_NAME = 3,
    SORT_COUNT = 4
};

struct CoreUsage {
    int core_id;
    double usage_pct;
    unsigned long long prev_Active;
    unsigned long long prev_Total;
    std::deque<int> history;
};

struct CpuMetrics {
    double total_usage_pct;
    unsigned long long prev_total_active;
    unsigned long long prev_total_all;
    double load_avg[3];
    long uptime_sec;
    std::deque<int> history;
    std::vector<CoreUsage> cores;
};

struct MemMetrics {
    unsigned long long total_kb;
    unsigned long long avail_kb;
    unsigned long long used_kb;
    unsigned long long cached_kb;
    unsigned long long swap_total_kb;
    unsigned long long swap_used_kb;
    double ram_pct;
    double swap_pct;
};

struct DiskMetrics {
    char device[32];
    double read_mbs;
    double write_mbs;
    unsigned long long prev_read_sectors;
    unsigned long long prev_write_sectors;
};

struct NetMetrics {
    char iface[32];
    double rx_mbps;
    double tx_mbps;
    unsigned long long prev_rx_bytes;
    unsigned long long prev_tx_bytes;
};

struct ThermalZone {
    char label[64];
    double temp_celsius;
};

struct ProcessInfo {
    pid_t pid;
    char name[64];
    char state;
    double cpu_pct;
    long rss_kb;
    unsigned long long prev_proc_ticks;
};

struct SystemState {
    CpuMetrics cpu;
    MemMetrics mem;
    std::vector<DiskMetrics> disks;
    std::vector<NetMetrics> nets;
    std::vector<ThermalZone> thermals;
    std::vector<ProcessInfo> procs;

    char status_msg[128];
    int sort_mode;
    int selected_index;
    volatile int running;
    pthread_mutex_t lock;
};
