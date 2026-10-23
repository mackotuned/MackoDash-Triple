#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * First-order exponential follower for needles and arcs. Data arrives at
 * 20-50 Hz while the display refreshes at about 60 Hz, so the needle glides
 * toward each new sample instead of jumping. The time constant is the lag
 * a step change takes to cover about 63 percent of the distance.
 */
typedef struct {
    float value;
    uint32_t last_ms;
    bool initialized;
} gauge_filter_t;

static inline void gauge_filter_reset(gauge_filter_t *filter, float value,
                                      uint32_t now_ms)
{
    filter->value = value;
    filter->last_ms = now_ms;
    filter->initialized = true;
}

static inline float gauge_filter_update(gauge_filter_t *filter, float target,
                                        uint32_t now_ms,
                                        float time_constant_ms)
{
    if (!filter->initialized) {
        gauge_filter_reset(filter, target, now_ms);
        return target;
    }
    const uint32_t elapsed_ms = now_ms - filter->last_ms;
    filter->last_ms = now_ms;
    if (elapsed_ms >= 500U || time_constant_ms <= 0.0f) {
        filter->value = target;
        return target;
    }
    const float alpha = 1.0f - expf(-(float)elapsed_ms / time_constant_ms);
    filter->value += (target - filter->value) * alpha;
    return filter->value;
}
