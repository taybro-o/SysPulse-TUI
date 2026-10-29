#include "collectors/collectors.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <unistd.h>
#include <vector>

void collect_thermals(SystemState* state) {
    std::vector<ThermalZone> current_zones;

    // 1. Traverse /sys/class/hwmon
    DIR* hwmon_dir = opendir("/sys/class/hwmon");
    if (hwmon_dir) {
        struct dirent* hw_ent;
        while ((hw_ent = readdir(hwmon_dir)) != nullptr) {
            if (hw_ent->d_name[0] == '.') continue;

            char hw_path[1024];
            snprintf(hw_path, sizeof(hw_path), "/sys/class/hwmon/%.255s", hw_ent->d_name);

            // Read hwmon driver name
            char hw_name[64] = {0};
            char name_path[1024];
            snprintf(name_path, sizeof(name_path), "/sys/class/hwmon/%.255s/name", hw_ent->d_name);
            FILE* name_fp = fopen(name_path, "r");
            if (name_fp) {
                if (fgets(hw_name, sizeof(hw_name), name_fp)) {
                    size_t len = strlen(hw_name);
                    if (len > 0 && hw_name[len - 1] == '\n') hw_name[len - 1] = '\0';
                }
                fclose(name_fp);
            }

            DIR* sub_dir = opendir(hw_path);
            if (!sub_dir) continue;

            struct dirent* sub_ent;
            while ((sub_ent = readdir(sub_dir)) != nullptr) {
                if (strncmp(sub_ent->d_name, "temp", 4) == 0 &&
                    strstr(sub_ent->d_name, "_input") != nullptr) {
                    char temp_path[1024];
                    snprintf(temp_path, sizeof(temp_path), "/sys/class/hwmon/%.255s/%.255s", hw_ent->d_name, sub_ent->d_name);
                    FILE* tfp = fopen(temp_path, "r");
                    if (tfp) {
                        long long milli_c = 0;
                        if (fscanf(tfp, "%lld", &milli_c) == 1 && milli_c > 0 && milli_c < 200000) {
                            ThermalZone tz;
                            memset(&tz, 0, sizeof(tz));
                            tz.temp_celsius = static_cast<double>(milli_c) / 1000.0;

                            // Check for corresponding _label file
                            char label_file[64];
                            snprintf(label_file, sizeof(label_file), "%.63s", sub_ent->d_name);
                            char* input_pos = strstr(label_file, "_input");
                            if (input_pos) {
                                size_t remain = sizeof(label_file) - static_cast<size_t>(input_pos - label_file);
                                snprintf(input_pos, remain, "_label");
                            }

                            char label_path[1024];
                            snprintf(label_path, sizeof(label_path), "/sys/class/hwmon/%.255s/%.63s", hw_ent->d_name, label_file);

                            char label_str[64] = {0};
                            FILE* lfp = fopen(label_path, "r");
                            if (lfp) {
                                if (fgets(label_str, sizeof(label_str), lfp)) {
                                    size_t len = strlen(label_str);
                                    if (len > 0 && label_str[len - 1] == '\n') label_str[len - 1] = '\0';
                                }
                                fclose(lfp);
                            }

                            if (label_str[0] != '\0') {
                                snprintf(tz.label, sizeof(tz.label), "%.63s", label_str);
                            } else if (hw_name[0] != '\0') {
                                snprintf(tz.label, sizeof(tz.label), "%.30s %.30s", hw_name, sub_ent->d_name);
                            } else {
                                snprintf(tz.label, sizeof(tz.label), "%.63s", sub_ent->d_name);
                            }
                            current_zones.push_back(tz);
                        }
                        fclose(tfp);
                    }
                }
            }
            closedir(sub_dir);
        }
        closedir(hwmon_dir);
    }

    // 2. Traverse /sys/class/thermal if no or few hwmon zones found
    if (current_zones.empty()) {
        DIR* th_dir = opendir("/sys/class/thermal");
        if (th_dir) {
            struct dirent* th_ent;
            while ((th_ent = readdir(th_dir)) != nullptr) {
                if (strncmp(th_ent->d_name, "thermal_zone", 12) == 0) {
                    char temp_path[1024];
                    snprintf(temp_path, sizeof(temp_path), "/sys/class/thermal/%.255s/temp", th_ent->d_name);
                    FILE* tfp = fopen(temp_path, "r");
                    if (tfp) {
                        long long milli_c = 0;
                        if (fscanf(tfp, "%lld", &milli_c) == 1 && milli_c > 0 && milli_c < 200000) {
                            ThermalZone tz;
                            memset(&tz, 0, sizeof(tz));
                            tz.temp_celsius = static_cast<double>(milli_c) / 1000.0;

                            char type_path[1024];
                            snprintf(type_path, sizeof(type_path), "/sys/class/thermal/%.255s/type", th_ent->d_name);
                            char type_str[64] = {0};
                            FILE* type_fp = fopen(type_path, "r");
                            if (type_fp) {
                                if (fgets(type_str, sizeof(type_str), type_fp)) {
                                    size_t len = strlen(type_str);
                                    if (len > 0 && type_str[len - 1] == '\n') type_str[len - 1] = '\0';
                                }
                                fclose(type_fp);
                            }

                            if (type_str[0] != '\0') {
                                snprintf(tz.label, sizeof(tz.label), "%.63s", type_str);
                            } else {
                                snprintf(tz.label, sizeof(tz.label), "%.63s", th_ent->d_name);
                            }
                            current_zones.push_back(tz);
                        }
                        fclose(tfp);
                    }
                }
            }
            closedir(th_dir);
        }
    }

    // Cap thermal zones to at most 8 to maintain clean UI layout
    if (current_zones.size() > 8) {
        current_zones.resize(8);
    }

    state->thermals = std::move(current_zones);
}
