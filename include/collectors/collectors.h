#pragma once

#include "core/system_state.h"
#include <cstddef>

void collect_cpu_and_mem(SystemState* state, double dt_sec);
void collect_io_and_net(SystemState* state, double dt_sec);
void collect_thermals(SystemState* state);
void collect_processes(SystemState* state, double dt_sec);
int send_process_signal(pid_t pid, int sig, char* status_buf, size_t buf_len);
