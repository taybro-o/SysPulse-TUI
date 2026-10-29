#include "collectors/collectors.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

static bool is_physical_disk(const char* name) {
    if (!name || name[0] == '\0') return false;

    // Check sd[a-z]+
    if (strncmp(name, "sd", 2) == 0 && isalpha(static_cast<unsigned char>(name[2]))) {
        for (size_t i = 2; name[i]; ++i) {
            if (!isalpha(static_cast<unsigned char>(name[i]))) return false;
        }
        return true;
    }

    // Check vd[a-z]+
    if (strncmp(name, "vd", 2) == 0 && isalpha(static_cast<unsigned char>(name[2]))) {
        for (size_t i = 2; name[i]; ++i) {
            if (!isalpha(static_cast<unsigned char>(name[i]))) return false;
        }
        return true;
    }

    // Check xvd[a-z]+
    if (strncmp(name, "xvd", 3) == 0 && isalpha(static_cast<unsigned char>(name[3]))) {
        for (size_t i = 3; name[i]; ++i) {
            if (!isalpha(static_cast<unsigned char>(name[i]))) return false;
        }
        return true;
    }

    // Check nvme[0-9]+n[0-9]+ without partition suffix 'p[0-9]+'
    if (strncmp(name, "nvme", 4) == 0) {
        const char* n_pos = strchr(name + 4, 'n');
        if (n_pos && !strchr(n_pos, 'p')) {
            return true;
        }
        return false;
    }

    // Check mmcblk[0-9]+ without partition suffix 'p[0-9]+'
    if (strncmp(name, "mmcblk", 6) == 0 && !strchr(name + 6, 'p')) {
        return true;
    }

    return false;
}

void collect_io_and_net(SystemState* state, double dt_sec) {
    if (dt_sec <= 0.0001) dt_sec = 1.0;

    // 1. Parse /proc/diskstats
    FILE* disk_fp = fopen("/proc/diskstats", "r");
    if (disk_fp) {
        char line[512];
        while (fgets(line, sizeof(line), disk_fp)) {
            int major = 0, minor = 0;
            char dev_name[64] = {0};
            unsigned long long rd_ios = 0, rd_merges = 0, rd_sec = 0, rd_ticks = 0;
            unsigned long long wr_ios = 0, wr_merges = 0, wr_sec = 0, wr_ticks = 0;

            int count = sscanf(line, "%d %d %63s %llu %llu %llu %llu %llu %llu %llu %llu",
                               &major, &minor, dev_name,
                               &rd_ios, &rd_merges, &rd_sec, &rd_ticks,
                               &wr_ios, &wr_merges, &wr_sec, &wr_ticks);
            if (count >= 11 && is_physical_disk(dev_name)) {
                // Find or insert into state->disks
                DiskMetrics* target = nullptr;
                for (auto& d : state->disks) {
                    if (strncmp(d.device, dev_name, sizeof(d.device)) == 0) {
                        target = &d;
                        break;
                    }
                }
                if (!target) {
                    DiskMetrics new_disk;
                    memset(&new_disk, 0, sizeof(new_disk));
                    snprintf(new_disk.device, sizeof(new_disk.device), "%.31s", dev_name);
                    state->disks.push_back(new_disk);
                    target = &state->disks.back();
                }

                if (target->prev_read_sectors > 0 || target->prev_write_sectors > 0) {
                    unsigned long long d_rd = (rd_sec >= target->prev_read_sectors) ? (rd_sec - target->prev_read_sectors) : 0;
                    unsigned long long d_wr = (wr_sec >= target->prev_write_sectors) ? (wr_sec - target->prev_write_sectors) : 0;
                    target->read_mbs = (static_cast<double>(d_rd) * 512.0) / (dt_sec * 1024.0 * 1024.0);
                    target->write_mbs = (static_cast<double>(d_wr) * 512.0) / (dt_sec * 1024.0 * 1024.0);
                }
                target->prev_read_sectors = rd_sec;
                target->prev_write_sectors = wr_sec;
            }
        }
        fclose(disk_fp);
    }

    // 2. Parse /proc/net/dev
    FILE* net_fp = fopen("/proc/net/dev", "r");
    if (net_fp) {
        char line[512];
        // Skip first two header lines
        if (fgets(line, sizeof(line), net_fp) && fgets(line, sizeof(line), net_fp)) {
            while (fgets(line, sizeof(line), net_fp)) {
                char* colon = strchr(line, ':');
                if (!colon) continue;

                *colon = '\0';
                // Trim leading whitespace on interface name
                char* iface_start = line;
                while (*iface_start && isspace(static_cast<unsigned char>(*iface_start))) {
                    iface_start++;
                }
                // Trim trailing whitespace
                char* iface_end = colon - 1;
                while (iface_end > iface_start && isspace(static_cast<unsigned char>(*iface_end))) {
                    *iface_end = '\0';
                    iface_end--;
                }

                if (strcmp(iface_start, "lo") == 0 || iface_start[0] == '\0') {
                    continue;
                }

                unsigned long long rx_bytes = 0, rx_packets = 0, rx_errs = 0, rx_drop = 0;
                unsigned long long rx_fifo = 0, rx_frame = 0, rx_compressed = 0, rx_multicast = 0;
                unsigned long long tx_bytes = 0, tx_packets = 0, tx_errs = 0, tx_drop = 0;
                unsigned long long tx_fifo = 0, tx_colls = 0, tx_carrier = 0, tx_compressed = 0;

                int count = sscanf(colon + 1,
                                   "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                                   &rx_bytes, &rx_packets, &rx_errs, &rx_drop,
                                   &rx_fifo, &rx_frame, &rx_compressed, &rx_multicast,
                                   &tx_bytes, &tx_packets, &tx_errs, &tx_drop,
                                   &tx_fifo, &tx_colls, &tx_carrier, &tx_compressed);
                if (count >= 9) {
                    NetMetrics* target = nullptr;
                    for (auto& n : state->nets) {
                        if (strncmp(n.iface, iface_start, sizeof(n.iface)) == 0) {
                            target = &n;
                            break;
                        }
                    }
                    if (!target) {
                        NetMetrics new_net;
                        memset(&new_net, 0, sizeof(new_net));
                        snprintf(new_net.iface, sizeof(new_net.iface), "%.31s", iface_start);
                        state->nets.push_back(new_net);
                        target = &state->nets.back();
                    }

                    if (target->prev_rx_bytes > 0 || target->prev_tx_bytes > 0) {
                        unsigned long long d_rx = (rx_bytes >= target->prev_rx_bytes) ? (rx_bytes - target->prev_rx_bytes) : 0;
                        unsigned long long d_tx = (tx_bytes >= target->prev_tx_bytes) ? (tx_bytes - target->prev_tx_bytes) : 0;
                        target->rx_mbps = (static_cast<double>(d_rx) * 8.0) / (dt_sec * 1000000.0);
                        target->tx_mbps = (static_cast<double>(d_tx) * 8.0) / (dt_sec * 1000000.0);
                    }
                    target->prev_rx_bytes = rx_bytes;
                    target->prev_tx_bytes = tx_bytes;
                }
            }
        }
        fclose(net_fp);
    }
}
