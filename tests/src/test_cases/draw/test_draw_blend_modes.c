#if LV_BUILD_TEST
#include "../lvgl.h"
#include "../../lvgl_private.h"

#include "unity/unity.h"

/* Image blend modes on canvases of each color format: a row per format, a
 * column per blend mode */

#define CANVAS_W 150
#define CANVAS_H 90

static const lv_color_format_t canvas_cfs[] = {
    LV_COLOR_FORMAT_ARGB8888,
    LV_COLOR_FORMAT_XRGB8888,
    LV_COLOR_FORMAT_RGB888,
    LV_COLOR_FORMAT_RGB565,
    LV_COLOR_FORMAT_L8,
};

static const lv_blend_mode_t blend_modes[] = {
    LV_BLEND_MODE_NORMAL,
    LV_BLEND_MODE_ADDITIVE,
    LV_BLEND_MODE_SUBTRACTIVE,
    LV_BLEND_MODE_MULTIPLY,
    LV_BLEND_MODE_DIFFERENCE,
};

#define ROWS (sizeof(canvas_cfs) / sizeof(canvas_cfs[0]))
#define COLS (sizeof(blend_modes) / sizeof(blend_modes[0]))

static LV_ATTRIBUTE_MEM_ALIGN uint8_t canvas_bufs[ROWS * COLS][LV_TEST_WIDTH_TO_STRIDE(CANVAS_W, 4) * CANVAS_H +
                                                                LV_DRAW_BUF_ALIGN];

void setUp(void)
{
    /* Function run before every test */
}

void tearDown(void)
{
    /* Function run after every test */
    lv_obj_clean(lv_screen_active());
}

/**
 * Draw the cogwheel on each canvas with each blend mode.
 * @param with_gradient true: draw a gradient on the canvas before the image,
 *                      false: the image is the first thing drawn on the filled canvas
 */
static void blend_modes_render(bool with_gradient, lv_opa_t opa)
{
    LV_IMAGE_DECLARE(test_image_cogwheel_argb8888);

    for(uint32_t row = 0; row < ROWS; row++) {
        for(uint32_t col = 0; col < COLS; col++) {
            lv_obj_t * canvas = lv_canvas_create(lv_screen_active());
            lv_canvas_set_buffer(canvas, lv_draw_buf_align(canvas_bufs[row * COLS + col], canvas_cfs[row]),
                                 CANVAS_W, CANVAS_H, canvas_cfs[row]);
            lv_obj_set_pos(canvas, (int32_t)col * (CANVAS_W + 10) + 5, (int32_t)row * (CANVAS_H + 6) + 3);
            lv_canvas_fill_bg(canvas, lv_color_hex(0x8fb8d8), LV_OPA_COVER);

            lv_layer_t layer;
            lv_canvas_init_layer(canvas, &layer);

            if(with_gradient) {
                lv_draw_rect_dsc_t rect_dsc;
                lv_draw_rect_dsc_init(&rect_dsc);
                rect_dsc.bg_grad.dir = LV_GRAD_DIR_HOR;
                rect_dsc.bg_grad.stops_count = 2;
                rect_dsc.bg_grad.stops[0].color = lv_color_hex(0xff4040);
                rect_dsc.bg_grad.stops[0].opa = LV_OPA_COVER;
                rect_dsc.bg_grad.stops[0].frac = 0;
                rect_dsc.bg_grad.stops[1].color = lv_color_hex(0x40c0ff);
                rect_dsc.bg_grad.stops[1].opa = LV_OPA_COVER;
                rect_dsc.bg_grad.stops[1].frac = 255;
                lv_area_t rect_area = {0, CANVAS_H / 3, CANVAS_W - 1, CANVAS_H - 1};
                lv_draw_rect(&layer, &rect_dsc, &rect_area);
            }

            lv_draw_image_dsc_t img_dsc;
            lv_draw_image_dsc_init(&img_dsc);
            img_dsc.src = &test_image_cogwheel_argb8888;
            img_dsc.blend_mode = blend_modes[col];
            img_dsc.opa = opa;
            lv_area_t img_area = {25, -5, 25 + 99, -5 + 99};
            lv_draw_image(&layer, &img_dsc, &img_area);

            lv_canvas_finish_layer(canvas, &layer);
        }
    }
}

void test_draw_blend_modes_over_content(void)
{
    blend_modes_render(true, LV_OPA_COVER);
    TEST_ASSERT_EQUAL_SCREENSHOT("draw/blend_modes_1.png");
}

void test_draw_blend_modes_first_task(void)
{
    blend_modes_render(false, LV_OPA_50);
    TEST_ASSERT_EQUAL_SCREENSHOT("draw/blend_modes_2.png");
}

#endif
