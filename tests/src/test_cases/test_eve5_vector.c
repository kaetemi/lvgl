#if LV_BUILD_TEST
#include "../lvgl.h"
#include "unity/unity.h"
#include "src/drivers/display/eve5/lv_eve5.h"
#include "src/draw/eve5/lv_draw_eve5.h"
#include "src/draw/lv_draw_private.h"
#include "src/draw/sw/lv_draw_sw.h"

static uint8_t * reference;
static uint8_t * actual;
static size_t frame_size;
static lv_vector_blend_t blend_mode;
static lv_draw_buf_t * canvas_buf;

void setUp(void)
{
    reference = actual = NULL;
    canvas_buf = NULL;
    if(!EVE_Hal_supportRenderTarget(lv_eve5_get_hal(lv_display_get_default()))) {
        TEST_IGNORE_MESSAGE("Vector backdrop rendering requires render targets");
    }
    lv_obj_t * screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x31577b), 0);
    frame_size = (size_t)lv_display_get_horizontal_resolution(NULL) * lv_display_get_vertical_resolution(NULL) * 4;
    reference = lv_malloc(frame_size);
    actual = lv_malloc(frame_size);
    TEST_ASSERT_NOT_NULL(reference);
    TEST_ASSERT_NOT_NULL(actual);
}

void tearDown(void)
{
    lv_draw_eve5_set_enabled(true);
    lv_obj_clean(lv_screen_active());
    if(canvas_buf) lv_draw_buf_destroy(canvas_buf);
    lv_free(reference);
    lv_free(actual);
}

static void vector_rect(lv_layer_t * layer, const lv_area_t * area, lv_color_t color, lv_opa_t opa)
{
    lv_draw_vector_dsc_t * dsc = lv_draw_vector_dsc_create(layer);
    lv_vector_path_t * path = lv_vector_path_create(LV_VECTOR_PATH_QUALITY_MEDIUM);
    lv_vector_path_append_rect(path, area, 0, 0);
    lv_draw_vector_dsc_set_fill_color(dsc, color);
    lv_draw_vector_dsc_set_fill_opa(dsc, opa);
    lv_draw_vector_dsc_set_stroke_opa(dsc, LV_OPA_TRANSP);
    lv_draw_vector_dsc_set_blend_mode(dsc, blend_mode);
    lv_draw_vector_dsc_add_path(dsc, path);
    lv_draw_vector(dsc);
    lv_vector_path_delete(path);
    lv_draw_vector_dsc_delete(dsc);
}

static void draw_scene(lv_event_t * e)
{
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target_obj(e), &area);
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_color = lv_color_hex(0x7b395d);
    lv_area_t left = area;
    left.x2 = left.x1 + 57;
    lv_draw_rect(layer, &rect, &left);

    /* Nonzero origin, two different backdrops, and several partial-mode tile boundaries. */
    lv_area_t vector = { area.x1 + 9, area.y1 + 7, area.x2 - 8, area.y2 - 6 };
    vector_rect(layer, &vector, lv_color_hex(0xc57139), LV_OPA_COVER);

    /* A hardware task after the software blend must preserve its output. */
    lv_area_t tail = { area.x1 + 28, area.y1 + 81, area.x2 - 23, area.y1 + 89 };
    rect.bg_color = lv_color_hex(0x29af83);
    lv_draw_rect(layer, &rect, &tail);
}

static void capture(uint8_t * pixels)
{
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    TEST_ASSERT_TRUE(lv_test_screenshot_read(pixels));
}

static void assert_pixels(void)
{
    unsigned max_diff = 0;
    size_t worst = 0;
    for(size_t i = 0; i < frame_size; i++) {
        if(i % 4 == 3) continue;
        unsigned diff = LV_ABS((int)reference[i] - (int)actual[i]);
        if(diff > max_diff) {
            max_diff = diff;
            worst = i;
        }
    }
    char message[160];
    size_t pixel = worst / 4;
    size_t width = lv_display_get_horizontal_resolution(NULL);
    lv_snprintf(message, sizeof(message), "Vector pixel (%u,%u): expected %u,%u,%u, got %u,%u,%u",
                (unsigned)(pixel % width), (unsigned)(pixel / width),
                reference[pixel * 4], reference[pixel * 4 + 1], reference[pixel * 4 + 2],
                actual[pixel * 4], actual[pixel * 4 + 1], actual[pixel * 4 + 2]);
    /* Hardware and software integer alpha blending can round differently. */
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(2, max_diff, message);
}

static void compare_scene(lv_vector_blend_t mode, bool layered)
{
    blend_mode = mode;
    lv_obj_t * panel = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(panel);
    lv_obj_set_pos(panel, 37, 43);
    lv_obj_set_size(panel, 119, 181);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x235fa1), 0);
    if(layered) lv_obj_set_style_opa_layered(panel, 173, 0);
    lv_obj_add_event_cb(panel, draw_scene, LV_EVENT_DRAW_MAIN, NULL);

    /* An independent pixel reference for these integer-aligned rectangles. */
    unsigned width = lv_display_get_horizontal_resolution(NULL);
    for(size_t i = 0; i < frame_size / 4; i++) {
        unsigned x = i % width, y = i / width;
        const uint8_t bg[3] = { 0x31, 0x57, 0x7b };
        uint8_t color[3] = { 0x23, 0x5f, 0xa1 };
        bool inside = x >= 37 && x <= 155 && y >= 43 && y <= 223;
        if(inside && x <= 94) { color[0] = 0x7b; color[1] = 0x39; color[2] = 0x5d; }
        if(inside && x >= 46 && x <= 147 && y >= 50 && y <= 217) {
            const uint8_t src[3] = { 0xc5, 0x71, 0x39 };
            for(unsigned c = 0; c < 3; c++) {
                if(mode == LV_VECTOR_BLEND_SCREEN) color[c] = 255 - ((255 - src[c]) * (255 - color[c]) + 127) / 255;
                else if(mode == LV_VECTOR_BLEND_MULTIPLY) color[c] = (src[c] * color[c] + 127) / 255;
                else if(mode == LV_VECTOR_BLEND_ADDITIVE) color[c] = LV_MIN(255, src[c] + color[c]);
                else color[c] = src[c];
            }
        }
        if(inside && x >= 65 && x <= 132 && y >= 124 && y <= 132) {
            color[0] = 0x29; color[1] = 0xaf; color[2] = 0x83;
        }
        for(unsigned c = 0; c < 3; c++) {
            reference[i * 4 + c] = !inside ? bg[c] : layered ? (color[c] * 173 + bg[c] * 82 + 127) / 255 : color[c];
        }
    }
    uint32_t downloads = lv_draw_eve5_get_stats()->downloads;
    capture(actual);
    if(mode == LV_VECTOR_BLEND_SRC_OVER) {
        TEST_ASSERT_EQUAL_UINT32(downloads, lv_draw_eve5_get_stats()->downloads);
    }
    assert_pixels();
}

void test_eve5_vector_screen_uses_backdrop(void)
{
    compare_scene(LV_VECTOR_BLEND_SCREEN, false);
}

void test_eve5_vector_multiply_uses_backdrop(void)
{
    compare_scene(LV_VECTOR_BLEND_MULTIPLY, false);
}

void test_eve5_vector_additive_uses_backdrop(void)
{
    compare_scene(LV_VECTOR_BLEND_ADDITIVE, false);
}

void test_eve5_vector_layer_preserves_alpha_and_origin(void)
{
    compare_scene(LV_VECTOR_BLEND_SCREEN, true);
}

void test_eve5_vector_source_over_needs_no_readback(void)
{
    compare_scene(LV_VECTOR_BLEND_SRC_OVER, false);
}

void test_eve5_vector_blends_with_existing_canvas(void)
{
    blend_mode = LV_VECTOR_BLEND_SCREEN;
    for(unsigned hardware = 0; hardware < 2; hardware++) {
        lv_draw_eve5_set_enabled(hardware != 0);
        canvas_buf = lv_draw_buf_create(97, 71, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
        TEST_ASSERT_NOT_NULL(canvas_buf);
        lv_obj_t * canvas = lv_canvas_create(lv_screen_active());
        lv_obj_set_pos(canvas, 29, 35);
        lv_canvas_set_draw_buf(canvas, canvas_buf);
        lv_canvas_fill_bg(canvas, lv_color_hex(0x375f97), 151);
        /* ThorVG reads and writes premultiplied pixels. Keep that contract on both backends. */
        TEST_ASSERT_EQUAL(LV_RESULT_OK, lv_draw_buf_premultiply(canvas_buf));

        lv_layer_t layer;
        lv_canvas_init_layer(canvas, &layer);
        lv_area_t area = { 11, 9, 83, 59 };
        vector_rect(&layer, &area, lv_color_hex(0xab73d1), 173);
        lv_canvas_finish_layer(canvas, &layer);
        TEST_ASSERT_TRUE(lv_draw_buf_ensure_resident(canvas_buf, NULL));
        uint8_t * pixels = hardware ? actual : reference;
        for(unsigned y = 0; y < 71; y++) {
            lv_memcpy(pixels + y * 97 * 4, canvas_buf->data + y * canvas_buf->header.stride, 97 * 4);
        }
        lv_obj_delete(canvas);
        lv_draw_buf_destroy(canvas_buf);
        canvas_buf = NULL;
    }
    frame_size = 97 * 71 * 4;
    assert_pixels();
}

void test_eve5_vector_empty_pattern_clip(void)
{
    uint32_t target[32 * 16];
    uint32_t pattern[32 * 128];
    lv_memzero(target, sizeof(target));
    lv_memset(pattern, 255, sizeof(pattern));
    lv_draw_buf_t buf;
    lv_draw_buf_init(&buf, 32, 16, LV_COLOR_FORMAT_ARGB8888, 32 * 4, target, sizeof(target));
    lv_layer_t layer;
    lv_layer_init(&layer);
    layer.draw_buf = &buf;
    layer.color_format = LV_COLOR_FORMAT_ARGB8888;
    layer.buf_area = (lv_area_t) { 20, 64, 51, 79 };
    layer._clip_area = layer.phy_clip_area = layer.buf_area;
    layer.partial_y_offset = 64;

    lv_image_dsc_t image = {
        .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888,
                    .w = 32, .h = 128, .stride = 32 * 4, .flags = LV_IMAGE_FLAGS_PREMULTIPLIED },
        .data_size = sizeof(pattern), .data = (const uint8_t *)pattern
    };
    lv_draw_image_dsc_t img_dsc;
    lv_draw_image_dsc_init(&img_dsc);
    img_dsc.src = &image;
    img_dsc.header = image.header;
    lv_draw_vector_dsc_t * dsc = lv_draw_vector_dsc_create(&layer);
    lv_draw_vector_dsc_set_fill_image(dsc, &img_dsc);
    lv_draw_vector_dsc_set_stroke_opa(dsc, LV_OPA_TRANSP);
    lv_vector_path_t * path = lv_vector_path_create(LV_VECTOR_PATH_QUALITY_MEDIUM);
    lv_fpoint_t center = { 36, 24 };
    lv_vector_path_append_circle(path, &center, 12, 12);
    lv_draw_vector_dsc_add_path(dsc, path);
    lv_draw_task_t task = { 0 };
    task.target_layer = &layer;
    task.clip_area = layer.buf_area;
    task.opa = LV_OPA_COVER;
    lv_draw_sw_vector(&task, dsc);
    lv_vector_path_delete(path);
    lv_draw_vector_dsc_delete(dsc);
    lv_image_cache_drop(&image);
    /* The tall pattern intersects this tile, but its circular clipping path is entirely above it. */
    for(unsigned i = 0; i < 32 * 16; i++) TEST_ASSERT_EQUAL_HEX32(0, target[i]);
}

static void render_pattern_tile(uint32_t * pixels, int32_t y, int32_t height, lv_image_dsc_t * image)
{
    lv_draw_buf_t buf;
    lv_draw_buf_init(&buf, 160, height, LV_COLOR_FORMAT_ARGB8888, 160 * 4, pixels, 160 * height * 4);
    lv_draw_buf_clear(&buf, NULL);
    lv_layer_t layer;
    lv_layer_init(&layer);
    layer.draw_buf = &buf;
    layer.color_format = LV_COLOR_FORMAT_ARGB8888;
    layer.buf_area = (lv_area_t) { 0, y, 159, y + height - 1 };
    layer._clip_area = layer.phy_clip_area = layer.buf_area;
    layer.partial_y_offset = y;

    lv_draw_image_dsc_t img_dsc;
    lv_draw_image_dsc_init(&img_dsc);
    img_dsc.src = image;
    img_dsc.header = image->header;
    lv_draw_vector_dsc_t * dsc = lv_draw_vector_dsc_create(&layer);
    lv_draw_vector_dsc_set_fill_image(dsc, &img_dsc);
    lv_draw_vector_dsc_set_stroke_opa(dsc, LV_OPA_TRANSP);
    lv_draw_vector_dsc_translate(dsc, 64, 64);
    lv_draw_vector_dsc_rotate(dsc, 25);
    lv_draw_vector_dsc_translate(dsc, -64, -64);
    lv_vector_path_t * path = lv_vector_path_create(LV_VECTOR_PATH_QUALITY_MEDIUM);
    lv_area_t area = { 16, 16, 111, 111 };
    lv_vector_path_append_rect(path, &area, 0, 0);
    lv_draw_vector_dsc_add_path(dsc, path);
    lv_draw_task_t task = { 0 };
    task.target_layer = &layer;
    task.clip_area = layer.buf_area;
    task.opa = LV_OPA_COVER;
    lv_draw_sw_vector(&task, dsc);
    lv_vector_path_delete(path);
    lv_draw_vector_dsc_delete(dsc);
}

void test_eve5_vector_tiled_image_keeps_sampling_phase(void)
{
    uint32_t pattern[96 * 96];
    for(unsigned y = 0; y < 96; y++) {
        for(unsigned x = 0; x < 96; x++) {
            pattern[y * 96 + x] = 0xff000000u | ((x * 13 % 256) << 16) | ((y * 7 % 256) << 8) | ((x + y) * 3 % 256);
        }
    }
    lv_image_dsc_t image = {
        .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888,
                    .w = 96, .h = 96, .stride = 96 * 4, .flags = LV_IMAGE_FLAGS_PREMULTIPLIED },
        .data_size = sizeof(pattern), .data = (const uint8_t *)pattern
    };
    render_pattern_tile((uint32_t *)reference, 0, 160, &image);
    for(int32_t y = 0; y < 160; y += 32) render_pattern_tile((uint32_t *)actual + y * 160, y, 32, &image);
    lv_image_cache_drop(&image);

    unsigned checked = 0;
    for(unsigned y = 2; y < 158; y++) {
        for(unsigned x = 2; x < 158; x++) {
            /* Compare the texture interior, excluding ThorVG's contour antialiasing. */
            bool interior = true;
            for(int dy = -2; dy <= 2; dy++) {
                for(int dx = -2; dx <= 2; dx++) {
                    if(reference[((y + dy) * 160 + x + dx) * 4 + 3] != 255) interior = false;
                }
            }
            if(!interior) continue;
            checked++;
            for(unsigned c = 0; c < 4; c++) {
                size_t i = (y * 160 + x) * 4 + c;
                TEST_ASSERT_UINT8_WITHIN_MESSAGE(2, reference[i], actual[i], "Tile clipping changed texture sampling");
            }
        }
    }
    TEST_ASSERT_GREATER_THAN_UINT(4000, checked);
}
#endif
