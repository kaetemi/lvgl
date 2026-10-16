#if LV_BUILD_TEST
#include "../lvgl.h"
#include "../../lvgl_private.h"
#include "unity/unity.h"

static lv_draw_buf_malloc_cb_t original_malloc;
static unsigned allocations;
static unsigned bitmap_calls;
static unsigned glyph_calls;
static bool expect_deferred;
static bool need_pixels;
static lv_draw_glyph_dsc_t letter_dsc;

static void * count_malloc(size_t size, lv_color_format_t cf)
{
    allocations++;
    return original_malloc(size, cf);
}

static bool get_glyph(const lv_font_t * font, lv_font_glyph_dsc_t * g, uint32_t letter, uint32_t next)
{
    LV_UNUSED(font);
    LV_UNUSED(next);
    if(letter != 'i' && letter != 'W') return false;
    g->gid.index = letter;
    g->box_w = letter == 'i' ? 3 : 29;
    g->box_h = letter == 'i' ? 8 : 24;
    g->adv_w = g->box_w + 2;
    g->format = LV_FONT_GLYPH_FORMAT_A8;
    return true;
}

static bool missing_glyph(const lv_font_t * font, lv_font_glyph_dsc_t * g, uint32_t letter, uint32_t next)
{
    LV_UNUSED(font);
    LV_UNUSED(g);
    LV_UNUSED(letter);
    LV_UNUSED(next);
    return false;
}

static LV_IMAGE_DSC_CONST void * get_bitmap(lv_font_glyph_dsc_t * g, lv_draw_buf_t * buf)
{
    bitmap_calls++;
    TEST_ASSERT_NOT_NULL(buf);
    TEST_ASSERT_NOT_NULL(buf->data);
    TEST_ASSERT_EQUAL(g->box_w, buf->header.w);
    TEST_ASSERT_EQUAL(g->box_h, buf->header.h);
    for(unsigned y = 0; y < g->box_h; y++) {
        lv_memset(buf->data + y * buf->header.stride, 255, g->box_w);
    }
    return buf;
}

/* Optional font-type probes also inspect the bitmap-font descriptor header. */
static lv_font_fmt_txt_dsc_t font_data;
static const lv_font_t font = {
    .get_glyph_dsc = get_glyph, .get_glyph_bitmap = get_bitmap,
    .line_height = 24, .dsc = &font_data,
};
static lv_font_fmt_txt_dsc_t fallback_data;
static const lv_font_t fallback_font = {
    .get_glyph_dsc = missing_glyph, .get_glyph_bitmap = get_bitmap,
    .line_height = 24, .dsc = &fallback_data, .fallback = &font,
};

void setUp(void)
{
    lv_refr_now(NULL);
    original_malloc = lv_draw_buf_get_font_handlers()->buf_malloc_cb;
    lv_draw_buf_get_font_handlers()->buf_malloc_cb = count_malloc;
    allocations = bitmap_calls = glyph_calls = 0;
    lv_draw_glyph_dsc_init(&letter_dsc);
}

void tearDown(void)
{
    lv_draw_buf_get_font_handlers()->buf_malloc_cb = original_malloc;
    if(letter_dsc._draw_buf) lv_draw_buf_destroy(letter_dsc._draw_buf);
    lv_obj_clean(lv_screen_active());
}

static void glyph_cb(lv_draw_task_t * task, lv_draw_glyph_dsc_t * glyph,
                     lv_draw_fill_dsc_t * fill, const lv_area_t * area)
{
    LV_UNUSED(task);
    LV_UNUSED(fill);
    LV_UNUSED(area);
    if(glyph == NULL) return;
    glyph_calls++;
    TEST_ASSERT_EQUAL_PTR(&font, glyph->g->resolved_font);
    TEST_ASSERT_EQUAL(expect_deferred, glyph->_draw_buf_deferred);
    if(!need_pixels) {
        TEST_ASSERT_NULL(glyph->_draw_buf);
        return;
    }
    if(!expect_deferred) TEST_ASSERT_NOT_NULL(glyph->_draw_buf);
    lv_draw_buf_t * buf = lv_draw_glyph_dsc_get_draw_buf(glyph);
    TEST_ASSERT_EQUAL_PTR(buf, lv_font_get_glyph_bitmap(glyph->g, buf));
}

static void iterate(bool deferred, bool pixels)
{
    expect_deferred = deferred;
    need_pixels = pixels;
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = &fallback_font;
    dsc.text = "iWi"; /* A larger miss grows the buffer; the final glyph reshapes it. */
    lv_draw_task_t task = {0};
    task.draw_dsc = &dsc;
    task.clip_area = (lv_area_t) {0, 0, 199, 39};
    if(deferred) lv_draw_label_iterate_characters_internal(&task, &dsc, &task.clip_area, glyph_cb, true);
    else lv_draw_label_iterate_characters(&task, &dsc, &task.clip_area, glyph_cb);
    TEST_ASSERT_EQUAL_UINT(3, glyph_calls);
}

void test_default_iterator_supplies_cpu_buffer(void)
{
    iterate(false, true);
    TEST_ASSERT_GREATER_THAN_UINT(0, allocations);
    TEST_ASSERT_EQUAL_UINT(3, bitmap_calls);
}

void test_deferred_iterator_skips_unused_cpu_buffer(void)
{
    iterate(true, false);
    TEST_ASSERT_EQUAL_UINT(0, allocations);
    TEST_ASSERT_EQUAL_UINT(0, bitmap_calls);
}

void test_deferred_iterator_prepares_fallback_bitmap_on_demand(void)
{
    iterate(true, true);
    TEST_ASSERT_GREATER_THAN_UINT(0, allocations);
    TEST_ASSERT_EQUAL_UINT(3, bitmap_calls);
}

void test_deferred_letter_prepares_buffer_after_cached_glyph(void)
{
    lv_draw_task_t task = {0};
    task.clip_area = (lv_area_t) {0, 0, 199, 39};
    lv_point_t pos = {0, 0};
    letter_dsc._draw_buf_deferred = true;
    expect_deferred = true;
    need_pixels = false;
    lv_draw_unit_draw_letter(&task, &letter_dsc, &pos, &fallback_font, 'i', glyph_cb);
    TEST_ASSERT_EQUAL_UINT(0, allocations);
    need_pixels = true;
    lv_draw_unit_draw_letter(&task, &letter_dsc, &pos, &fallback_font, 'W', glyph_cb);
    TEST_ASSERT_EQUAL_UINT(2, glyph_calls);
    TEST_ASSERT_EQUAL_UINT(1, bitmap_calls);
    TEST_ASSERT_NOT_NULL(letter_dsc._draw_buf);
}

#if LV_USE_DRAW_EVE5
void test_eve_resident_generic_and_fallback_labels_need_no_cpu_buffer(void)
{
    lv_obj_t * label = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(label, &font, 0);
    lv_label_set_text(label, "iWi");
    lv_obj_t * fallback = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(fallback, &fallback_font, 0);
    lv_obj_set_y(fallback, 40);
    lv_obj_set_style_opa_layered(fallback, 200, 0);
    lv_label_set_text(fallback, "WiW");
    lv_refr_now(NULL);
    TEST_ASSERT_GREATER_THAN_UINT(0, bitmap_calls);
    TEST_ASSERT_GREATER_THAN_UINT(0, allocations);
    bitmap_calls = allocations = 0;
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    TEST_ASSERT_EQUAL_UINT(0, bitmap_calls);
    TEST_ASSERT_EQUAL_UINT(0, allocations);
#if LV_USE_DRAW_VRAM
    /* Losing residency must restore the CPU decode path on the next redraw. */
    lv_font_release_vram(&font);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    TEST_ASSERT_GREATER_THAN_UINT(0, bitmap_calls);
    TEST_ASSERT_GREATER_THAN_UINT(0, allocations);
#endif
}
#endif
#endif
