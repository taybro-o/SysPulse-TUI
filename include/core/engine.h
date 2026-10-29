#pragma once

#include "core/system_state.h"

namespace ftxui {
    class ScreenInteractive;
}

void init_system_state(SystemState* state);
void destroy_system_state(SystemState* state);
void start_engine(SystemState* state, ftxui::ScreenInteractive* screen);
void stop_engine(SystemState* state);
void collect_all_metrics(SystemState* state, double dt_sec);
