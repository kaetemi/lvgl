#if LV_BUILD_TEST
#include "../lvgl.h"
#include "unity/unity.h"
#include "src/draw/lv_draw_private.h"
#include "src/drivers/display/eve5/lv_eve5.h"

static lv_obj_t * canvas;
static lv_draw_buf_t * buffer;

void setUp(void)
{
    canvas = NULL;
    buffer = NULL;
    if(!EVE_Hal_supportRenderTarget(lv_eve5_get_hal(lv_display_get_default()))) {
        TEST_IGNORE_MESSAGE("Layer alpha recovery requires render targets");
    }
    buffer = lv_draw_buf_create(128, 128, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
    TEST_ASSERT_NOT_NULL(buffer);
    canvas = lv_canvas_create(lv_screen_active());
    lv_canvas_set_draw_buf(canvas, buffer);
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_TRANSP);
    TEST_ASSERT_EQUAL(LV_RESULT_OK, lv_draw_buf_premultiply(buffer));
}

void tearDown(void)
{
    if(canvas) lv_obj_delete(canvas);
    if(buffer) lv_draw_buf_destroy(buffer);
}

/* Every primitive is white, so its premultiplied RGB must equal its alpha,
 * irrespective of hardware antialiasing or shadow filtering. Checking the
 * whole buffer catches both lost edge coverage and invented opaque pixels. */
static void assert_white_coverage(void)
{
    TEST_ASSERT_TRUE(lv_draw_buf_ensure_resident(buffer, NULL));
    TEST_ASSERT_TRUE(buffer->header.flags & LV_IMAGE_FLAGS_PREMULTIPLIED);
    unsigned covered = 0;
    for(unsigned y = 0; y < 128; y++) {
        const uint8_t * row = buffer->data + y * buffer->header.stride;
        for(unsigned x = 0; x < 128; x++) {
            const uint8_t * px = row + x * 4;
            if(px[3]) covered++;
            for(unsigned c = 0; c < 3; c++) {
                char message[96];
                lv_snprintf(message, sizeof(message), "Pixel (%u,%u): RGB=%u,%u,%u alpha=%u",
                            x, y, px[2], px[1], px[0], px[3]);
                TEST_ASSERT_UINT8_WITHIN_MESSAGE(1, px[3], px[c], message);
            }
        }
    }
    TEST_ASSERT_GREATER_THAN_UINT(1000, covered);
}

static void rounded_shadow(int32_t width)
{
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_color = lv_color_white();
    rect.radius = 13;
    rect.shadow_color = lv_color_white();
    rect.shadow_opa = 127;
    rect.shadow_width = width;
    rect.shadow_offset_y = 2;
    lv_area_t area = { 14, 14, 113, 113 };
    lv_draw_rect(&layer, &rect, &area);
    lv_canvas_finish_layer(canvas, &layer);
    assert_white_coverage();
}

void test_eve5_alpha_preserves_core_shadow_at_rounded_edge(void)
{
    rounded_shadow(2);
}

void test_eve5_alpha_preserves_blurred_shadow_at_rounded_edge(void)
{
    rounded_shadow(12);
}

void test_eve5_alpha_preserves_existing_canvas_at_rounded_edge(void)
{
    lv_draw_buf_clear_flag(buffer, LV_IMAGE_FLAGS_PREMULTIPLIED);
    lv_canvas_fill_bg(canvas, lv_color_white(), 73);
    TEST_ASSERT_EQUAL(LV_RESULT_OK, lv_draw_buf_premultiply(buffer));
    rounded_shadow(0);
}

void test_eve5_alpha_preserves_later_content_at_rounded_edge(void)
{
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    lv_draw_fill_dsc_t fill;
    lv_draw_fill_dsc_init(&fill);
    fill.color = lv_color_white();
    fill.radius = 13;
    lv_area_t area = { 14, 14, 113, 113 };
    lv_draw_fill(&layer, &fill, &area);
    fill.radius = 0;
    fill.opa = 91;
    lv_area_t overlay = { 9, 95, 31, 119 };
    lv_draw_fill(&layer, &fill, &overlay);
    lv_canvas_finish_layer(canvas, &layer);
    assert_white_coverage();
}

void test_eve5_alpha_respects_opaque_fill_clip(void)
{
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    lv_area_t original_clip = layer._clip_area;
    layer._clip_area.x2 = 63;
    lv_draw_fill_dsc_t fill;
    lv_draw_fill_dsc_init(&fill);
    fill.color = lv_color_white();
    fill.radius = 13;
    lv_area_t area = { 14, 14, 113, 113 };
    lv_draw_fill(&layer, &fill, &area);
    layer._clip_area = original_clip;
    fill.radius = 0;
    fill.opa = 127;
    lv_draw_fill(&layer, &fill, &area);
    lv_canvas_finish_layer(canvas, &layer);
    assert_white_coverage();
    const uint8_t * px = buffer->data + 64 * buffer->header.stride + 96 * 4;
    TEST_ASSERT_EQUAL_UINT8(127, px[3]);
}

void test_eve5_alpha_keeps_merged_border_interior_opaque(void)
{
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_color = lv_color_white();
    rect.border_color = lv_color_white();
    rect.border_width = 2;
    lv_area_t area = { 14, 14, 113, 113 };
    lv_draw_rect(&layer, &rect, &area);
    lv_canvas_finish_layer(canvas, &layer);
    assert_white_coverage();
    const uint8_t * px = buffer->data + 64 * buffer->header.stride + 64 * 4;
    TEST_ASSERT_EQUAL_UINT8(255, px[3]);
}

void test_eve5_alpha_does_not_expand_fill_to_differently_clipped_border(void)
{
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    lv_area_t original_clip = layer._clip_area;
    layer._clip_area.x2 = 63;
    lv_draw_fill_dsc_t fill;
    lv_draw_fill_dsc_init(&fill);
    fill.color = lv_color_white();
    lv_area_t area = { 14, 14, 113, 113 };
    lv_draw_fill(&layer, &fill, &area);
    layer._clip_area = original_clip;
    lv_draw_border_dsc_t border;
    lv_draw_border_dsc_init(&border);
    border.color = lv_color_white();
    border.width = 2;
    lv_draw_border(&layer, &border, &area);
    lv_canvas_finish_layer(canvas, &layer);
    assert_white_coverage();
    const uint8_t * px = buffer->data + 64 * buffer->header.stride + 96 * 4;
    TEST_ASSERT_EQUAL_UINT8(0, px[3]);
}
#endif
