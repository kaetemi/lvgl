/**
 * @file lv_test_eve5.h
 *
 * Runs the test suite on the EVE5 display driver and draw unit, through the
 * EVE HAL, instead of on the test display.
 */

#ifndef LV_TEST_EVE5_H
#define LV_TEST_EVE5_H

#ifdef __cplusplus
extern "C" {
#endif

#include <../lvgl.h>

#if LV_USE_EVE5

/**
 * Open and boot the EVE device, then create the EVE5 display and draw unit
 * at the given resolution. The device is selected by the EVE_DEVICE, EVE_CHIP
 * and EVE_HEADLESS environment variables of the HAL.
 * @param hor_res   horizontal resolution
 * @param ver_res   vertical resolution
 * @return          the display, it is also the default display
 */
lv_display_t * lv_test_eve5_display_create(int32_t hor_res, int32_t ver_res);

/**
 * Close the EVE device. Call after lv_deinit() deleted the display.
 */
void lv_test_eve5_close(void);

/** Start a test case's off-screen drawing diagnostics without clearing the displayed frame's status. */
void lv_test_eve5_case_begin(void);

#endif /*LV_USE_EVE5*/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_TEST_EVE5_H*/
