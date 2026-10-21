/*
 * Testes da lógica pura (mapeamento + modelo do braço). Rodam no computador:
 *   ./build.sh test
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../arm_model.h"
#include "../mapping.h"

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static struct pad_state pad_with(uint32_t buttons)
{
    struct pad_state pad = { true, buttons, 0, 0 };
    return pad;
}

static void test_page1_default(void)
{
    struct mapper m;
    struct mapper_output out;
    mapper_init(&m);

    struct pad_state pad = pad_with(PAD_B);
    mapper_update(&m, &pad, 0, &out);
    CHECK(out.dir[JOINT_COTOVELO] == 1.0f);
    CHECK(out.dir[JOINT_OMBRO] == 0.0f);

    pad = pad_with(PAD_Y | PAD_X);
    mapper_update(&m, &pad, 20, &out);
    CHECK(out.dir[JOINT_COTOVELO] == -1.0f);
    CHECK(out.dir[JOINT_GARRA] == -1.0f);

    pad = pad_with(PAD_A);
    mapper_update(&m, &pad, 40, &out);
    CHECK(out.dir[JOINT_GARRA] == 1.0f);

    /* B e Y juntos se anulam. */
    pad = pad_with(PAD_B | PAD_Y);
    mapper_update(&m, &pad, 60, &out);
    CHECK(out.dir[JOINT_COTOVELO] == 0.0f);
}

static void test_start_short_toggles_page(void)
{
    struct mapper m;
    struct mapper_output out;
    mapper_init(&m);

    struct pad_state pad = pad_with(PAD_START);
    mapper_update(&m, &pad, 0, &out);
    CHECK(out.events == 0);

    pad = pad_with(0);
    mapper_update(&m, &pad, 300, &out);
    CHECK(out.events & MAPPER_EVENT_PAGE_CHANGED);
    CHECK(m.page == PAGE_BASE_OMBRO);

    pad = pad_with(PAD_B | PAD_A);
    mapper_update(&m, &pad, 320, &out);
    CHECK(out.dir[JOINT_OMBRO] == 1.0f);
    CHECK(out.dir[JOINT_BASE] == 1.0f);
    CHECK(out.dir[JOINT_COTOVELO] == 0.0f);
    CHECK(out.dir[JOINT_GARRA] == 0.0f);
}

static void test_held_button_waits_release_after_toggle(void)
{
    struct mapper m;
    struct mapper_output out;
    mapper_init(&m);

    struct pad_state pad = pad_with(PAD_B | PAD_START);
    mapper_update(&m, &pad, 0, &out);
    CHECK(out.dir[JOINT_COTOVELO] == 1.0f);

    pad = pad_with(PAD_B); /* soltou START, B continua apertado */
    mapper_update(&m, &pad, 200, &out);
    CHECK(m.page == PAGE_BASE_OMBRO);
    CHECK(out.dir[JOINT_OMBRO] == 0.0f); /* não herda o B da página anterior */

    pad = pad_with(0);
    mapper_update(&m, &pad, 220, &out);

    pad = pad_with(PAD_B);
    mapper_update(&m, &pad, 240, &out);
    CHECK(out.dir[JOINT_OMBRO] == 1.0f);
}

static void test_start_long_is_home_not_toggle(void)
{
    struct mapper m;
    struct mapper_output out;
    mapper_init(&m);

    struct pad_state pad = pad_with(PAD_START);
    mapper_update(&m, &pad, 1000, &out);
    mapper_update(&m, &pad, 1000 + MAPPER_START_LONG_MS - 1, &out);
    CHECK(out.events == 0);

    mapper_update(&m, &pad, 1000 + MAPPER_START_LONG_MS, &out);
    CHECK(out.events & MAPPER_EVENT_HOME);

    mapper_update(&m, &pad, 1000 + MAPPER_START_LONG_MS + 500, &out);
    CHECK(out.events == 0); /* só uma vez */

    pad = pad_with(0);
    mapper_update(&m, &pad, 4000, &out);
    CHECK(!(out.events & MAPPER_EVENT_PAGE_CHANGED));
    CHECK(m.page == PAGE_COTOVELO_GARRA);
}

static void test_dpad_always_base_and_shoulder(void)
{
    struct mapper m;
    struct mapper_output out;
    mapper_init(&m); /* página 1: botões não mexem base/ombro */

    struct pad_state pad = { true, 0, 1, -1 }; /* direita + cima */
    mapper_update(&m, &pad, 0, &out);
    CHECK(out.dir[JOINT_BASE] == 1.0f);
    CHECK(out.dir[JOINT_OMBRO] == 1.0f);

    /* Página 2 + D-pad no mesmo sentido continua limitado a 1. */
    m.page = PAGE_BASE_OMBRO;
    pad.buttons = PAD_A;
    mapper_update(&m, &pad, 20, &out);
    CHECK(out.dir[JOINT_BASE] == 1.0f);
}

static void test_disconnected_stops(void)
{
    struct mapper m;
    struct mapper_output out;
    mapper_init(&m);

    struct pad_state pad = { false, PAD_B, 1, 1 };
    mapper_update(&m, &pad, 0, &out);

    for (int j = 0; j < JOINT_COUNT; ++j)
        CHECK(out.dir[j] == 0.0f);
}

static void test_arm_integrates_and_clamps(void)
{
    struct joint_config cfg[JOINT_COUNT];
    arm_default_config(cfg);

    struct arm_model arm;
    arm_init(&arm, cfg);
    CHECK(arm.target[JOINT_COTOVELO] == cfg[JOINT_COTOVELO].home);

    float dir[JOINT_COUNT] = { 0, 0, 1, 0 };
    arm_step(&arm, dir, 0.5f);
    CHECK(fabsf(arm.target[JOINT_COTOVELO] - (cfg[JOINT_COTOVELO].home + 0.5f * cfg[JOINT_COTOVELO].speed)) < 0.01f);

    for (int i = 0; i < 100; ++i)
        arm_step(&arm, dir, 0.1f);

    CHECK(arm.target[JOINT_COTOVELO] == cfg[JOINT_COTOVELO].max);
}

static void test_arm_homing(void)
{
    struct joint_config cfg[JOINT_COUNT];
    arm_default_config(cfg);

    struct arm_model arm;
    arm_init(&arm, cfg);
    arm.target[JOINT_BASE] = 150.0f;

    const float zero[JOINT_COUNT] = { 0 };
    arm_start_homing(&arm);
    arm_step(&arm, zero, 0.1f);
    CHECK(fabsf(arm.target[JOINT_BASE] - (150.0f - arm.home_speed * 0.1f)) < 0.01f);

    for (int i = 0; i < 100; ++i)
        arm_step(&arm, zero, 0.1f);

    CHECK(arm.target[JOINT_BASE] == cfg[JOINT_BASE].home);
    CHECK(!arm.homing);

    /* Movimento manual cancela o home. */
    arm.target[JOINT_BASE] = 150.0f;
    arm_start_homing(&arm);
    const float jog[JOINT_COUNT] = { 0, 0, 1, 0 };
    arm_step(&arm, jog, 0.1f);
    CHECK(!arm.homing);
    CHECK(arm.target[JOINT_BASE] == 150.0f);
}

static void test_arm_limits_from_firmware(void)
{
    struct joint_config cfg[JOINT_COUNT];
    arm_default_config(cfg);

    struct arm_model arm;
    arm_init(&arm, cfg);
    arm.target[JOINT_GARRA] = 85.0f;

    arm_set_limits(&arm, JOINT_GARRA, 20.0f, 70.0f, 40.0f);
    CHECK(arm.target[JOINT_GARRA] == 70.0f);
    CHECK(arm.joint[JOINT_GARRA].home == 40.0f);

    arm_set_limits(&arm, JOINT_GARRA, 80.0f, 10.0f, 40.0f); /* inválido: ignorado */
    CHECK(arm.joint[JOINT_GARRA].min == 20.0f);
}

int main(void)
{
    test_page1_default();
    test_start_short_toggles_page();
    test_held_button_waits_release_after_toggle();
    test_start_long_is_home_not_toggle();
    test_dpad_always_base_and_shoulder();
    test_disconnected_stops();
    test_arm_integrates_and_clamps();
    test_arm_homing();
    test_arm_limits_from_firmware();

    if (failures)
    {
        printf("%d verificação(ões) falharam\n", failures);
        return 1;
    }

    printf("todos os testes passaram\n");
    return 0;
}
