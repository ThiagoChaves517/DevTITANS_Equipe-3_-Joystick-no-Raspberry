#pragma once

/*
 * Modelo do braço do lado da Raspberry: ângulos-alvo das 4 juntas,
 * limites de curso e integração da velocidade (modo jog).
 *
 * Os limites "oficiais" pertencem ao firmware do braço. Quando o ESP32
 * responde ao comando I, a bridge adota os limites dele (arm_set_limits)
 * para o alvo nunca passar do que o servo realmente consegue fazer.
 */

#include <stdbool.h>

enum arm_joint
{
    JOINT_BASE = 0,
    JOINT_OMBRO,
    JOINT_COTOVELO,
    JOINT_GARRA,
    JOINT_COUNT
};

struct joint_config
{
    float min;   /* graus */
    float max;   /* graus */
    float home;  /* graus */
    float speed; /* graus/s com o botão pressionado */
};

struct arm_model
{
    struct joint_config joint[JOINT_COUNT];
    float target[JOINT_COUNT];
    bool  homing;
    float home_speed; /* graus/s ao voltar para home */
};

extern const char *const arm_joint_name[JOINT_COUNT];

/* Valores padrão para o braço MeArm de acrílico com 4x SG90 (ver ArmConfig.h). */
void arm_default_config(struct joint_config cfg[JOINT_COUNT]);

void arm_init(struct arm_model *arm, const struct joint_config cfg[JOINT_COUNT]);

/* Troca min/max/home (ex.: valores informados pelo firmware) e re-satura o alvo. */
void arm_set_limits(struct arm_model *arm, int joint, float min, float max, float home);

void arm_start_homing(struct arm_model *arm);

/*
 * Avança dt segundos. dir[j] em [-1, 1]: sentido do movimento de cada junta.
 * Qualquer movimento manual cancela o retorno para home.
 */
void arm_step(struct arm_model *arm, const float dir[JOINT_COUNT], float dt);
