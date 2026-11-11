#ifndef STEAM_NEEDLE_GEOMETRY_H
#define STEAM_NEEDLE_GEOMETRY_H

#include <math.h>
#include <stdint.h>

enum {
    PRESSURE_NEEDLE_INNER_RADIUS = 108,
    STEAM_NEEDLE_EDGE_GAP = 30,
    BREW_NEEDLE_EDGE_GAP = 60
};

typedef struct {
    float inner_x;
    float inner_y;
    float outer_x;
    float outer_y;
} steam_needle_points_t;

static inline steam_needle_points_t steam_needle_points(float center_x, float center_y,
                                                        float inner_radius, float outer_radius,
                                                        float angle_degrees)
{
    const float angle = angle_degrees * 0.017453292519943295f;
    const float x = sinf(angle);
    const float y = -cosf(angle);
    steam_needle_points_t points = {
        center_x + inner_radius * x, center_y + inner_radius * y,
        center_x + outer_radius * x, center_y + outer_radius * y
    };
    return points;
}

static inline float steam_simulated_angle(uint32_t elapsed_ms)
{
    uint32_t phase = elapsed_ms % 10000u;
    if(phase > 5000u) phase = 10000u - phase;
    return 219.0f - 78.0f * (float)phase / 5000.0f;
}

static inline float brew_pressure_angle(float pressure_bar)
{
    if(pressure_bar < 0.0f) pressure_bar = 0.0f;
    if(pressure_bar > 15.0f) pressure_bar = 15.0f;
    return 254.0f + pressure_bar * (212.0f / 15.0f);
}

static inline float temperature_simulated_angle(uint32_t elapsed_ms)
{
    uint32_t phase = elapsed_ms % 10000u;
    if(phase > 5000u) phase = 10000u - phase;
    return 230.0f + 260.0f * (float)phase / 5000.0f;
}

#endif
