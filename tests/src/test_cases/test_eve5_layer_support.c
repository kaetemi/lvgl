#if LV_BUILD_TEST
#include "../lvgl.h"
#include "unity/unity.h"
#include "src/core/lv_global.h"
#include "src/core/lv_obj_draw_private.h"
#include "src/display/lv_display_private.h"
#include "src/draw/lv_draw_private.h"
#include "src/drivers/display/eve5/lv_eve5.h"
#include "src/draw/eve5/lv_draw_eve5.h"

static lv_draw_unit_t * saved_units;
static lv_draw_unit_t mock_unit;
static lv_draw_unit_t * created_unit;
static uint32_t saved_unit_count;
static lv_display_t * other_display;
static lv_display_t * accepted_display;
static unsigned probe_calls;
static uint8_t * reference;
static uint8_t * frame;
static size_t frame_size;

static int32_t probe_evaluate(lv_draw_unit_t * unit, lv_draw_task_t * task)
{
    LV_UNUSED(unit);
    probe_calls++;
    TEST_ASSERT_TRUE(task->is_probe);
    TEST_ASSERT_EQUAL(LV_DRAW_TASK_TYPE_FILL, task->type);
    TEST_ASSERT_EQUAL(LV_DRAW_TASK_STATE_WAITING, task->state);
    TEST_ASSERT_EQUAL(LV_COLOR_FORMAT_ARGB8888, task->target_layer->color_format);
    TEST_ASSERT_NULL(task->target_layer->draw_buf);
    TEST_ASSERT_NULL(task->target_layer->draw_task_head);
    TEST_ASSERT_NULL(task->target_layer->next);
    TEST_ASSERT_EQUAL_PTR(task->target_layer->display->layer_head, task->target_layer->parent);
    TEST_ASSERT_EQUAL_PTR(task->target_layer, ((lv_draw_fill_dsc_t *)task->draw_dsc)->base.layer);
    TEST_ASSERT_EQUAL(LV_OPA_COVER, ((lv_draw_fill_dsc_t *)task->draw_dsc)->opa);
    TEST_ASSERT_GREATER_THAN(0, lv_area_get_width(&task->area));
    TEST_ASSERT_EQUAL_MEMORY(&task->area, &task->clip_area, sizeof(task->area));
    if(task->target_layer->display == accepted_display) task->preferred_draw_unit_id = 123;
    /* Like EVE, claim by ID despite returning zero. */
    return 0;
}

void setUp(void)
{
    saved_units = LV_GLOBAL_DEFAULT()->draw_info.unit_head;
    saved_unit_count = LV_GLOBAL_DEFAULT()->draw_info.unit_cnt;
    created_unit = NULL;
    other_display = NULL;
    accepted_display = NULL;
    probe_calls = 0;
    lv_memzero(&mock_unit, sizeof(mock_unit));
    mock_unit.evaluate_cb = probe_evaluate;
    lv_obj_t * screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xffffff), 0);
    frame_size = (size_t)lv_display_get_horizontal_resolution(NULL) * lv_display_get_vertical_resolution(NULL) * 4;
    reference = lv_malloc(frame_size);
    frame = lv_malloc(frame_size);
    TEST_ASSERT_NOT_NULL(reference);
    TEST_ASSERT_NOT_NULL(frame);
}

void tearDown(void)
{
    LV_GLOBAL_DEFAULT()->draw_info.unit_head = saved_units;
    LV_GLOBAL_DEFAULT()->draw_info.unit_cnt = saved_unit_count;
    lv_free(created_unit);
    lv_draw_eve5_set_enabled(true);
    lv_draw_invalidate_layer_support();
    if(other_display) lv_display_delete(other_display);
    lv_obj_clean(lv_screen_active());
    lv_refr_now(NULL);
    lv_free(reference);
    lv_free(frame);
}

static lv_obj_t * panel_create(void)
{
    lv_obj_t * panel = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(panel);
    lv_obj_set_pos(panel, 100, 100);
    lv_obj_set_size(panel, 100, 60);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xe06020), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    return panel;
}

static void capture(uint8_t * pixels)
{
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    TEST_ASSERT_TRUE(lv_test_screenshot_read(pixels));
}

static void assert_optional_effect(uint32_t before)
{
    capture(frame);
    if(lv_draw_layer_supported(NULL)) {
        TEST_ASSERT_EQUAL_UINT32(before, lv_draw_get_layer_fallback_count(NULL));
        TEST_ASSERT_NOT_EQUAL(0, lv_memcmp(reference, frame, frame_size));
    }
    else {
        TEST_ASSERT_GREATER_THAN_UINT32(before, lv_draw_get_layer_fallback_count(NULL));
        TEST_ASSERT_EQUAL_MEMORY(reference, frame, frame_size);
    }
}

void test_layer_query_is_cached_per_display_without_rendering(void)
{
    lv_display_t * disp = lv_display_get_default();
    other_display = lv_display_create(80, 60);
    LV_GLOBAL_DEFAULT()->draw_info.unit_head = &mock_unit;
    lv_draw_invalidate_layer_support();
    accepted_display = disp;
    lv_layer_t * head = disp->layer_head;
    lv_layer_t * next = head->next;
    lv_draw_task_t * tasks = head->draw_task_head;
    EVE_GpuAlloc * ga = lv_eve5_get_allocator(disp);
    uint32_t used = EVE_GpuAlloc_GetTotalUsed(ga);
    uint32_t refused = lv_draw_eve5_get_stats()->layer_tasks_refused;
    uint32_t fallbacks = lv_draw_get_layer_fallback_count(disp);
    TEST_ASSERT_TRUE(lv_draw_layer_supported(disp));
    TEST_ASSERT_TRUE(lv_draw_layer_supported(disp));
    TEST_ASSERT_EQUAL_UINT(1, probe_calls);
    TEST_ASSERT_FALSE(lv_draw_layer_supported(other_display));
    TEST_ASSERT_FALSE(lv_draw_layer_supported(other_display));
    TEST_ASSERT_EQUAL_UINT(2, probe_calls);
    accepted_display = other_display;
    TEST_ASSERT_TRUE(lv_draw_layer_supported(disp)); /* Cached until invalidated */
    lv_draw_invalidate_layer_support();
    TEST_ASSERT_FALSE(lv_draw_layer_supported(disp));
    TEST_ASSERT_TRUE(lv_draw_layer_supported(other_display));
    TEST_ASSERT_EQUAL_UINT(4, probe_calls);
    TEST_ASSERT_EQUAL_PTR(head, disp->layer_head);
    TEST_ASSERT_EQUAL_PTR(next, head->next);
    TEST_ASSERT_EQUAL_PTR(tasks, head->draw_task_head);
    TEST_ASSERT_EQUAL_UINT32(used, EVE_GpuAlloc_GetTotalUsed(ga));
    TEST_ASSERT_EQUAL_UINT32(refused, lv_draw_eve5_get_stats()->layer_tasks_refused);
    TEST_ASSERT_EQUAL_UINT32(fallbacks, lv_draw_get_layer_fallback_count(disp));
}

void test_layer_query_matches_real_draw_units_and_enable_changes(void)
{
    bool hardware = EVE_Hal_supportRenderTarget(lv_eve5_get_hal(lv_display_get_default()));
    uint32_t refused = lv_draw_eve5_get_stats()->layer_tasks_refused;
    lv_draw_invalidate_layer_support();
    TEST_ASSERT_EQUAL(hardware || LV_USE_DRAW_SW, lv_draw_layer_supported(NULL));
    lv_draw_eve5_set_enabled(false);
    TEST_ASSERT_EQUAL(!!LV_USE_DRAW_SW, lv_draw_layer_supported(NULL));
    lv_draw_eve5_set_enabled(true);
    TEST_ASSERT_EQUAL(hardware || LV_USE_DRAW_SW, lv_draw_layer_supported(NULL));
    TEST_ASSERT_EQUAL_UINT32(refused, lv_draw_eve5_get_stats()->layer_tasks_refused);
}

void test_layer_query_rechecks_when_a_draw_unit_is_added(void)
{
    LV_GLOBAL_DEFAULT()->draw_info.unit_head = NULL;
    lv_draw_invalidate_layer_support();
    TEST_ASSERT_FALSE(lv_draw_layer_supported(NULL));
    created_unit = lv_draw_create_unit(sizeof(*created_unit));
    TEST_ASSERT_NOT_NULL(created_unit);
    created_unit->evaluate_cb = probe_evaluate;
    accepted_display = lv_display_get_default();
    TEST_ASSERT_TRUE(lv_draw_layer_supported(NULL));
    TEST_ASSERT_EQUAL_UINT(1, probe_calls);
}

void test_layer_fallback_preserves_styles_and_effective_geometry(void)
{
    lv_obj_t * panel = panel_create();
    lv_obj_set_style_transform_rotation(panel, 900, 0);
    lv_obj_set_style_transform_scale_x(panel, 128, 0);
    lv_obj_update_layout(panel);
    LV_GLOBAL_DEFAULT()->draw_info.unit_head = &mock_unit;
    lv_draw_invalidate_layer_support();
    lv_point_t p = {180, 110};
    lv_point_t original = p;
    TEST_ASSERT_EQUAL(LV_LAYER_TYPE_NONE, lv_obj_get_layer_type(panel));
    lv_obj_transform_point(panel, &p, LV_OBJ_POINT_TRANSFORM_FLAG_NONE);
    TEST_ASSERT_EQUAL_MEMORY(&original, &p, sizeof(p));
    TEST_ASSERT_EQUAL(900, lv_obj_get_style_transform_rotation(panel, 0));
    TEST_ASSERT_EQUAL(128, lv_obj_get_style_transform_scale_x(panel, 0));
    TEST_ASSERT_TRUE(lv_obj_hit_test(panel, &p));
    accepted_display = lv_display_get_default();
    lv_draw_invalidate_layer_support();
    TEST_ASSERT_EQUAL(LV_LAYER_TYPE_TRANSFORM, lv_obj_get_layer_type(panel));
    lv_obj_transform_point(panel, &p, LV_OBJ_POINT_TRANSFORM_FLAG_NONE);
    TEST_ASSERT_NOT_EQUAL(0, lv_memcmp(&original, &p, sizeof(p)));
}

void test_corner_clipping_falls_back_to_rectangular_clipping(void)
{
    lv_obj_t * panel = panel_create();
    lv_obj_set_style_radius(panel, 24, 0);
    lv_obj_t * child = lv_obj_create(panel);
    lv_obj_remove_style_all(child);
    lv_obj_set_pos(child, -20, -20);
    lv_obj_set_size(child, 140, 100);
    lv_obj_set_style_bg_color(child, lv_color_hex(0x00ff00), 0);
    lv_obj_set_style_bg_opa(child, LV_OPA_COVER, 0);
    capture(reference);
    uint32_t before = lv_draw_get_layer_fallback_count(NULL);
    lv_obj_set_style_clip_corner(panel, true, 0);
    assert_optional_effect(before);
    if(!lv_draw_layer_supported(NULL)) {
        uint32_t width = lv_display_get_horizontal_resolution(NULL);
        TEST_ASSERT_EQUAL_UINT8(0, frame[(101 * width + 101) * 4]);
        TEST_ASSERT_EQUAL_UINT8(255, frame[(101 * width + 101) * 4 + 1]);
        /* Child content must still be clipped to the parent's rectangle. */
        TEST_ASSERT_EQUAL_UINT8(255, frame[(90 * width + 90) * 4]);
    }
}

void test_widget_transform_falls_back_to_untransformed_content(void)
{
    lv_obj_t * panel = panel_create();
    capture(reference);
    uint32_t before = lv_draw_get_layer_fallback_count(NULL);
    lv_obj_set_style_transform_rotation(panel, 300, 0);
    lv_obj_set_style_transform_scale_y(panel, 180, 0);
    lv_obj_set_style_transform_skew_x(panel, 100, 0);
    assert_optional_effect(before);
}

void test_layered_opacity_falls_back_without_losing_ordinary_opacity(void)
{
    lv_obj_t * panel = panel_create();
    lv_obj_set_style_opa(panel, 180, 0);
    capture(reference);
    uint32_t before = lv_draw_get_layer_fallback_count(NULL);
    lv_obj_set_style_opa_layered(panel, 128, 0);
    assert_optional_effect(before);
}

void test_bitmap_mask_falls_back_to_unmasked_content(void)
{
    LV_IMAGE_DECLARE(test_image_cogwheel_a8);
    lv_obj_t * panel = panel_create();
    capture(reference);
    uint32_t before = lv_draw_get_layer_fallback_count(NULL);
    lv_obj_set_style_bitmap_mask_src(panel, &test_image_cogwheel_a8, 0);
    assert_optional_effect(before);
}

void test_blend_mode_falls_back_to_normal_content(void)
{
    lv_obj_t * panel = panel_create();
    capture(reference);
    uint32_t before = lv_draw_get_layer_fallback_count(NULL);
    lv_obj_set_style_blend_mode(panel, LV_BLEND_MODE_ADDITIVE, 0);
    assert_optional_effect(before);
}

void test_drop_shadow_falls_back_without_removing_native_box_shadow(void)
{
    lv_obj_t * panel = panel_create();
    lv_obj_set_style_shadow_width(panel, 8, 0);
    lv_obj_set_style_shadow_opa(panel, 128, 0);
    lv_obj_t * label = lv_label_create(panel);
    lv_label_set_text(label, "Shadow");
    capture(reference);
    uint32_t before = lv_draw_get_layer_fallback_count(NULL);
    lv_obj_set_style_drop_shadow_radius(panel, 4, 0);
    lv_obj_set_style_drop_shadow_offset_x(panel, 10, 0);
    lv_obj_set_style_drop_shadow_opa(panel, 200, 0);
    lv_obj_set_style_drop_shadow_radius(label, 3, 0);
    lv_obj_set_style_drop_shadow_offset_y(label, 8, 0);
    lv_obj_set_style_drop_shadow_opa(label, 200, 0);
    assert_optional_effect(before);
}

void test_scale_label_rotation_falls_back_to_readable_labels(void)
{
    lv_obj_t * scale = lv_scale_create(lv_screen_active());
    lv_obj_set_pos(scale, 100, 100);
    lv_obj_set_size(scale, 300, 100);
    lv_scale_set_mode(scale, LV_SCALE_MODE_HORIZONTAL_BOTTOM);
    lv_scale_set_total_tick_count(scale, 5);
    lv_scale_set_major_tick_every(scale, 1);
    capture(reference);
    uint32_t before = lv_draw_get_layer_fallback_count(NULL);
    lv_obj_set_style_transform_rotation(scale, 450, LV_PART_INDICATOR);
    assert_optional_effect(before);
}

void test_bar_mask_falls_back_to_visible_indicator(void)
{
    lv_obj_t * bar = lv_bar_create(lv_screen_active());
    lv_obj_remove_style_all(bar);
    lv_obj_set_pos(bar, 100, 100);
    lv_obj_set_size(bar, 200, 60);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xff0000), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x00ff00), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(bar, lv_color_hex(0x00ff00), LV_PART_INDICATOR);
    lv_bar_set_value(bar, 50, LV_ANIM_OFF);
    capture(reference);
    uint32_t before = lv_draw_get_layer_fallback_count(NULL);
    /* A uniform gradient still takes the bar's layer-mask path. */
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    capture(frame);
    uint32_t width = lv_display_get_horizontal_resolution(NULL);
    /* Check visible green content, independent of gradient quantization. */
    TEST_ASSERT_LESS_THAN_UINT8(16, frame[(130 * width + 150) * 4]);
    TEST_ASSERT_GREATER_THAN_UINT8(240, frame[(130 * width + 150) * 4 + 1]);
    TEST_ASSERT_LESS_THAN_UINT8(16, frame[(130 * width + 150) * 4 + 2]);
    if(lv_draw_layer_supported(NULL)) {
        TEST_ASSERT_EQUAL_UINT32(before, lv_draw_get_layer_fallback_count(NULL));
    }
    else {
        TEST_ASSERT_GREATER_THAN_UINT32(before, lv_draw_get_layer_fallback_count(NULL));
    }
    before = lv_draw_get_layer_fallback_count(NULL);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_NONE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 30, 0);
    capture(frame);
    TEST_ASSERT_EQUAL_UINT8(0, frame[(130 * width + 150) * 4]);
    TEST_ASSERT_EQUAL_UINT8(255, frame[(130 * width + 150) * 4 + 1]);
    if(!lv_draw_layer_supported(NULL)) {
        TEST_ASSERT_GREATER_THAN_UINT32(before, lv_draw_get_layer_fallback_count(NULL));
    }
}
#endif
