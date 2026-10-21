#include <jni.h>
#include <android/log.h>

#include <linux/uinput.h>

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

#define LOG_TAG "SnesUInput"

#define LOGI(...) \
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define LOGE(...) \
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static int uinput_fd = -1;

/*
 * Estado SNES anterior.
 *
 * 0xFFFF = todos os botoes soltos.
 */
static unsigned short previous_state = 0xFFFF;


/*
 * ============================================================
 * EVENTO LINUX
 * ============================================================
 */

static void emit_event(
    int type,
    int code,
    int value)
{
    struct input_event ev;

    memset(
        &ev,
        0,
        sizeof(ev)
    );

    ev.type = type;
    ev.code = code;
    ev.value = value;

    if (write(
            uinput_fd,
            &ev,
            sizeof(ev)) < 0)
    {
        LOGE(
            "write input_event failed: %s",
            strerror(errno)
        );
    }
}


/*
 * ============================================================
 * SYN REPORT
 * ============================================================
 */

static void sync_report()
{
    emit_event(
        EV_SYN,
        SYN_REPORT,
        0
    );
}


/*
 * ============================================================
 * CRIA UINPUT
 * ============================================================
 */

static int create_uinput()
{
    if (uinput_fd >= 0)
    {
        return 0;
    }

    uinput_fd =
        open(
            "/dev/uinput",
            O_WRONLY | O_NONBLOCK
        );

    if (uinput_fd < 0)
    {
        LOGE(
            "Nao foi possivel abrir /dev/uinput: %s",
            strerror(errno)
        );

        return -1;
    }

    LOGI(
        "/dev/uinput aberto"
    );


    /*
     * --------------------------------------------------------
     * EVENTOS DE TECLAS
     * --------------------------------------------------------
     */

    if (ioctl(
            uinput_fd,
            UI_SET_EVBIT,
            EV_KEY) < 0)
    {
        LOGE(
            "UI_SET_EVBIT EV_KEY failed: %s",
            strerror(errno)
        );

        close(uinput_fd);
        uinput_fd = -1;

        return -1;
    }


    /*
     * Botoes do controle.
     */

    int buttons[] =
    {
        BTN_A,
        BTN_B,
        BTN_X,
        BTN_Y,
        BTN_TL,
        BTN_TR,
        BTN_SELECT,
        BTN_START
    };

    const int button_count =
        sizeof(buttons) / sizeof(buttons[0]);

    for (int i = 0; i < button_count; ++i)
    {
        if (ioctl(
                uinput_fd,
                UI_SET_KEYBIT,
                buttons[i]) < 0)
        {
            LOGE(
                "UI_SET_KEYBIT failed: %d",
                buttons[i]
            );

            close(uinput_fd);
            uinput_fd = -1;

            return -1;
        }
    }


    /*
     * --------------------------------------------------------
     * D-PAD
     * --------------------------------------------------------
     */

    if (ioctl(
            uinput_fd,
            UI_SET_EVBIT,
            EV_ABS) < 0)
    {
        LOGE(
            "UI_SET_EVBIT EV_ABS failed: %s",
            strerror(errno)
        );

        close(uinput_fd);
        uinput_fd = -1;

        return -1;
    }


    int axes[] =
    {
        ABS_HAT0X,
        ABS_HAT0Y
    };

    const int axis_count =
        sizeof(axes) / sizeof(axes[0]);

    for (int i = 0; i < axis_count; ++i)
    {
        if (ioctl(
                uinput_fd,
                UI_SET_ABSBIT,
                axes[i]) < 0)
        {
            LOGE(
                "UI_SET_ABSBIT failed: %d",
                axes[i]
            );

            close(uinput_fd);
            uinput_fd = -1;

            return -1;
        }

        struct uinput_abs_setup abs_setup;

        memset(
            &abs_setup,
            0,
            sizeof(abs_setup)
        );

        abs_setup.code =
            axes[i];

        abs_setup.absinfo.minimum =
            -1;

        abs_setup.absinfo.maximum =
            1;

        abs_setup.absinfo.flat =
            0;

        abs_setup.absinfo.fuzz =
            0;

        if (ioctl(
                uinput_fd,
                UI_ABS_SETUP,
                &abs_setup) < 0)
        {
            LOGE(
                "UI_ABS_SETUP failed: %d: %s",
                axes[i],
                strerror(errno)
            );

            close(uinput_fd);
            uinput_fd = -1;

            return -1;
        }
    }


    /*
     * --------------------------------------------------------
     * IDENTIDADE DO DISPOSITIVO
     * --------------------------------------------------------
     */

    struct uinput_setup setup;

    memset(
        &setup,
        0,
        sizeof(setup)
    );

    setup.id.bustype =
        BUS_VIRTUAL;

    setup.id.vendor =
        0x1234;

    setup.id.product =
        0x0001;

    setup.id.version =
        1;

    strncpy(
        setup.name,
        "SNES-BT-Controller",
        UINPUT_MAX_NAME_SIZE - 1
    );


    if (ioctl(
            uinput_fd,
            UI_DEV_SETUP,
            &setup) < 0)
    {
        LOGE(
            "UI_DEV_SETUP failed: %s",
            strerror(errno)
        );

        close(uinput_fd);
        uinput_fd = -1;

        return -1;
    }


    /*
     * Cria o dispositivo.
     */

    if (ioctl(
            uinput_fd,
            UI_DEV_CREATE) < 0)
    {
        LOGE(
            "UI_DEV_CREATE failed: %s",
            strerror(errno)
        );

        close(uinput_fd);
        uinput_fd = -1;

        return -1;
    }


    /*
     * Dá tempo para o kernel registrar
     * o dispositivo.
     */

    usleep(100000);

    previous_state =
        0xFFFF;

    LOGI(
        "Dispositivo uinput criado: SNES-BT-Controller"
    );

    return 0;
}


/*
 * ============================================================
 * DESTROI UINPUT
 * ============================================================
 */

static void destroy_uinput()
{
    if (uinput_fd < 0)
    {
        return;
    }

    ioctl(
        uinput_fd,
        UI_DEV_DESTROY
    );

    close(
        uinput_fd
    );

    uinput_fd =
        -1;

    LOGI(
        "Dispositivo uinput destruido"
    );
}


/*
 * ============================================================
 * ATUALIZA BOTAO
 * ============================================================
 *
 * SNES:
 *
 * 0 = pressionado
 * 1 = solto
 *
 */

static void update_button(
    unsigned short state,
    unsigned short previous,
    unsigned short mask,
    int key_code)
{
    int current_pressed =
        ((state & mask) == 0);

    int previous_pressed =
        ((previous & mask) == 0);

    /*
     * Estado não mudou.
     */

    if (current_pressed ==
        previous_pressed)
    {
        return;
    }

    /*
     * 1 = DOWN
     * 0 = UP
     */

    emit_event(
        EV_KEY,
        key_code,
        current_pressed ? 1 : 0
    );
}


/*
 * ============================================================
 * ATUALIZA D-PAD
 * ============================================================
 */

static void update_dpad(
    unsigned short state,
    unsigned short previous)
{
    /*
     * Estado atual.
     *
     * X:
     *
     * LEFT  = -1
     * RIGHT =  1
     * NONE  =  0
     *
     * Y:
     *
     * UP   = -1
     * DOWN =  1
     * NONE =  0
     */

    int x = 0;
    int y = 0;

    if ((state & (1 << 6)) == 0)
    {
        x = -1;
    }
    else if ((state & (1 << 7)) == 0)
    {
        x = 1;
    }

    if ((state & (1 << 4)) == 0)
    {
        y = -1;
    }
    else if ((state & (1 << 5)) == 0)
    {
        y = 1;
    }


    /*
     * Estado anterior.
     */

    int previous_x = 0;
    int previous_y = 0;

    if ((previous & (1 << 6)) == 0)
    {
        previous_x = -1;
    }
    else if ((previous & (1 << 7)) == 0)
    {
        previous_x = 1;
    }

    if ((previous & (1 << 4)) == 0)
    {
        previous_y = -1;
    }
    else if ((previous & (1 << 5)) == 0)
    {
        previous_y = 1;
    }


    /*
     * Só envia se mudou.
     */

    if (x != previous_x)
    {
        emit_event(
            EV_ABS,
            ABS_HAT0X,
            x
        );
    }

    if (y != previous_y)
    {
        emit_event(
            EV_ABS,
            ABS_HAT0Y,
            y
        );
    }
}


/*
 * ============================================================
 * ATUALIZA ESTADO SNES
 * ============================================================
 */

static void update_state(
    unsigned short state)
{
    if (uinput_fd < 0)
    {
        return;
    }

    unsigned short previous =
        previous_state;


    /*
     * B
     * bit 0
     */

    update_button(
        state,
        previous,
        (1 << 0),
        BTN_B
    );


    /*
     * Y
     * bit 1
     */

    update_button(
        state,
        previous,
        (1 << 1),
        BTN_Y
    );


    /*
     * SELECT
     * bit 2
     */

    update_button(
        state,
        previous,
        (1 << 2),
        BTN_SELECT
    );


    /*
     * START
     * bit 3
     */

    update_button(
        state,
        previous,
        (1 << 3),
        BTN_START
    );


    /*
     * D-PAD
     */

    update_dpad(
        state,
        previous
    );


    /*
     * A
     * bit 8
     */

    update_button(
        state,
        previous,
        (1 << 8),
        BTN_A
    );


    /*
     * X
     * bit 9
     */

    update_button(
        state,
        previous,
        (1 << 9),
        BTN_X
    );


    /*
     * L
     * bit 10
     */

    update_button(
        state,
        previous,
        (1 << 10),
        BTN_TL
    );


    /*
     * R
     * bit 11
     */

    update_button(
        state,
        previous,
        (1 << 11),
        BTN_TR
    );


    /*
     * Finaliza o pacote de eventos.
     */

    if (state != previous)
    {
        sync_report();
    }


    previous_state =
        state;
}


/*
 * ============================================================
 * JNI - INIT
 * ============================================================
 */

JNIEXPORT jboolean JNICALL
Java_com_snesbt_receiver_MainActivity_nativeInitUInput(
    JNIEnv *env,
    jobject thiz)
{
    (void)env;
    (void)thiz;

    return
        create_uinput() == 0
        ? JNI_TRUE
        : JNI_FALSE;
}


/*
 * ============================================================
 * JNI - UPDATE STATE
 * ============================================================
 */

JNIEXPORT void JNICALL
Java_com_snesbt_receiver_MainActivity_nativeUpdateState(
    JNIEnv *env,
    jobject thiz,
    jint state)
{
    (void)env;
    (void)thiz;

    update_state(
        (unsigned short)(state & 0xFFFF)
    );
}


/*
 * ============================================================
 * JNI - DESTROY
 * ============================================================
 */

JNIEXPORT void JNICALL
Java_com_snesbt_receiver_MainActivity_nativeDestroyUInput(
    JNIEnv *env,
    jobject thiz)
{
    (void)env;
    (void)thiz;

    destroy_uinput();
}