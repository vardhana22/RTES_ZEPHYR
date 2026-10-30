
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>
#include <stdbool.h>
#include <errno.h>

/* Personalized timing: roll number 2026mt01117 */
#define TON_MS       800
#define TOFF_MS      500

#define STACK_SIZE   1024
#define THREAD_PRIO  5

/* Read LED connections from the board Devicetree */
#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)

static const struct gpio_dt_spec leds[2] = {
    GPIO_DT_SPEC_GET(LED0_NODE, gpios),
    GPIO_DT_SPEC_GET(LED1_NODE, gpios)
};

/* Separate thread stacks and control blocks */
K_THREAD_STACK_DEFINE(led0_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(led1_stack, STACK_SIZE);

static struct k_thread led0_thread;
static struct k_thread led1_thread;

static k_tid_t thread_ids[2];
static bool thread_running[2] = {false, false};
static bool initialized = false;

/* Each thread independently controls its own LED */
static void led_thread(void *p1, void *p2, void *p3)
{
    int id = (int)(uintptr_t)p1;

    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        gpio_pin_set_dt(&leds[id], 1);
        k_msleep(TON_MS);

        gpio_pin_set_dt(&leds[id], 0);
        k_msleep(TOFF_MS);
    }
}

/* Validate thread number entered in the Shell */
static int get_thread_id(const struct shell *sh,
                         size_t argc, char **argv)
{
    if (argc != 2 ||
        (strcmp(argv[1], "0") != 0 &&
         strcmp(argv[1], "1") != 0)) {
        shell_error(sh, "Usage: %s <0|1>", argv[0]);
        return -EINVAL;
    }

    return argv[1][0] - '0';
}

/* Command: start_thread 0 or start_thread 1 */
static int cmd_start_thread(const struct shell *sh,
                            size_t argc, char **argv)
{
    int id = get_thread_id(sh, argc, argv);

    if (id < 0) {
        return id;
    }

    if (!initialized) {
        shell_error(sh, "Threads not initialized");
        return -EAGAIN;
    }

    if (thread_running[id]) {
        shell_print(sh, "LED%d already RUNNING", id);
        return 0;
    }

    thread_running[id] = true;
    k_thread_resume(thread_ids[id]);

    shell_print(sh, "LED%d thread RUNNING", id);
    return 0;
}

/* Command: suspend_thread 0 or suspend_thread 1 */
static int cmd_suspend_thread(const struct shell *sh,
                              size_t argc, char **argv)
{
    int id = get_thread_id(sh, argc, argv);

    if (id < 0) {
        return id;
    }

    if (!initialized) {
        shell_error(sh, "Threads not initialized");
        return -EAGAIN;
    }

    if (thread_running[id]) {
        k_thread_suspend(thread_ids[id]);
        thread_running[id] = false;
    }

    /* Ensure the LED is OFF even if suspended during ON time */
    gpio_pin_set_dt(&leds[id], 0);

    shell_print(sh, "LED%d thread SUSPENDED", id);
    return 0;
}

/* Command: thread_status */
static int cmd_thread_status(const struct shell *sh,
                             size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (!initialized) {
        shell_error(sh, "Threads not initialized");
        return -EAGAIN;
    }

    shell_print(sh, "LED0: %s",
                thread_running[0] ? "RUNNING" : "SUSPENDED");

    shell_print(sh, "LED1: %s",
                thread_running[1] ? "RUNNING" : "SUSPENDED");

    return 0;
}

/* Register the three top-level Zephyr Shell commands */
SHELL_CMD_ARG_REGISTER(start_thread, NULL,
    "Resume LED thread: start_thread <0|1>",
    cmd_start_thread, 2, 0);

SHELL_CMD_ARG_REGISTER(suspend_thread, NULL,
    "Suspend LED thread: suspend_thread <0|1>",
    cmd_suspend_thread, 2, 0);

SHELL_CMD_REGISTER(thread_status, NULL,
    "Display both LED thread states",
    cmd_thread_status);

int main(void)
{
    /* Check both onboard LED devices */
    for (int i = 0; i < 2; i++) {
        if (!gpio_is_ready_dt(&leds[i])) {
            return 0;
        }

        if (gpio_pin_configure_dt(
                &leds[i], GPIO_OUTPUT_INACTIVE) < 0) {
            return 0;
        }
    }

    /*
     * Create each thread at priority 5.
     * Main runs at a higher scheduling priority
     * during initialization, allowing us to
     * suspend each newly created thread.
     */
    thread_ids[0] = k_thread_create(
        &led0_thread,
        led0_stack,
        K_THREAD_STACK_SIZEOF(led0_stack),
        led_thread,
        (void *)(uintptr_t)0, NULL, NULL,
        THREAD_PRIO, 0, K_NO_WAIT);

    k_thread_suspend(thread_ids[0]);

    thread_ids[1] = k_thread_create(
        &led1_thread,
        led1_stack,
        K_THREAD_STACK_SIZEOF(led1_stack),
        led_thread,
        (void *)(uintptr_t)1, NULL, NULL,
        THREAD_PRIO, 0, K_NO_WAIT);

    k_thread_suspend(thread_ids[1]);

    initialized = true;

    /* Shell runs in its own Zephyr thread */
    while (1) {
        k_msleep(1000);
    }

    return 0;
}
