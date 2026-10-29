#include "ui/dashboard.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

using namespace ftxui;

static Color get_usage_color(double pct) {
    if (pct < 50.0) return Color::RGB(34, 197, 94);   // Green
    if (pct < 80.0) return Color::RGB(234, 179, 8);   // Yellow
    return Color::RGB(239, 68, 68);                    // Red
}

static Color get_temp_color(double t) {
    if (t < 55.0) return Color::RGB(34, 197, 94);     // Green
    if (t < 75.0) return Color::RGB(234, 179, 8);     // Yellow
    return Color::RGB(239, 68, 68);                    // Red
}

Component create_dashboard(SystemState* state, ScreenInteractive* screen) {
    (void)screen;
    return Renderer([state]() {
        // 1. Thread-safe snapshot copy
        SystemState snap;
        pthread_mutex_lock(&state->lock);
        snap = *state;
        pthread_mutex_unlock(&state->lock);

        // --- Header Bar ---
        long up = snap.cpu.uptime_sec;
        long up_h = up / 3600;
        long up_m = (up % 3600) / 60;
        long up_s = up % 60;
        char uptime_str[32];
        snprintf(uptime_str, sizeof(uptime_str), "%02ld:%02ld:%02ld", up_h, up_m, up_s);

        char load_str[64];
        snprintf(load_str, sizeof(load_str), "Load: %.2f %.2f %.2f",
                 snap.cpu.load_avg[0], snap.cpu.load_avg[1], snap.cpu.load_avg[2]);

        auto header = hbox({
            text(" SysPulse-TUI ") | bold | color(Color::RGB(56, 189, 248)),
            separator(),
            text(" Up: " + std::string(uptime_str) + " ") | color(Color::RGB(148, 163, 184)),
            separator(),
            text(" " + std::string(load_str) + " ") | color(Color::RGB(203, 213, 225)),
            separator(),
            text(" Status: ") | dim,
            text(std::string(snap.status_msg) + " ") | color(Color::RGB(74, 222, 128)) | flex,
        }) | bgcolor(Color::RGB(15, 23, 42)) | borderStyled(Color::RGB(71, 85, 105));

        // --- Top Panel: CPU & Thermals ---
        // Left: CPU Historical braille graph
        auto cpu_graph_func = [history = snap.cpu.history](int width, int height) -> std::vector<int> {
            std::vector<int> out(width, 0);
            if (history.empty() || width <= 0 || height <= 0) return out;
            int h_size = static_cast<int>(history.size());
            for (int i = 0; i < width; ++i) {
                int idx = h_size - 1 - (width - 1 - i);
                if (idx >= 0 && idx < h_size) {
                    out[i] = (history[idx] * height) / 100;
                } else {
                    out[i] = 0;
                }
            }
            return out;
        };

        char cpu_title[64];
        snprintf(cpu_title, sizeof(cpu_title), " CPU Total: %5.1f%% ", snap.cpu.total_usage_pct);

        auto cpu_left = vbox({
            hbox({
                text(cpu_title) | bold | color(get_usage_color(snap.cpu.total_usage_pct)),
                gauge(static_cast<float>(snap.cpu.total_usage_pct / 100.0)) | color(get_usage_color(snap.cpu.total_usage_pct)) | flex,
            }),
            separator(),
            graph(cpu_graph_func) | color(Color::RGB(56, 189, 248)) | flex,
        }) | flex;

        // Right: Cores + Thermals
        Elements core_elements;
        for (const auto& c : snap.cpu.cores) {
            char cname[16];
            snprintf(cname, sizeof(cname), "C%-2d ", c.core_id);
            char cpct[16];
            snprintf(cpct, sizeof(cpct), " %3d%%", static_cast<int>(c.usage_pct));
            core_elements.push_back(
                hbox({
                    text(cname) | dim,
                    gauge(static_cast<float>(c.usage_pct / 100.0)) | color(get_usage_color(c.usage_pct)) | flex,
                    text(cpct) | bold | color(get_usage_color(c.usage_pct)),
                })
            );
        }

        Elements thermal_elements;
        if (snap.thermals.empty()) {
            thermal_elements.push_back(text("No thermal zones detected") | dim);
        } else {
            for (const auto& t : snap.thermals) {
                char tbuf[32];
                snprintf(tbuf, sizeof(tbuf), "%5.1f°C", t.temp_celsius);
                char lbuf[40];
                snprintf(lbuf, sizeof(lbuf), "%-14.14s", t.label);
                thermal_elements.push_back(
                    hbox({
                        text(lbuf) | dim,
                        text(" " + std::string(tbuf)) | bold | color(get_temp_color(t.temp_celsius)),
                    })
                );
            }
        }

        auto cpu_right = vbox({
            text(" Cores Usage ") | bold | color(Color::RGB(203, 213, 225)),
            vbox(std::move(core_elements)) | flex,
            separator(),
            text(" Thermals ") | bold | color(Color::RGB(203, 213, 225)),
            vbox(std::move(thermal_elements)),
        }) | size(WIDTH, GREATER_THAN, 34);

        auto top_panel = window(
            text(" CPU & HARDWARE THERMALS ") | bold | color(Color::RGB(56, 189, 248)),
            hbox({
                cpu_left,
                separator(),
                cpu_right,
            })
        ) | size(HEIGHT, EQUAL, 12);

        // --- Middle Left Panel: Memory & Disk I/O ---
        double ram_used_gib = static_cast<double>(snap.mem.used_kb) / 1048576.0;
        double ram_tot_gib = static_cast<double>(snap.mem.total_kb) / 1048576.0;
        double cached_gib = static_cast<double>(snap.mem.cached_kb) / 1048576.0;
        double swap_used_gib = static_cast<double>(snap.mem.swap_used_kb) / 1048576.0;
        double swap_tot_gib = static_cast<double>(snap.mem.swap_total_kb) / 1048576.0;

        char ram_str[64];
        snprintf(ram_str, sizeof(ram_str), "RAM:  %5.2f / %5.2f GiB (%4.1f%%)",
                 ram_used_gib, ram_tot_gib, snap.mem.ram_pct);

        char cache_str[64];
        snprintf(cache_str, sizeof(cache_str), "Cache: %5.2f GiB", cached_gib);

        char swap_str[64];
        snprintf(swap_str, sizeof(swap_str), "Swap: %5.2f / %5.2f GiB (%4.1f%%)",
                 swap_used_gib, swap_tot_gib, snap.mem.swap_pct);

        Elements disk_elements;
        if (snap.disks.empty()) {
            disk_elements.push_back(text("No physical storage drives") | dim);
        } else {
            for (const auto& d : snap.disks) {
                char dbuf[64];
                snprintf(dbuf, sizeof(dbuf), " %-8s  ▼ %6.2f MB/s   ▲ %6.2f MB/s",
                         d.device, d.read_mbs, d.write_mbs);
                disk_elements.push_back(
                    hbox({
                        text(dbuf) | color(Color::RGB(226, 232, 240)),
                    })
                );
            }
        }

        auto mem_disk_panel = window(
            text(" MEMORY & STORAGE I/O ") | bold | color(Color::RGB(168, 85, 247)),
            vbox({
                hbox({
                    text(ram_str) | color(Color::RGB(216, 180, 254)),
                    separator(),
                    text(cache_str) | color(Color::RGB(147, 197, 253)),
                }),
                gauge(static_cast<float>(snap.mem.ram_pct / 100.0)) | color(Color::RGB(168, 85, 247)),
                text(swap_str) | color(Color::RGB(253, 224, 71)),
                gauge(static_cast<float>(snap.mem.swap_pct / 100.0)) | color(Color::RGB(234, 179, 8)),
                separator(),
                text(" Physical Drives I/O ") | bold | color(Color::RGB(203, 213, 225)),
                vbox(std::move(disk_elements)),
            })
        ) | flex;

        // --- Middle Right Panel: Network Throughput ---
        Elements net_elements;
        if (snap.nets.empty()) {
            net_elements.push_back(text("No network interfaces detected") | dim);
        } else {
            for (const auto& n : snap.nets) {
                char net_label[32];
                snprintf(net_label, sizeof(net_label), " Interface: %-10s", n.iface);
                char rx_str[48];
                snprintf(rx_str, sizeof(rx_str), " ▼ Down: %7.2f Mbps", n.rx_mbps);
                char tx_str[48];
                snprintf(tx_str, sizeof(tx_str), " ▲ Up:   %7.2f Mbps", n.tx_mbps);

                net_elements.push_back(vbox({
                    text(net_label) | bold | color(Color::RGB(56, 189, 248)),
                    hbox({
                        text(rx_str) | color(Color::RGB(34, 197, 94)),
                        gauge(static_cast<float>(std::min(1.0, n.rx_mbps / 100.0))) | color(Color::RGB(34, 197, 94)) | flex,
                    }),
                    hbox({
                        text(tx_str) | color(Color::RGB(236, 72, 153)),
                        gauge(static_cast<float>(std::min(1.0, n.tx_mbps / 100.0))) | color(Color::RGB(236, 72, 153)) | flex,
                    }),
                }));
            }
        }

        auto net_panel = window(
            text(" NETWORK THROUGHPUT ") | bold | color(Color::RGB(6, 182, 212)),
            vbox(std::move(net_elements))
        ) | flex;

        auto middle_row = hbox({
            mem_disk_panel,
            net_panel,
        }) | size(HEIGHT, EQUAL, 8);

        // --- Bottom Panel: Interactive Process Manager ---
        const char* sort_labels[4] = {"CPU%", "MEM", "PID", "NAME"};
        std::string sort_indicator = std::string(" Sort: ") + sort_labels[snap.sort_mode % 4] + " ";

        auto proc_header = hbox({
            text(" PID   ") | bold | (snap.sort_mode == SORT_PID ? (color(Color::RGB(250, 204, 21)) | bold) : color(Color::RGB(148, 163, 184))),
            text(" COMMAND / NAME             ") | bold | (snap.sort_mode == SORT_NAME ? (color(Color::RGB(250, 204, 21)) | bold) : color(Color::RGB(148, 163, 184))) | flex,
            text(" STAT ") | bold | color(Color::RGB(148, 163, 184)),
            text(" CPU%    ") | bold | (snap.sort_mode == SORT_CPU ? (color(Color::RGB(250, 204, 21)) | bold) : color(Color::RGB(148, 163, 184))),
            text(" MEM (MiB) ") | bold | (snap.sort_mode == SORT_MEM ? (color(Color::RGB(250, 204, 21)) | bold) : color(Color::RGB(148, 163, 184))),
        }) | bgcolor(Color::RGB(30, 41, 59));

        // Scroll window calculations
        int total_procs = static_cast<int>(snap.procs.size());
        int sel = snap.selected_index;
        if (sel < 0) sel = 0;
        if (sel >= total_procs && total_procs > 0) sel = total_procs - 1;

        int page_size = 12;
        int start_idx = 0;
        if (sel >= page_size) {
            start_idx = sel - page_size + 1;
        }
        int end_idx = std::min(total_procs, start_idx + page_size);

        Elements proc_rows;
        for (int i = start_idx; i < end_idx; ++i) {
            const auto& p = snap.procs[i];
            char pid_buf[16];
            snprintf(pid_buf, sizeof(pid_buf), " %-6d", p.pid);

            char name_buf[48];
            snprintf(name_buf, sizeof(name_buf), " %-27.27s", p.name);

            char state_buf[16];
            snprintf(state_buf, sizeof(state_buf), "   %c  ", p.state);

            char cpu_buf[16];
            snprintf(cpu_buf, sizeof(cpu_buf), " %6.1f%%", p.cpu_pct);

            char mem_buf[16];
            snprintf(mem_buf, sizeof(mem_buf), " %8.1f ", static_cast<double>(p.rss_kb) / 1024.0);

            bool is_selected = (i == sel);
            auto row = hbox({
                text(pid_buf),
                text(name_buf) | flex,
                text(state_buf),
                text(cpu_buf) | color(get_usage_color(p.cpu_pct)),
                text(mem_buf) | color(Color::RGB(168, 85, 247)),
            });

            if (is_selected) {
                row = row | bgcolor(Color::RGB(37, 99, 235)) | color(Color::White) | bold;
            } else if (i % 2 == 1) {
                row = row | bgcolor(Color::RGB(15, 23, 42));
            } else {
                row = row | bgcolor(Color::RGB(24, 32, 50));
            }

            proc_rows.push_back(row);
        }

        if (proc_rows.empty()) {
            proc_rows.push_back(text(" No processes running ") | dim);
        }

        char proc_title[64];
        snprintf(proc_title, sizeof(proc_title), " PROCESS MONITOR (%d tasks) - %s",
                 total_procs, sort_indicator.c_str());

        auto proc_panel = window(
            text(proc_title) | bold | color(Color::RGB(250, 204, 21)),
            vbox({
                proc_header,
                vbox(std::move(proc_rows)) | flex,
            })
        ) | flex;

        // --- Bottom Keybind Legend Bar ---
        auto footer = hbox({
            text(" [▲/▼, j/k] Select ") | color(Color::RGB(226, 232, 240)),
            separator(),
            text(" [PgUp/PgDn] Jump 10 ") | color(Color::RGB(226, 232, 240)),
            separator(),
            text(" [s] Sort Mode ") | color(Color::RGB(250, 204, 21)) | bold,
            separator(),
            text(" [t] SIGTERM (15) ") | color(Color::RGB(234, 179, 8)) | bold,
            separator(),
            text(" [x/F9] SIGKILL (9) ") | color(Color::RGB(239, 68, 68)) | bold,
            separator(),
            text(" [q/Esc] Quit ") | color(Color::RGB(148, 163, 184)),
        }) | bgcolor(Color::RGB(15, 23, 42)) | borderStyled(Color::RGB(71, 85, 105));

        return vbox({
            header,
            top_panel,
            middle_row,
            proc_panel,
            footer,
        }) | bgcolor(Color::RGB(10, 15, 26));
    });
}
