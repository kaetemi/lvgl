/**
 * @file lv_test_screenshot_compare.h
 *
 */

#ifndef LV_TEST_SCREENSHOT_COMPARE_H
#define LV_TEST_SCREENSHOT_COMPARE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "../../config/lv_conf_internal.h"
#if LV_USE_TEST && defined(LV_USE_TEST_SCREENSHOT_COMPARE) && LV_USE_TEST_SCREENSHOT_COMPARE

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**
 * Return value of `lv_test_screenshot_compare`
 */
typedef enum {
    /**
     * The screenshot is different than the reference image
     */
    LV_TEST_SCREENSHOT_RESULT_FAILED,

    /**
     * The screenshot is the same as the reference image.
     * It is also returned if `LV_TEST_SCREENSHOT_CREATE_REFERENCE_IMAGE` is enabled
     * and the reference image was missing.
     */
    LV_TEST_SCREENSHOT_RESULT_PASSED,

    /**
     * If `LV_TEST_SCREENSHOT_CREATE_REFERENCE_IMAGE` is not enabled
     * and the reference image is missing.
     */
    LV_TEST_SCREENSHOT_RESULT_NO_REFERENCE_IMAGE,

} lv_test_screenshot_result_t;

/**
 * Reads the frame shown on a display, for displays whose frame is not in their active
 * draw buffer, such as a display rendered by an external GPU.
 * @param disp      the display
 * @param buf       destination for the display resolution, 4 bytes per pixel: R, G, B, A
 * @param stride    bytes per row in `buf`
 * @return          true on success
 */
typedef bool (*lv_test_screenshot_read_cb_t)(lv_display_t * disp, uint8_t * buf, uint32_t stride);

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Set how screenshots read the displayed frame. By default they read the active draw buffer
 * of the default display, which holds the whole frame on `lv_test_display_create()`'s display.
 * @param read_cb   function that reads the displayed frame, or NULL to read the active draw buffer
 */
void lv_test_screenshot_set_read_cb(lv_test_screenshot_read_cb_t read_cb);

/**
 * Read the frame shown on the default display, the way screenshots read it, without
 * redrawing it first.
 * @param buf   destination for the display resolution, 4 bytes per pixel: R, G, B, A
 * @return      true on success
 */
bool lv_test_screenshot_read(uint8_t * buf);

/**
 * Invalidate and redraw the current screen and compare its content with a reference PNG image
 * - If the reference image is not found it will be created automatically from the rendered screen.
 * - If the compare fails an `<image_name>_err.png` file will be created with the rendered content next to the reference image.
 *
 * It requires lodepng.
 *
 * @param fn_ref    path to the reference image. Will be appended to REF_IMGS_PATH if set.
 * @return          An element of `lv_test_screenshot_result_t`
 * @note            This function assumes that the default display is the test display that was created by
 *                  `lv_test_display_create()`
 */
lv_test_screenshot_result_t lv_test_screenshot_compare(const char * fn_ref);


/**
 * Works the same way as `lv_test_screenshot_compare` but it doesn't invalidate and redraw the screen
 * but uses its current content. (lv_refr_now(NULL) might be called earlier)
 * @param fn_ref    reference image path
 */
lv_test_screenshot_result_t lv_test_screenshot_compare_core(const char * fn_ref);

/**********************
 *      MACROS
 **********************/

#endif /*LV_USE_TEST_SCREENSHOT_COMPARE*/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_TEST_SCREENSHOT_COMPARE_H*/
