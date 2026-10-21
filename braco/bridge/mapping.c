#include "mapping.h"

#include <string.h>

#define FACE_BUTTONS (PAD_A | PAD_B | PAD_X | PAD_Y)

uint32_t pad_button_from_name(const char *name)
{
    static const struct { const char *name; uint32_t bit; } table[] =
    {
        { "A", PAD_A }, { "B", PAD_B }, { "X", PAD_X }, { "Y", PAD_Y },
        { "L", PAD_L }, { "R", PAD_R },
        { "SELECT", PAD_SELECT }, { "START", PAD_START }
    };

    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i)
    {
        if (strcmp(name, table[i].name) == 0)
            return table[i].bit;
    }

    return 0;
}

const char *mapper_page_name(enum mapper_page page)
{
    return page == PAGE_BASE_OMBRO ? "BASE/OMBRO" : "COTOVELO/GARRA";
}

void mapper_init(struct mapper *m)
{
    memset(m, 0, sizeof(*m));
    m->page = PAGE_COTOVELO_GARRA;
}

/* +1 se só "plus" está pressionado, -1 se só "minus", 0 caso contrário. */
static float axis(uint32_t buttons, uint32_t plus, uint32_t minus)
{
    const bool p = (buttons & plus) != 0;
    const bool n = (buttons & minus) != 0;

    if (p && !n)
        return 1.0f;

    if (n && !p)
        return -1.0f;

    return 0.0f;
}

static float clamp_dir(float value)
{
    if (value > 1.0f)
        return 1.0f;

    if (value < -1.0f)
        return -1.0f;

    return value;
}

static void update_start(struct mapper *m, bool pressed, uint32_t now_ms, uint32_t *events)
{
    if (pressed && !m->start_down)
    {
        m->start_down = true;
        m->start_long_fired = false;
        m->start_down_ms = now_ms;
        return;
    }

    if (pressed && !m->start_long_fired &&
        (uint32_t)(now_ms - m->start_down_ms) >= MAPPER_START_LONG_MS)
    {
        m->start_long_fired = true;
        *events |= MAPPER_EVENT_HOME;
        return;
    }

    if (!pressed && m->start_down)
    {
        m->start_down = false;

        if (!m->start_long_fired)
        {
            m->page = (m->page == PAGE_COTOVELO_GARRA)
                ? PAGE_BASE_OMBRO
                : PAGE_COTOVELO_GARRA;

            m->wait_release = true;
            *events |= MAPPER_EVENT_PAGE_CHANGED;
        }
    }
}

void mapper_update(struct mapper *m,
                   const struct pad_state *pad,
                   uint32_t now_ms,
                   struct mapper_output *out)
{
    memset(out, 0, sizeof(*out));

    if (!pad->connected)
    {
        /* Controle sumiu: nada se move e um START pela metade é esquecido. */
        m->start_down = false;
        return;
    }

    update_start(m, (pad->buttons & PAD_START) != 0, now_ms, &out->events);

    uint32_t face = pad->buttons & FACE_BUTTONS;

    /*
     * Um botão segurado durante a troca de página não pode começar a mover
     * a junta da página nova: espera todos serem soltos.
     */
    if (m->wait_release)
    {
        if (face == 0)
            m->wait_release = false;
        else
            face = 0;
    }

    if (m->page == PAGE_COTOVELO_GARRA)
    {
        out->dir[JOINT_COTOVELO] = axis(face, PAD_B, PAD_Y);
        out->dir[JOINT_GARRA]    = axis(face, PAD_A, PAD_X);
    }
    else
    {
        out->dir[JOINT_OMBRO] = axis(face, PAD_B, PAD_Y);
        out->dir[JOINT_BASE]  = axis(face, PAD_A, PAD_X);
    }

    /* D-pad: cima (hat_y = -1) sobe o ombro; direita (hat_x = +1) gira a base no sentido (+). */
    out->dir[JOINT_BASE]  = clamp_dir(out->dir[JOINT_BASE] + (float)pad->hat_x);
    out->dir[JOINT_OMBRO] = clamp_dir(out->dir[JOINT_OMBRO] - (float)pad->hat_y);
}
