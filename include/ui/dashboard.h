#pragma once

#include "core/system_state.h"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

ftxui::Component create_dashboard(SystemState* state, ftxui::ScreenInteractive* screen);
