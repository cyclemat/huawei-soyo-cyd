#pragma once
#include <algorithm>
#include <cstdint>

enum class Mode { Idle, Zero, Manual };

struct Dispatch {
    int soyo = 0;
    int huawei = 0;
    uint32_t neutralSince = 0;

    void stop(uint32_t now) {
        if (soyo || huawei) neutralSince = now;
        soyo = huawei = 0;
    }

    void update(uint32_t now, Mode mode, bool ready, int grid, int manual,
                int maxSoyo, int maxHuawei, int reserve) {
        if (!ready || mode == Mode::Idle || (mode == Mode::Manual && !manual)) {
            stop(now);
            return;
        }
        const int current = soyo - huawei;
        int requested = -manual;
        if (mode == Mode::Zero) {
            const int error = grid - reserve;
            requested = current;
            if (error > 20 || error < -20) {
                requested += std::max(-150, std::min(150, error / 3));
            }
        }
        requested = std::max(-maxHuawei, std::min(maxSoyo, requested));
        if ((current > 0 && requested <= 0) || (current < 0 && requested >= 0)) {
            stop(now);
            return;
        }
        if (!current && now - neutralSince < 5000) return;
        soyo = std::max(0, requested);
        huawei = std::max(0, -requested);
    }
};
