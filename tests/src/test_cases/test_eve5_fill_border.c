#if LV_BUILD_TEST
#include "../lvgl.h"
#include "unity/unity.h"
#include "src/drivers/display/eve5/lv_eve5.h"
#include "src/draw/eve5/lv_draw_eve5_private.h"

static lv_draw_eve5_unit_t * unit;
static EVE_GpuHandle target;
static uint8_t * pixels;

void setUp(void)
{
    unit = lv_malloc_zeroed(sizeof(*unit));
    TEST_ASSERT_NOT_NULL(unit);
    unit->hal = lv_eve5_get_hal(lv_display_get_default());
    unit->allocator = lv_eve5_get_allocator(lv_display_get_default());
    target = GA_HANDLE_INVALID;
    pixels = NULL;
    TEST_ASSERT_TRUE(EVE_Cmd_waitFlush(unit->hal));
}

void tearDown(void)
{
    lv_draw_eve5_dl_start(unit);
    TEST_ASSERT_TRUE(EVE_Cmd_waitFlush(unit->hal));
    EVE_GpuAlloc_Free(unit->allocator, target);
    lv_free(pixels);
    lv_free(unit);
}

/* Reference square borders use disjoint strips, so even opacity 253/254 is
 * blended once at corners. The general border path treats those opacities as
 * opaque and can blend overlapping side rectangles twice. */
static void draw_reference_border(lv_draw_task_t * t)
{
    const lv_draw_border_dsc_t * border = t->draw_dsc;
    if(border->radius) {
        lv_draw_eve5_hal_draw_border(unit, t, false);
        return;
    }
    lv_draw_fill_dsc_t fill;
    lv_draw_fill_dsc_init(&fill);
    fill.color = border->color;
    fill.opa = border->opa;
    lv_draw_task_t strip = *t;
    strip.draw_dsc = &fill;
    lv_area_t remaining = t->area;
    strip.area = remaining;
    strip.area.y2 = LV_MIN(remaining.y1 + border->width - 1, remaining.y2);
    lv_draw_eve5_hal_draw_fill(unit, &strip, false);
    remaining.y1 = strip.area.y2 + 1;
    if(remaining.y1 > remaining.y2) return;
    strip.area = remaining;
    strip.area.y1 = LV_MAX(remaining.y2 - border->width + 1, remaining.y1);
    lv_draw_eve5_hal_draw_fill(unit, &strip, false);
    remaining.y2 = strip.area.y1 - 1;
    if(remaining.y1 > remaining.y2) return;
    strip.area = remaining;
    strip.area.x2 = LV_MIN(remaining.x1 + border->width - 1, remaining.x2);
    lv_draw_eve5_hal_draw_fill(unit, &strip, false);
    remaining.x1 = strip.area.x2 + 1;
    if(remaining.x1 > remaining.x2) return;
    strip.area = remaining;
    strip.area.x1 = LV_MAX(remaining.x2 - border->width + 1, remaining.x1);
    lv_draw_eve5_hal_draw_fill(unit, &strip, false);
}

/* Compare the combined draw against independent background and border draws
 * over a nonuniform background. Also paint after the pair: a leaked stencil
 * test or scissor would change that overlay. */
static void compare_pair(uint8_t fill_opa, uint8_t border_opa, int32_t width,
                         lv_area_t area, lv_area_t clip, int32_t radius)
{
    EVE_HalContext * phost = unit->hal;
    uint32_t dst = GA_INVALID;
#ifdef EVE_SUPPORT_RENDERTARGET
    if(EVE_Hal_supportRenderTarget(phost)) {
        target = EVE_GpuAlloc_Alloc(unit->allocator, 128 * 64 * 3, GA_ALIGN_128);
        dst = EVE_GpuAlloc_Get(unit->allocator, target);
        TEST_ASSERT_NOT_EQUAL(GA_INVALID, dst);
        EVE_CoCmd_renderTarget(phost, dst, RGB8, 128, 64);
    }
#endif
    lv_draw_eve5_dl_start(unit);
    EVE_CoDl_clearColorRgb(phost, 31, 79, 137);
    /* Nonzero stencil at entry ensures the pair really clears its region. */
    EVE_CoDl_clearStencil(phost, 173);
    EVE_CoDl_clear(phost, 1, 1, 1);
    EVE_CoDl_clearStencil(phost, 0);
    EVE_CoDl_blendFunc(phost, SRC_ALPHA, ONE_MINUS_SRC_ALPHA);

    lv_layer_t layer = { 0 };
    /* Nonzero layer origin exercises the screen-to-layer translation. */
    layer.buf_area = (lv_area_t) { 100, 200, 227, 263 };
    for(unsigned side = 0; side < 2; side++) {
        lv_draw_fill_dsc_t fill;
        lv_draw_border_dsc_t border;
        lv_draw_fill_dsc_init(&fill);
        lv_draw_border_dsc_init(&border);
        fill.color = lv_color_hex(0xe7b559);
        fill.opa = fill_opa;
        fill.radius = radius;
        border.color = lv_color_hex(0x3ba5d1);
        border.opa = border_opa;
        border.width = width;
        border.side = LV_BORDER_SIDE_FULL;
        border.radius = radius;
        lv_draw_task_t f = { 0 }, b = { 0 };
        f.target_layer = b.target_layer = &layer;
        f.draw_dsc = &fill;
        b.draw_dsc = &border;
        f.area = area;
        f.clip_area = clip;
        lv_area_move(&f.area, 100 + side * 64, 200);
        lv_area_move(&f.clip_area, 100 + side * 64, 200);
        b.area = f.area;
        b.clip_area = f.clip_area;

        lv_area_t backdrop = { 100 + side * 64, 200, 131 + side * 64, 263 };
        lv_draw_fill_dsc_t bg;
        lv_draw_fill_dsc_init(&bg);
        bg.color = lv_color_hex(0x912b53);
        lv_draw_task_t background = f;
        background.draw_dsc = &bg;
        background.area = backdrop;
        background.clip_area = layer.buf_area;
        lv_draw_eve5_hal_draw_fill(unit, &background, false);

        if(side == 0) lv_draw_eve5_hal_draw_fill_with_border(unit, &f, &b);
        else {
            lv_draw_eve5_hal_draw_fill(unit, &f, false);
            draw_reference_border(&b);
        }

        /* This crosses the inner, border and untouched regions. */
        background.area = (lv_area_t) { 100 + side * 64, 219, 163 + side * 64, 220 };
        bg.color = lv_color_hex(0x79c543);
        bg.opa = 91;
        lv_draw_eve5_hal_draw_fill(unit, &background, false);
    }
    EVE_CoDl_display(phost);
    EVE_CoCmd_swap(phost);
#ifdef EVE_SUPPORT_RENDERTARGET
    if(EVE_Hal_supportRenderTarget(phost)) EVE_CoCmd_graphicsFinish(phost);
#endif
    TEST_ASSERT_TRUE(EVE_Cmd_waitFlush(phost));
    unsigned stride = dst == GA_INVALID ? phost->Width * 4 : 128 * 3;
    unsigned bpp = dst == GA_INVALID ? 4 : 3;
    pixels = lv_malloc(stride * (dst == GA_INVALID ? phost->Height : 64));
    TEST_ASSERT_NOT_NULL(pixels);
    if(dst == GA_INVALID) TEST_ASSERT_TRUE(lv_eve5_read_screen(lv_display_get_default(), pixels, stride));
    else EVE_Hal_rdMem(phost, pixels, dst, stride * 64);
    for(unsigned y = 0; y < 64; y++) {
        for(unsigned x = 0; x < 64; x++) {
            const uint8_t * left = pixels + y * stride + x * bpp;
            const uint8_t * right = left + 64 * bpp;
            TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(right, left, 3, "Combined fill/border changed RGB pixels");
        }
    }
}

void test_eve5_fill_border_translucent_fill(void)
{
    compare_pair(51, 255, 1, (lv_area_t) { 8, 8, 47, 47 }, (lv_area_t) { 0, 0, 63, 63 }, 0);
}

void test_eve5_fill_border_translucent_border(void)
{
    compare_pair(129, 117, 4, (lv_area_t) { 8, 8, 47, 47 }, (lv_area_t) { 0, 0, 63, 63 }, 0);
}

void test_eve5_fill_border_almost_opaque_border(void)
{
    compare_pair(51, 254, 2, (lv_area_t) { 8, 8, 47, 47 }, (lv_area_t) { 0, 0, 63, 63 }, 0);
}

void test_eve5_fill_border_clipped(void)
{
    compare_pair(51, 255, 3, (lv_area_t) { 8, 8, 47, 47 }, (lv_area_t) { 22, 6, 50, 27 }, 0);
}

void test_eve5_fill_border_clipped_to_inner(void)
{
    compare_pair(129, 117, 4, (lv_area_t) { 8, 8, 47, 47 }, (lv_area_t) { 20, 20, 25, 25 }, 0);
}

void test_eve5_fill_border_clipped_to_border(void)
{
    compare_pair(129, 117, 4, (lv_area_t) { 8, 8, 47, 47 }, (lv_area_t) { 8, 8, 10, 47 }, 0);
}

void test_eve5_fill_border_empty_inner(void)
{
    compare_pair(129, 117, 12, (lv_area_t) { 8, 8, 27, 27 }, (lv_area_t) { 0, 0, 63, 63 }, 0);
}

void test_eve5_fill_border_single_pixel(void)
{
    compare_pair(3, 117, 1, (lv_area_t) { 8, 8, 8, 8 }, (lv_area_t) { 0, 0, 63, 63 }, 0);
}

void test_eve5_fill_border_outside_clip(void)
{
    compare_pair(51, 255, 1, (lv_area_t) { 8, 8, 27, 27 }, (lv_area_t) { 40, 40, 63, 63 }, 0);
}

void test_eve5_fill_border_large_coordinates(void)
{
    compare_pair(51, 255, 1, (lv_area_t) { -100000, -100000, 47, 47 }, (lv_area_t) { 0, 0, 63, 63 }, 0);
}

void test_eve5_fill_border_rounded_fallback(void)
{
    compare_pair(51, 117, 3, (lv_area_t) { 8, 8, 47, 47 }, (lv_area_t) { 0, 0, 63, 63 }, 8);
}
#endif
