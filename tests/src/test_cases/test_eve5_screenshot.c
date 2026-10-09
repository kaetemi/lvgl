#if LV_BUILD_TEST
#include "../lvgl.h"
#include "src/display/lv_display_private.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "unity/unity.h"
#include "src/drivers/display/eve5/lv_eve5.h"
#include "src/draw/eve5/lv_draw_eve5_private.h"
#include "../lv_test_eve5.h"

static lv_draw_buf_t * image;
static uint8_t * frame;
static EVE_GpuHandle pressure;
static unsigned renders;

static void count_render(lv_event_t * e)
{
    LV_UNUSED(e);
    renders++;
}

static void invalidate_again(lv_event_t * e)
{
    lv_eve5_request_invalidate(lv_event_get_target(e));
}

static void fail_with_temporary_allocations(lv_event_t * e)
{
    EVE_GpuAlloc * ga = lv_eve5_get_allocator(lv_event_get_target(e));
    EVE_GpuAlloc_Alloc(ga, 64, GA_GC_FLAG);
    EVE_GpuAlloc_Alloc(ga, EVE_GpuAlloc_GetTotal(ga) + 4, 0);
}

void setUp(void)
{
    lv_display_t * disp = lv_display_get_default();
    pressure = GA_HANDLE_INVALID;
    lv_obj_clean(lv_screen_active());
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0xff0000), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(lv_screen_active(), 0, 0);
    lv_refr_now(disp);
    renders = 0;
    lv_display_add_event_cb(disp, count_render, LV_EVENT_RENDER_START, NULL);
    image = lv_draw_buf_create(64, 64, LV_COLOR_FORMAT_RGB565, 128);
    TEST_ASSERT_NOT_NULL(image);
#if LV_USE_DRAW_VRAM
    TEST_ASSERT_TRUE(lv_draw_buf_ensure_resident(image, NULL));
#endif
    uint16_t * pixels = (uint16_t *)image->data;
    for(unsigned i = 0; i < 64 * 64; i++) pixels[i] = 0x07e0;
    frame = lv_malloc((size_t)lv_display_get_horizontal_resolution(disp) *
                      lv_display_get_vertical_resolution(disp) * 4);
    TEST_ASSERT_NOT_NULL(frame);
}

void tearDown(void)
{
    lv_display_t * disp = lv_display_get_default();
    lv_display_remove_event_cb_with_user_data(disp, invalidate_again, NULL);
    lv_display_remove_event_cb_with_user_data(disp, count_render, NULL);
    lv_display_remove_event_cb_with_user_data(disp, fail_with_temporary_allocations, NULL);
    lv_obj_clean(lv_screen_active());
    lv_image_cache_drop(image);
    lv_draw_buf_destroy(image);
    /* Retire any texture or pressure block still referenced by the last DL. */
    lv_refr_now(disp);
    EVE_GpuAlloc_Free(lv_eve5_get_allocator(disp), pressure);
    lv_free(frame);
}

static void occupy_previous_frame(bool reclaimable)
{
    lv_display_t * disp = lv_display_get_default();
    if(EVE_Hal_supportRenderTarget(lv_eve5_get_hal(disp))) TEST_IGNORE_MESSAGE("NORT allocator lifecycle");
#if !LV_USE_DRAW_VRAM
    TEST_IGNORE_MESSAGE("Source residency before NORT rendering requires VRAM support");
#endif
    EVE_GpuAlloc * ga = lv_eve5_get_allocator(disp);
    uint32_t available = EVE_GpuAlloc_GetTotal(ga) - ga->TotalReserved - EVE_GpuAlloc_GetTotalUsed(ga);
    TEST_ASSERT_GREATER_THAN(8192, available);
    pressure = EVE_GpuAlloc_Alloc(ga, available - 4096, reclaimable ? GA_GC_FLAG : 0);
    TEST_ASSERT_NOT_EQUAL(GA_INVALID, EVE_GpuAlloc_Get(ga, pressure));
    /* The first swap retains this used allocation. Only the next swap may
     * reclaim it, just like an asset from the previous scanout. */
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(disp);
    lv_obj_t * obj = lv_image_create(lv_screen_active());
    lv_image_set_src(obj, image);
    lv_obj_set_pos(obj, 0, 0);
    renders = 0;
}

static void assert_pixel(uint8_t red, uint8_t green)
{
    uint32_t w = lv_display_get_horizontal_resolution(lv_display_get_default());
    uint8_t * p = frame + (20 * w + 20) * 4;
    TEST_ASSERT_EQUAL_UINT8(red, p[0]);
    TEST_ASSERT_EQUAL_UINT8(green, p[1]);
    TEST_ASSERT_EQUAL_UINT8(0, p[2]);
}

void test_eve5_screenshot_retries_after_scanout_reclaims_memory(void)
{
    occupy_previous_frame(true);
    lv_display_t * disp = lv_display_get_default();
    EVE_GpuAlloc * ga = lv_eve5_get_allocator(disp);
    uint32_t tick = lv_tick_get();
    lv_refr_now(disp);
    TEST_ASSERT_GREATER_THAN(0, EVE_GpuAlloc_GetAllocFailedCount(ga));
    TEST_ASSERT_GREATER_THAN(0, disp->inv_p);
    TEST_ASSERT_TRUE(lv_eve5_read_screen(disp, frame, lv_display_get_horizontal_resolution(disp) * 4));
    assert_pixel(255, 0); /* The transition frame really is missing the image. */
    TEST_ASSERT_NOT_EQUAL(LV_TEST_SCREENSHOT_RESULT_FAILED,
                         lv_test_screenshot_compare_core("eve5/allocator_recovered.png"));
    TEST_ASSERT_TRUE(lv_test_screenshot_read(frame));
    assert_pixel(0, 255);
    TEST_ASSERT_EQUAL_UINT(2, renders);
    TEST_ASSERT_EQUAL_UINT32(0, EVE_GpuAlloc_GetAllocFailedCount(ga));
    TEST_ASSERT_EQUAL_UINT32(0, disp->inv_p);
    TEST_ASSERT_EQUAL_UINT32(tick, lv_tick_get());
}

void test_eve5_screenshot_retains_unrecoverable_allocation_failure(void)
{
    occupy_previous_frame(false);
    lv_display_t * disp = lv_display_get_default();
    EVE_GpuAlloc * ga = lv_eve5_get_allocator(disp);
    lv_refr_now(disp);
    uint32_t failures = EVE_GpuAlloc_GetAllocFailedCount(ga);
    TEST_ASSERT_GREATER_THAN(0, failures);
    TEST_ASSERT_EQUAL_UINT32(0, disp->inv_p);
    lv_refr_now(disp); /* An empty refresh must not erase the diagnosis. */
    TEST_ASSERT_TRUE(lv_test_screenshot_read(frame));
    assert_pixel(255, 0);
    TEST_ASSERT_EQUAL_UINT(1, renders);
    TEST_ASSERT_EQUAL_UINT32(failures, EVE_GpuAlloc_GetAllocFailedCount(ga));
    TEST_ASSERT_NOT_EQUAL(LV_TEST_SCREENSHOT_RESULT_FAILED,
                         lv_test_screenshot_compare_core("eve5/allocator_insufficient.png"));
}

void test_eve5_screenshot_bounds_continuous_redraw_requests(void)
{
    lv_display_t * disp = lv_display_get_default();
    lv_display_add_event_cb(disp, invalidate_again, LV_EVENT_RENDER_START, NULL);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(disp);
    renders = 0;
    uint32_t tick = lv_tick_get();
    TEST_ASSERT_FALSE(lv_test_screenshot_read(frame));
    TEST_ASSERT_EQUAL_UINT(8, renders);
    TEST_ASSERT_EQUAL_UINT32(tick, lv_tick_get());
}

void test_eve5_allocator_retry_stops_when_gc_cannot_make_progress(void)
{
    lv_display_t * disp = lv_display_get_default();
    if(EVE_Hal_supportRenderTarget(lv_eve5_get_hal(disp))) TEST_IGNORE_MESSAGE("NORT allocator lifecycle");
    EVE_GpuAlloc * ga = lv_eve5_get_allocator(disp);
    pressure = EVE_GpuAlloc_Alloc(ga, 64, GA_GC_FLAG);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(disp);
    renders = 0;
    lv_display_add_event_cb(disp, fail_with_temporary_allocations, LV_EVENT_RENDER_START, NULL);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(disp);
    TEST_ASSERT_TRUE(lv_eve5_has_pending_refresh(disp));
    TEST_ASSERT_TRUE(lv_test_screenshot_read(frame));
    TEST_ASSERT_FALSE(lv_eve5_has_pending_refresh(disp));
    TEST_ASSERT_EQUAL_UINT(2, renders);
    TEST_ASSERT_EQUAL_UINT32(1, EVE_GpuAlloc_GetAllocFailedCount(ga));

    /* An application update starts a new retry sequence even if the old
     * scene's recovery invalidation has not rendered yet. */
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(disp);
    TEST_ASSERT_TRUE(lv_eve5_has_pending_refresh(disp));
    lv_obj_invalidate(lv_screen_active());
    TEST_ASSERT_TRUE(lv_test_screenshot_read(frame));
    TEST_ASSERT_FALSE(lv_eve5_has_pending_refresh(disp));
    TEST_ASSERT_EQUAL_UINT(5, renders);
}

void test_eve5_layer_refusals_count_only_unsupported_targets(void)
{
    lv_display_t * disp = lv_display_get_default();
    lv_draw_eve5_reset_stats();
    lv_test_eve5_case_begin();
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(disp);
    TEST_ASSERT_EQUAL_UINT32(0, lv_draw_eve5_get_stats()->layer_tasks_refused);

    uint32_t fallbacks_before = lv_draw_get_layer_fallback_count(disp);
    lv_obj_t * panel = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 100, 60);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_clip_corner(panel, true, 0);
    lv_obj_t * label = lv_label_create(panel);
    lv_label_set_text(label, "Layer\nclipping\nexample");
    lv_refr_now(disp);
    uint32_t refused = lv_draw_eve5_get_stats()->layer_tasks_refused;
    if(EVE_Hal_supportRenderTarget(lv_eve5_get_hal(disp)) || !lv_draw_layer_supported(disp)) {
        TEST_ASSERT_EQUAL_UINT32(0, refused);
    }
    else {
        TEST_ASSERT_GREATER_THAN_UINT32(0, refused);
    }
    uint32_t fallbacks = lv_draw_get_layer_fallback_count(disp);
    if(lv_draw_layer_supported(disp)) TEST_ASSERT_EQUAL_UINT32(fallbacks_before, fallbacks);
    else TEST_ASSERT_GREATER_THAN_UINT32(fallbacks_before, fallbacks);

    /* Empty refreshes and repeated captures must retain the displayed frame's
     * count without introducing more refusals. */
    lv_refr_now(disp);
    TEST_ASSERT_TRUE(lv_test_screenshot_read(frame));
    TEST_ASSERT_TRUE(lv_test_screenshot_read(frame));
    TEST_ASSERT_EQUAL_UINT32(refused, lv_draw_eve5_get_stats()->layer_tasks_refused);
    TEST_ASSERT_EQUAL_UINT32(fallbacks, lv_draw_get_layer_fallback_count(disp));

    lv_obj_set_style_clip_corner(panel, false, 0);
    lv_refr_now(disp);
    TEST_ASSERT_EQUAL_UINT32(refused, lv_draw_eve5_get_stats()->layer_tasks_refused);
    TEST_ASSERT_EQUAL_UINT32(fallbacks, lv_draw_get_layer_fallback_count(disp));
    lv_draw_eve5_reset_stats();
    lv_test_eve5_case_begin();
    TEST_ASSERT_EQUAL_UINT32(0, lv_draw_eve5_get_stats()->layer_tasks_refused);
    lv_obj_invalidate(panel);
    lv_refr_now(disp);
    TEST_ASSERT_TRUE(lv_test_screenshot_read(frame));
    TEST_ASSERT_EQUAL_UINT32(0, lv_draw_eve5_get_stats()->layer_tasks_refused);
}

void test_eve5_layer_refusals_retain_offscreen_canvas_drawing(void)
{
    lv_display_t * disp = lv_display_get_default();
    lv_draw_eve5_reset_stats();
    lv_test_eve5_case_begin();
    lv_obj_t * canvas = lv_canvas_create(lv_screen_active());
    lv_canvas_set_draw_buf(canvas, image);
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    lv_draw_fill_dsc_t fill;
    lv_draw_fill_dsc_init(&fill);
    lv_area_t area = {0, 0, 31, 31};
    lv_draw_fill(&layer, &fill, &area);
    lv_canvas_finish_layer(canvas, &layer);
    uint32_t refused = lv_draw_eve5_get_stats()->layer_tasks_refused;
    TEST_ASSERT_EQUAL_UINT32(EVE_Hal_supportRenderTarget(lv_eve5_get_hal(disp)) ? 0 : 1, refused);

    /* Rendering the canvas bitmap cannot re-issue its missing fill task.
     * Its off-screen diagnostic must survive multiple screen refreshes. */
    for(unsigned i = 0; i < 2; i++) {
        lv_obj_invalidate(canvas);
        lv_refr_now(disp);
        TEST_ASSERT_TRUE(lv_test_screenshot_read(frame));
        TEST_ASSERT_EQUAL_UINT32(refused, lv_draw_eve5_get_stats()->layer_tasks_refused);
    }
    lv_obj_delete(canvas);
    lv_test_eve5_case_begin();
    lv_refr_now(disp);
    TEST_ASSERT_TRUE(lv_test_screenshot_read(frame));
}

void test_eve5_degradation_stats_follow_rendered_frames(void)
{
    lv_display_t * disp = lv_display_get_default();
    if(EVE_Hal_supportRenderTarget(lv_eve5_get_hal(disp))) TEST_IGNORE_MESSAGE("NORT degradation policy");
    static lv_point_precise_t points[71];
    for(unsigned i = 0; i < 71; i++) points[i] = (lv_point_precise_t) {i * 10, i & 1 ? 80 : 40};
    lv_draw_eve5_reset_stats();
    lv_test_eve5_case_begin();
    lv_obj_t * line = lv_line_create(lv_screen_active());
    lv_line_set_points(line, points, 71);
    lv_obj_set_style_line_width(line, 3, 0);
    lv_obj_set_style_line_dash_width(line, 4, 0);
    lv_obj_set_style_line_dash_gap(line, 3, 0);
    lv_refr_now(disp);
    uint32_t degraded = lv_draw_eve5_get_stats()->degraded_tasks[0];
#if LV_DRAW_EVE5_NORT_DEGRADE
    TEST_ASSERT_GREATER_THAN_UINT32(0, degraded);
#else
    TEST_ASSERT_EQUAL_UINT32(0, degraded);
#endif
    TEST_ASSERT_EQUAL_UINT32(0, lv_draw_eve5_get_stats()->degraded_tasks[1]);
    TEST_ASSERT_TRUE(lv_test_screenshot_read(frame));
    lv_refr_now(disp); /* Empty refresh: preserve displayed-frame diagnostics. */
    TEST_ASSERT_TRUE(lv_test_screenshot_read(frame));
    TEST_ASSERT_EQUAL_UINT32(degraded, lv_draw_eve5_get_stats()->degraded_tasks[0]);
    lv_obj_delete(line);
    lv_refr_now(disp);
    TEST_ASSERT_TRUE(lv_test_screenshot_read(frame));
    TEST_ASSERT_EQUAL_UINT32(degraded, lv_draw_eve5_get_stats()->degraded_tasks[0]);
    lv_draw_eve5_reset_stats();
    TEST_ASSERT_EQUAL_UINT32(0, lv_draw_eve5_get_stats()->degraded_tasks[0]);
    TEST_ASSERT_EQUAL_UINT32(0, lv_draw_eve5_get_stats()->degraded_tasks[1]);
}
#endif
