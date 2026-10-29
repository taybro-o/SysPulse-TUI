#include "core/engine.h"
#include "collectors/collectors.h"
#include <ftxui/component/screen_interactive.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <time.h>

struct EngineContext {
    SystemState* state = nullptr;
    ftxui::ScreenInteractive* screen = nullptr;
    pthread_t thread = 0;
    bool thread_active = false;
};

static EngineContext s_engine;

void collect_all_metrics(SystemState* state, double dt_sec) {
    collect_cpu_and_mem(state, dt_sec);
    collect_io_and_net(state, dt_sec);
    collect_thermals(state);
    collect_processes(state, dt_sec);
}

static void* worker_thread_func(void* arg) {
    EngineContext* ctx = static_cast<EngineContext*>(arg);
    struct timespec prev_time;
    clock_gettime(CLOCK_MONOTONIC, &prev_time);

    // Initial baseline collection
    pthread_mutex_lock(&ctx->state->lock);
    collect_all_metrics(ctx->state, 1.0);
    pthread_mutex_unlock(&ctx->state->lock);

    if (ctx->screen) {
        ctx->screen->PostEvent(ftxui::Event::Custom);
    }

    while (ctx->state->running) {
        // Sleep in 50ms intervals up to 1000ms for swift exit
        for (int i = 0; i < 20 && ctx->state->running; ++i) {
            usleep(50000);
        }
        if (!ctx->state->running) break;

        struct timespec curr_time;
        clock_gettime(CLOCK_MONOTONIC, &curr_time);
        double dt = static_cast<double>(curr_time.tv_sec - prev_time.tv_sec) +
                    static_cast<double>(curr_time.tv_nsec - prev_time.tv_nsec) / 1e9;
        if (dt <= 0.0) dt = 1.0;
        prev_time = curr_time;

        pthread_mutex_lock(&ctx->state->lock);
        collect_all_metrics(ctx->state, dt);
        pthread_mutex_unlock(&ctx->state->lock);

        if (ctx->screen) {
            ctx->screen->PostEvent(ftxui::Event::Custom);
        }
    }
    return nullptr;
}

void init_system_state(SystemState* state) {
    pthread_mutex_init(&state->lock, nullptr);
    state->running = 1;
    state->sort_mode = SORT_CPU;
    state->selected_index = 0;
    state->cpu.total_usage_pct = 0.0;
    state->cpu.prev_total_active = 0;
    state->cpu.prev_total_all = 0;
    state->cpu.load_avg[0] = state->cpu.load_avg[1] = state->cpu.load_avg[2] = 0.0;
    state->cpu.uptime_sec = 0;
    state->cpu.history.clear();
    state->cpu.cores.clear();
    memset(&state->mem, 0, sizeof(state->mem));
    state->disks.clear();
    state->nets.clear();
    state->thermals.clear();
    state->procs.clear();
    snprintf(state->status_msg, sizeof(state->status_msg), "SysPulse-TUI active | Normal operation");
}

void destroy_system_state(SystemState* state) {
    pthread_mutex_destroy(&state->lock);
}

void start_engine(SystemState* state, ftxui::ScreenInteractive* screen) {
    s_engine.state = state;
    s_engine.screen = screen;
    s_engine.thread_active = true;
    pthread_create(&s_engine.thread, nullptr, worker_thread_func, &s_engine);
}

void stop_engine(SystemState* state) {
    if (state) {
        state->running = 0;
    }
    if (s_engine.thread_active) {
        pthread_join(s_engine.thread, nullptr);
        s_engine.thread_active = false;
    }
}
