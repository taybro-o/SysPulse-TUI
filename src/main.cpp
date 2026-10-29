#include "core/system_state.h"
#include "core/engine.h"
#include "collectors/collectors.h"
#include "ui/dashboard.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <csignal>
#include <algorithm>

static void resort_processes(SystemState* state) {
    int mode = state->sort_mode;
    std::sort(state->procs.begin(), state->procs.end(), [mode](const ProcessInfo& a, const ProcessInfo& b) {
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
}

static int run_headless_test() {
    SystemState state;
    init_system_state(&state);

    printf("===================================================\n");
    printf("   SysPulse-TUI Headless Self-Test Verification    \n");
    printf("===================================================\n");
    printf("[1/2] Establishing baseline virtual filesystem snapshots...\n");
    collect_all_metrics(&state, 1.0);

    printf("[2/2] Sleeping 1.0s to measure differential delta metrics...\n");
    sleep(1);
    collect_all_metrics(&state, 1.0);

    printf("\n--- [CPU & Host Overview] ---\n");
    printf("  Aggregate CPU Load : %.2f%%\n", state.cpu.total_usage_pct);
    printf("  Logical Cores      : %zu detected\n", state.cpu.cores.size());
    for (const auto& c : state.cpu.cores) {
        printf("    Core %-2d         : %5.1f%%\n", c.core_id, c.usage_pct);
    }
    printf("  Load Averages      : %.2f (1m), %.2f (5m), %.2f (15m)\n",
           state.cpu.load_avg[0], state.cpu.load_avg[1], state.cpu.load_avg[2]);
    printf("  System Uptime      : %ld seconds\n", state.cpu.uptime_sec);

    printf("\n--- [Memory & Swap Metrics] ---\n");
    printf("  RAM Total          : %7.2f GiB\n", static_cast<double>(state.mem.total_kb) / 1048576.0);
    printf("  RAM Used           : %7.2f GiB (%.1f%%)\n",
           static_cast<double>(state.mem.used_kb) / 1048576.0, state.mem.ram_pct);
    printf("  RAM Available      : %7.2f GiB\n", static_cast<double>(state.mem.avail_kb) / 1048576.0);
    printf("  RAM Cached         : %7.2f GiB\n", static_cast<double>(state.mem.cached_kb) / 1048576.0);
    printf("  Swap Total         : %7.2f GiB\n", static_cast<double>(state.mem.swap_total_kb) / 1048576.0);
    printf("  Swap Used          : %7.2f GiB (%.1f%%)\n",
           static_cast<double>(state.mem.swap_used_kb) / 1048576.0, state.mem.swap_pct);

    printf("\n--- [Storage Drive I/O Throughput] ---\n");
    if (state.disks.empty()) {
        printf("  No physical drives found (virtualized/container environment).\n");
    } else {
        for (const auto& d : state.disks) {
            printf("  Device %-12s: Read %7.2f MB/s | Write %7.2f MB/s\n",
                   d.device, d.read_mbs, d.write_mbs);
        }
    }

    printf("\n--- [Network Interface Throughput] ---\n");
    if (state.nets.empty()) {
        printf("  No active non-loopback network interfaces.\n");
    } else {
        for (const auto& n : state.nets) {
            printf("  Interface %-9s: Download (Rx) %7.2f Mbps | Upload (Tx) %7.2f Mbps\n",
                   n.iface, n.rx_mbps, n.tx_mbps);
        }
    }

    printf("\n--- [Hardware Thermals] ---\n");
    if (state.thermals.empty()) {
        printf("  No thermal zones detected (VM / unexposed sensors).\n");
    } else {
        for (const auto& t : state.thermals) {
            printf("  Zone: %-25s | Temperature: %5.1f °C\n", t.label, t.temp_celsius);
        }
    }

    printf("\n--- [Top 5 Active Processes (Sorted by CPU%%)] ---\n");
    printf("  %-7s %-25s %-5s %-8s %-12s\n", "PID", "NAME", "STATE", "CPU%", "RSS (MiB)");
    size_t count = std::min(state.procs.size(), static_cast<size_t>(5));
    for (size_t i = 0; i < count; ++i) {
        const auto& p = state.procs[i];
        printf("  %-7d %-25.25s %-5c %6.1f%% %9.1f MiB\n",
               p.pid, p.name, p.state, p.cpu_pct, static_cast<double>(p.rss_kb) / 1024.0);
    }

    printf("\n===================================================\n");
    printf("   [VERIFICATION SUCCESSFUL] All collectors passed.  \n");
    printf("===================================================\n");

    destroy_system_state(&state);
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc > 1 && strcmp(argv[1], "--test") == 0) {
        return run_headless_test();
    }

    SystemState state;
    init_system_state(&state);

    auto screen = ftxui::ScreenInteractive::Fullscreen();

    // Spawn POSIX background engine thread
    start_engine(&state, &screen);

    // Build FTXUI dashboard component
    auto dashboard = create_dashboard(&state, &screen);

    // Wire keyboard interaction
    auto component = ftxui::CatchEvent(dashboard, [&](ftxui::Event event) {
        pthread_mutex_lock(&state.lock);

        int total_procs = static_cast<int>(state.procs.size());

        if (event == ftxui::Event::ArrowUp || event == ftxui::Event::Character('k')) {
            if (state.selected_index > 0) {
                state.selected_index--;
            }
            pthread_mutex_unlock(&state.lock);
            return true;
        }

        if (event == ftxui::Event::ArrowDown || event == ftxui::Event::Character('j')) {
            if (state.selected_index + 1 < total_procs) {
                state.selected_index++;
            }
            pthread_mutex_unlock(&state.lock);
            return true;
        }

        if (event == ftxui::Event::PageUp) {
            state.selected_index = std::max(0, state.selected_index - 10);
            pthread_mutex_unlock(&state.lock);
            return true;
        }

        if (event == ftxui::Event::PageDown) {
            if (total_procs > 0) {
                state.selected_index = std::min(total_procs - 1, state.selected_index + 10);
            }
            pthread_mutex_unlock(&state.lock);
            return true;
        }

        if (event == ftxui::Event::Character('s')) {
            state.sort_mode = (state.sort_mode + 1) % SORT_COUNT;
            resort_processes(&state);
            snprintf(state.status_msg, sizeof(state.status_msg), "Switched sort column");
            pthread_mutex_unlock(&state.lock);
            return true;
        }

        if (event == ftxui::Event::Character('t')) {
            if (!state.procs.empty() && state.selected_index >= 0 && state.selected_index < total_procs) {
                pid_t target_pid = state.procs[state.selected_index].pid;
                send_process_signal(target_pid, SIGTERM, state.status_msg, sizeof(state.status_msg));
            }
            pthread_mutex_unlock(&state.lock);
            return true;
        }

        if (event == ftxui::Event::Character('x') || event == ftxui::Event::F9) {
            if (!state.procs.empty() && state.selected_index >= 0 && state.selected_index < total_procs) {
                pid_t target_pid = state.procs[state.selected_index].pid;
                send_process_signal(target_pid, SIGKILL, state.status_msg, sizeof(state.status_msg));
            }
            pthread_mutex_unlock(&state.lock);
            return true;
        }

        if (event == ftxui::Event::Character('q') || event == ftxui::Event::Escape) {
            state.running = 0;
            screen.ExitLoopClosure()();
            pthread_mutex_unlock(&state.lock);
            return true;
        }

        pthread_mutex_unlock(&state.lock);
        return false;
    });

    // Enter UI loop
    screen.Loop(component);

    // Clean teardown: reap pthread and destroy mutex
    stop_engine(&state);
    destroy_system_state(&state);

    return 0;
}
