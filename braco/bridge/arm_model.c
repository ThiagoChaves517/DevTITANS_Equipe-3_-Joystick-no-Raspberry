#include "arm_model.h"

#include <math.h>

const char *const arm_joint_name[JOINT_COUNT] =
{
    "base",
    "ombro",
    "cotovelo",
    "garra"
};

void arm_default_config(struct joint_config cfg[JOINT_COUNT])
{
    /*
     * Mesmos números do ArmConfig.h do firmware. São um ponto de partida
     * conservador: calibrar com o braço montado (ver braco/README.md).
     */
    cfg[JOINT_BASE]     = (struct joint_config){ 10.0f, 170.0f, 90.0f, 60.0f };
    cfg[JOINT_OMBRO]    = (struct joint_config){ 45.0f, 135.0f, 90.0f, 45.0f };
    cfg[JOINT_COTOVELO] = (struct joint_config){ 45.0f, 135.0f, 90.0f, 45.0f };
    cfg[JOINT_GARRA]    = (struct joint_config){ 30.0f,  90.0f, 60.0f, 90.0f };
}

static float clampf(float value, float lo, float hi)
{
    if (value < lo)
        return lo;

    if (value > hi)
        return hi;

    return value;
}

void arm_init(struct arm_model *arm, const struct joint_config cfg[JOINT_COUNT])
{
    for (int j = 0; j < JOINT_COUNT; ++j)
    {
        arm->joint[j] = cfg[j];
        arm->target[j] = cfg[j].home;
    }

    arm->homing = false;
    arm->home_speed = 30.0f;
}

void arm_set_limits(struct arm_model *arm, int joint, float min, float max, float home)
{
    if (joint < 0 || joint >= JOINT_COUNT || min >= max)
        return;

    struct joint_config *cfg = &arm->joint[joint];

    cfg->min = min;
    cfg->max = max;
    cfg->home = clampf(home, min, max);

    arm->target[joint] = clampf(arm->target[joint], min, max);
}

void arm_start_homing(struct arm_model *arm)
{
    arm->homing = true;
}

void arm_step(struct arm_model *arm, const float dir[JOINT_COUNT], float dt)
{
    bool manual = false;

    for (int j = 0; j < JOINT_COUNT; ++j)
    {
        if (dir[j] != 0.0f)
            manual = true;
    }

    if (manual)
        arm->homing = false;

    bool all_home = true;

    for (int j = 0; j < JOINT_COUNT; ++j)
    {
        const struct joint_config *cfg = &arm->joint[j];
        float target = arm->target[j];

        if (arm->homing)
        {
            const float step = arm->home_speed * dt;
            const float error = cfg->home - target;

            if (fabsf(error) <= step)
                target = cfg->home;
            else
                target += (error > 0.0f) ? step : -step;

            if (target != cfg->home)
                all_home = false;
        }
        else
        {
            target += clampf(dir[j], -1.0f, 1.0f) * cfg->speed * dt;
        }

        arm->target[j] = clampf(target, cfg->min, cfg->max);
    }

    if (arm->homing && all_home)
        arm->homing = false;
}
