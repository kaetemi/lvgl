/**
 * @file lv_test_eve5.c
 *
 */

#if LV_BUILD_TEST
#include "lv_test_eve5.h"

#if LV_USE_EVE5

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../unity/unity.h"
#include "../../src/drivers/display/eve5/lv_eve5.h"
#include "../../src/draw/eve5/lv_draw_eve5.h"

static EVE_HalContext s_host;
static EVE_GpuAlloc s_allocator;
static bool s_opened;

static bool idle_cb(EVE_HalContext * phost);
static bool read_screen_cb(lv_display_t * disp, uint8_t * buf, uint32_t stride);

lv_display_t * lv_test_eve5_display_create(int32_t hor_res, int32_t ver_res)
{
    EVE_HalContext * phost = &s_host;

    /* Keep the output in order with the emulator's, and complete when a
     * sanitizer ends the process */
    setvbuf(stdout, NULL, _IOLBF, 0);

    EVE_Hal_initialize();
    if(!EVE_Util_openDeviceInteractive(phost, NULL)) {
        TEST_PRINTF("EVE5: could not open the EVE device");
        exit(EXIT_FAILURE);
    }
    s_opened = true;
    phost->CbCmdWait = idle_cb;

    /* The reference images are 800x480, the WVGA preset */
    EVE_BootupParameters bootup;
    EVE_Util_bootupDefaults(phost, &bootup);
    EVE_ConfigParameters config;
    if(!EVE_Util_bootup(phost, &bootup)) {
        TEST_PRINTF("EVE5: could not boot the EVE device");
        exit(EXIT_FAILURE);
    }
    EVE_Util_configDefaults(phost, &config, EVE_DISPLAY_WVGA_800x480_74Hz);
    if(!EVE_Util_config(phost, &config)) {
        TEST_PRINTF("EVE5: could not configure the EVE display");
        exit(EXIT_FAILURE);
    }
    if((int32_t)phost->Width != hor_res || (int32_t)phost->Height != ver_res) {
        TEST_PRINTF("EVE5: the display is %dx%d instead of %dx%d", (int)phost->Width, (int)phost->Height,
                    (int)hor_res, (int)ver_res);
        exit(EXIT_FAILURE);
    }

    /* LV_TEST_EVE5_RENDER_MODE=full renders full frames into the double
     * buffered swapchain instead of tiles */
    const char * mode_env = getenv("LV_TEST_EVE5_RENDER_MODE");
    lv_eve5_render_mode_t mode = (mode_env && !strcmp(mode_env, "full")) ?
                                 LV_EVE5_RENDER_MODE_FULL : LV_EVE5_RENDER_MODE_PARTIAL;
    lv_display_t * disp = lv_eve5_create_ex(phost, &s_allocator, mode);
    if(disp == NULL) {
        TEST_PRINTF("EVE5: could not create the display");
        exit(EXIT_FAILURE);
    }
    lv_draw_eve5_init(phost, &s_allocator);

    /* The display driver sets EVE_millis as the tick source, but the tests
     * advance time themselves with lv_tick_inc() */
    lv_tick_set_cb(NULL);

    lv_test_screenshot_set_read_cb(read_screen_cb);
    return disp;
}

void lv_test_eve5_close(void)
{
    if(!s_opened) return;
    lv_draw_eve5_deinit();
    EVE_Hal_close(&s_host);
    EVE_Hal_release();
    s_opened = false;
}

static bool read_screen_cb(lv_display_t * disp, uint8_t * buf, uint32_t stride)
{
    return lv_eve5_read_screen(disp, buf, stride);
}

static bool idle_cb(EVE_HalContext * phost)
{
    /* Free the GPU memory whose last use the coprocessor has completed */
    EVE_GpuAlloc_UpdateFree(&s_allocator, EVE_Cmd_syncCompleted(phost));
    return true;
}

#endif /*LV_USE_EVE5*/
#endif /*LV_BUILD_TEST*/
