#if LV_BUILD_TEST
#include "../lvgl.h"
#include "unity/unity.h"
#include "src/drivers/display/eve5/lv_eve5.h"
#include "src/draw/eve5/lv_draw_eve5_private.h"

static lv_draw_eve5_unit_t * unit;
static lv_layer_t layer;
static lv_draw_task_t tasks[8];
static lv_draw_fill_dsc_t fill;
static lv_draw_border_dsc_t border;
static lv_image_dsc_t image;
static uint8_t pixels[8 * 8 * 4];

void setUp(void)
{
    unit = lv_malloc_zeroed(sizeof(*unit));
    TEST_ASSERT_NOT_NULL(unit);
    unit->hal = lv_eve5_get_hal(lv_display_get_default());
    unit->allocator = lv_eve5_get_allocator(lv_display_get_default());
    lv_draw_eve5_handle_pool_init(unit);
    lv_draw_eve5_ring_init(unit);
    lv_memzero(&image, sizeof(image));
    lv_memzero(&layer, sizeof(layer));
    layer.buf_area = (lv_area_t) {0, 0, 799, 479};
    lv_draw_fill_dsc_init(&fill);
    fill.grad.dir = LV_GRAD_DIR_VER;
    fill.grad.stops_count = 2;
    fill.grad.stops[0] = (lv_grad_stop_t) {.color = lv_color_hex(0xFF0000), .opa = 255, .frac = 0};
    fill.grad.stops[1] = (lv_grad_stop_t) {.color = lv_color_hex(0x0000FF), .opa = 255, .frac = 255};
    lv_draw_border_dsc_init(&border);
    border.width = 2;
    lv_memzero(tasks, sizeof(tasks));
    for(unsigned i = 0; i < 8; i++) {
        tasks[i].type = LV_DRAW_TASK_TYPE_FILL;
        tasks[i].preferred_draw_unit_id = DRAW_UNIT_ID_EVE5;
        tasks[i].state = LV_DRAW_TASK_STATE_QUEUED;
        tasks[i].target_layer = &layer;
        tasks[i].draw_dsc = &fill;
        tasks[i].area = (lv_area_t) {20, 20, 119, 99};
        tasks[i].clip_area = layer.buf_area;
        tasks[i].next = i < 7 ? &tasks[i + 1] : NULL;
    }
    layer.draw_task_head = tasks;
}

void tearDown(void)
{
    lv_draw_eve5_dl_start(unit);
    TEST_ASSERT_TRUE(EVE_Cmd_waitFlush(unit->hal));
    lv_draw_eve5_res_deinit(unit);
    lv_draw_eve5_ring_deinit(unit);
    lv_free(unit);
}

void test_nort_degradation_plans_from_the_tail(void)
{
    tasks[4].next = NULL;
    uint32_t cost[3];
    lv_draw_eve5_degrade_bounds(unit, tasks, NULL, false, cost);
    lv_draw_eve5_degrade_plan_t plan;
    lv_draw_eve5_degrade_plan(unit, tasks, NULL, 5 * cost[0], &plan);
    TEST_ASSERT_EQUAL_UINT8(0, plan.level);
    TEST_ASSERT_NULL(plan.boundary[0]);
    TEST_ASSERT_NULL(plan.boundary[1]);
    lv_draw_eve5_degrade_plan(unit, tasks, NULL, 2 * cost[0] + 3 * cost[1], &plan);
    TEST_ASSERT_EQUAL_UINT8(0, plan.level);
    TEST_ASSERT_EQUAL_PTR(&tasks[2], plan.boundary[0]);
    TEST_ASSERT_NULL(plan.boundary[1]);
}

void test_nort_second_level_follows_exhausting_the_first(void)
{
    tasks[4].next = NULL;
    for(unsigned i = 0; i < 5; i++) {
        tasks[i].type = LV_DRAW_TASK_TYPE_BORDER;
        tasks[i].draw_dsc = &border;
    }
    lv_draw_eve5_degrade_plan_t plan;
    uint32_t cost[3];
    lv_draw_eve5_degrade_bounds(unit, tasks, NULL, false, cost);
    lv_draw_eve5_degrade_plan(unit, tasks, NULL, 2 * cost[1], &plan);
    TEST_ASSERT_EQUAL_UINT8(1, plan.level);
    TEST_ASSERT_NULL(plan.boundary[0]);
    TEST_ASSERT_EQUAL_PTR(&tasks[2], plan.boundary[1]);
}

void test_nort_fill_border_pairs_are_indivisible(void)
{
    fill.grad.dir = LV_GRAD_DIR_NONE;
    tasks[5].next = NULL;
    for(unsigned i = 1; i < 6; i += 2) {
        tasks[i].type = LV_DRAW_TASK_TYPE_BORDER;
        tasks[i].draw_dsc = &border;
    }
    uint32_t cost[3];
    lv_draw_eve5_degrade_bounds(unit, tasks, NULL, false, cost);
    lv_draw_eve5_degrade_plan_t plan;
    lv_draw_eve5_degrade_plan(unit, tasks, NULL, cost[1] + 2 * cost[2], &plan);
    TEST_ASSERT_EQUAL_UINT64(3 * cost[0], plan.remaining[0]);
    TEST_ASSERT_EQUAL_PTR(&tasks[2], plan.boundary[1]);
    /* A foreign-unit task splits a pair and is not included in our totals. */
    tasks[1].preferred_draw_unit_id = 0;
    lv_draw_eve5_degrade_plan(unit, tasks, &tasks[2], 2048, &plan);
    TEST_ASSERT_EQUAL_UINT64(20, plan.remaining[2]);
}

void test_nort_boundary_moves_and_disappears_with_recovered_space(void)
{
    tasks[4].next = NULL;
    uint32_t cost[3];
    lv_draw_eve5_degrade_bounds(unit, tasks, NULL, false, cost);
    lv_draw_eve5_degrade_plan_t plan;
    lv_draw_eve5_degrade_plan(unit, tasks, NULL, cost[0] + 4 * cost[1], &plan);
    TEST_ASSERT_EQUAL_PTR(&tasks[1], plan.boundary[0]);
    for(unsigned i = 0; i < 3; i++) plan.remaining[i] -= cost[i];
    lv_draw_eve5_degrade_replan(unit, &tasks[1], NULL, cost[0] + 3 * cost[1], &plan);
    TEST_ASSERT_EQUAL_PTR(&tasks[2], plan.boundary[0]);
    lv_draw_eve5_degrade_replan(unit, &tasks[1], NULL, 4 * cost[0], &plan);
    TEST_ASSERT_EQUAL_UINT8(0, plan.level);
    TEST_ASSERT_NULL(plan.boundary[0]);
}

void test_nort_actual_hal_savings_recover_all_gradient_effects(void)
{
    if(EVE_Hal_supportRenderTarget(unit->hal)) TEST_IGNORE_MESSAGE("NORT renderer policy");
    tasks[2].next = NULL;
    uint32_t cost[3];
    lv_draw_eve5_degrade_bounds(unit, tasks, NULL, false, cost);
    lv_draw_eve5_dl_start(unit);
    uint32_t capacity = lv_draw_eve5_dl_budget(unit->hal);
    uint32_t prefix = capacity - 2 - (2 * cost[0] + cost[1]);
    for(uint32_t i = 0; i < prefix; i++) EVE_CoDl_colorMask(unit->hal, 1, 1, 1, 1);
    lv_draw_eve5_degrade_plan_t plan;
    lv_draw_eve5_degrade_plan(unit, tasks, NULL, capacity - 2 - unit->hal->DlEntries, &plan);
    TEST_ASSERT_EQUAL_PTR(&tasks[2], plan.boundary[0]);
    lv_draw_eve5_stats_t before = *lv_draw_eve5_get_stats();
    lv_draw_eve5_slice_t slice = {0};
    lv_draw_eve5_budget_t budget = {.reserve = 2};
    TEST_ASSERT_EQUAL_INT(3, lv_draw_eve5_render_tasks(unit, &layer, true, true, &slice, &budget));
    TEST_ASSERT_NULL(budget.stop);
    TEST_ASSERT_EQUAL_UINT32(before.degraded_tasks[0], lv_draw_eve5_get_stats()->degraded_tasks[0]);
    TEST_ASSERT_EQUAL_UINT32(before.degraded_tasks[1], lv_draw_eve5_get_stats()->degraded_tasks[1]);
    TEST_ASSERT_TRUE(EVE_Cmd_waitFlush(unit->hal));
    EVE_HalContext * phost = unit->hal;
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(capacity - 2, EVE_Hal_rd32(phost, REG_CMD_DL) / 4);
    TEST_ASSERT_EQUAL_UINT8(0, unit->degrade_level);
}

void test_nort_long_polyline_degrades_without_losing_the_following_task(void)
{
    if(EVE_Hal_supportRenderTarget(unit->hal)) TEST_IGNORE_MESSAGE("NORT renderer policy");
    lv_point_precise_t points[71];
    for(unsigned i = 0; i < 71; i++) points[i] = (lv_point_precise_t) {10 + (int32_t)i * 10, i & 1 ? 80 : 40};
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.width = 3;
    line.dash_width = 4;
    line.dash_gap = 3;
    line.points = points;
    line.point_cnt = 71;
    tasks[0].type = LV_DRAW_TASK_TYPE_LINE;
    tasks[0].draw_dsc = &line;
    tasks[1].next = NULL;
    fill.grad.dir = LV_GRAD_DIR_NONE;
    lv_draw_eve5_stats_t before = *lv_draw_eve5_get_stats();
    lv_draw_eve5_dl_start(unit);
    lv_draw_eve5_slice_t slice = {0};
    lv_draw_eve5_budget_t budget = {.reserve = 2};
    TEST_ASSERT_EQUAL_INT(2, lv_draw_eve5_render_tasks(unit, &layer, true, true, &slice, &budget));
    TEST_ASSERT_NULL(budget.stop);
    TEST_ASSERT_EQUAL(LV_DRAW_TASK_STATE_FINISHED, tasks[1].state);
    TEST_ASSERT_EQUAL_UINT32(before.degraded_tasks[0] + 1, lv_draw_eve5_get_stats()->degraded_tasks[0]);
    TEST_ASSERT_TRUE(EVE_Cmd_waitFlush(unit->hal));
    EVE_HalContext * phost = unit->hal;
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(lv_draw_eve5_dl_budget(phost) - 2, EVE_Hal_rd32(phost, REG_CMD_DL) / 4);
    TEST_ASSERT_EQUAL_INT(4, line.dash_width); /* Original descriptors are intact. */
}

void test_nort_unfittable_content_retains_the_hard_guard(void)
{
    if(EVE_Hal_supportRenderTarget(unit->hal)) TEST_IGNORE_MESSAGE("NORT renderer policy");
    char text[601];
    lv_memset(text, 'W', 600);
    text[600] = '\0';
    lv_draw_label_dsc_t label;
    lv_draw_label_dsc_init(&label);
    label.text = text;
    label.flag = LV_TEXT_FLAG_EXPAND;
    tasks[0].type = LV_DRAW_TASK_TYPE_LABEL;
    tasks[0].draw_dsc = &label;
    tasks[0].area = tasks[0].clip_area = layer.buf_area;
    tasks[1].next = NULL;
    lv_draw_eve5_dl_start(unit);
    lv_draw_eve5_slice_t slice = {0};
    lv_draw_eve5_budget_t budget = {.reserve = 2};
    TEST_ASSERT_EQUAL_INT(0, lv_draw_eve5_render_tasks(unit, &layer, true, true, &slice, &budget));
    TEST_ASSERT_EQUAL_PTR(tasks, budget.stop);
    TEST_ASSERT_EQUAL(LV_DRAW_TASK_STATE_QUEUED, tasks[1].state);
}

void test_degraded_gradient_averages_alpha_and_keeps_descriptors(void)
{
    fill.grad.stops[0].opa = 0;
    lv_draw_task_t draw = tasks[0];
    lv_draw_eve5_degrade_dsc_t dsc;
    TEST_ASSERT_TRUE(lv_draw_eve5_degrade_task(&draw, &dsc, 1));
    TEST_ASSERT_EQUAL(LV_GRAD_DIR_NONE, dsc.fill.grad.dir);
    TEST_ASSERT_EQUAL_UINT8(85, dsc.fill.color.red);
    TEST_ASSERT_EQUAL_UINT8(170, dsc.fill.color.blue);
    TEST_ASSERT_EQUAL_UINT8(127, dsc.fill.opa);
    TEST_ASSERT_EQUAL(LV_GRAD_DIR_VER, fill.grad.dir);
    TEST_ASSERT_EQUAL_PTR(&fill, tasks[0].draw_dsc);
}

void test_degraded_line_bounds_cover_real_dl_with_hv_and_masked_caps(void)
{
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.width = 7;
    line.p1 = (lv_point_precise_t) {5, 25};
    line.dash_width = 3;
    line.dash_gap = 2;
    tasks[0].type = LV_DRAW_TASK_TYPE_LINE;
    tasks[0].draw_dsc = &line;
    for(unsigned i = 0; i < 8; i++) {
        line.p2 = (lv_point_precise_t) {700, i & 1 ? 25 : 250};
        line.round_start = (i & 2) != 0;
        line.round_end = (i & 4) != 0;
        unit->degrade_level = 1;
        lv_draw_eve5_dl_start(unit);
        lv_draw_eve5_hal_draw_line(unit, tasks, false, true);
        TEST_ASSERT_TRUE(EVE_Cmd_waitFlush(unit->hal));
        uint32_t cost[3];
        lv_draw_eve5_degrade_bounds(unit, tasks, NULL, false, cost);
        EVE_HalContext * phost = unit->hal;
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(cost[1], EVE_Hal_rd32(phost, REG_CMD_DL) / 4);
    }
}

void test_degraded_fill_bounds_cover_real_dl_and_merged_pairs(void)
{
    tasks[1].type = LV_DRAW_TASK_TYPE_BORDER;
    tasks[1].draw_dsc = &border;
    tasks[1].next = NULL;
    for(unsigned i = 0; i < 8; i++) {
        fill.radius = border.radius = i & 1 ? 12 : 0;
        fill.opa = i & 2 ? 120 : 255;
        border.opa = i & 4 ? 100 : 255;
        uint32_t cost[3];
        lv_draw_eve5_degrade_bounds(unit, tasks, NULL, false, cost);
        for(unsigned level = 1; level <= 2; level++) {
            lv_draw_task_t draw = tasks[0];
            lv_draw_eve5_degrade_dsc_t dsc;
            lv_draw_eve5_degrade_task(&draw, &dsc, level);
            lv_draw_eve5_dl_start(unit);
            if(level == 1) lv_draw_eve5_hal_draw_fill_with_border(unit, &draw, &tasks[1]);
            else lv_draw_eve5_hal_draw_fill(unit, &draw, false);
            TEST_ASSERT_TRUE(EVE_Cmd_waitFlush(unit->hal));
            EVE_HalContext * phost = unit->hal;
            TEST_ASSERT_LESS_OR_EQUAL_UINT32(cost[level], EVE_Hal_rd32(phost, REG_CMD_DL) / 4);
        }
    }
}

void test_degraded_transform_does_not_enable_a_new_image_mask(void)
{
    lv_draw_image_dsc_t desc;
    lv_draw_image_dsc_init(&desc);
    desc.rotation = 450;
    desc.clip_radius = 16;
    tasks[0].type = LV_DRAW_TASK_TYPE_IMAGE;
    tasks[0].draw_dsc = &desc;
    lv_draw_task_t draw = tasks[0];
    lv_draw_eve5_degrade_dsc_t dsc;
    TEST_ASSERT_TRUE(lv_draw_eve5_degrade_task(&draw, &dsc, 2));
    TEST_ASSERT_EQUAL_INT(0, dsc.image.rotation);
    TEST_ASSERT_EQUAL_INT(0, eve5_image_clip_radius(&dsc.image));
    TEST_ASSERT_EQUAL_INT(450, desc.rotation);
    TEST_ASSERT_EQUAL_INT(16, desc.clip_radius);
}

static void check_image_bound(lv_color_format_t cf, bool alpha_only)
{
    image.header.magic = LV_IMAGE_HEADER_MAGIC;
    image.header.cf = cf;
    image.header.w = image.header.h = 8;
    image.header.stride = 8 * lv_color_format_get_size(cf);
    image.data_size = image.header.stride * 8;
    image.data = pixels;
    lv_memset(pixels, 0xFF, sizeof(pixels));
    TEST_ASSERT_NOT_NULL(lv_draw_eve5_upload_image_to_gpu_ex(unit, &image, false, false));
    lv_draw_image_dsc_t desc;
    lv_draw_image_dsc_init(&desc);
    desc.src = &image;
    desc.header = image.header;
    desc.recolor = lv_color_hex(0xA03040);
    desc.recolor_opa = 170;
    tasks[0].type = LV_DRAW_TASK_TYPE_IMAGE;
    tasks[0].draw_dsc = &desc;
    tasks[0].next = NULL;
    for(unsigned i = 0; i < 4; i++) {
        desc.rotation = i & 1 ? 450 : 0;
        desc.scale_x = i & 2 ? 300 : LV_SCALE_NONE;
        uint32_t cost[3];
        lv_draw_eve5_degrade_bounds(unit, tasks, NULL, false, cost);
        lv_draw_task_t draw = tasks[0];
        lv_draw_eve5_degrade_dsc_t dsc;
        lv_draw_eve5_degrade_task(&draw, &dsc, 2);
        unit->degrade_level = 2;
        unit->task_degraded = false;
        lv_draw_eve5_dl_start(unit);
        lv_draw_eve5_hal_draw_image(unit, &draw, false);
        TEST_ASSERT_TRUE(EVE_Cmd_waitFlush(unit->hal));
        EVE_HalContext * phost = unit->hal;
        uint32_t actual = EVE_Hal_rd32(phost, REG_CMD_DL) / 4;
        TEST_ASSERT_GREATER_THAN_UINT32(0, actual);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(cost[2], actual);
        TEST_ASSERT_EQUAL(!alpha_only, unit->task_degraded);
        TEST_ASSERT_EQUAL_UINT8(170, desc.recolor_opa);
    }
}

void test_degraded_color_images_use_one_bounded_pass(void)
{
    check_image_bound(LV_COLOR_FORMAT_ARGB8888, false);
}

void test_degraded_alpha_icons_keep_their_tint(void)
{
    check_image_bound(LV_COLOR_FORMAT_A8, true);
}

void test_degraded_label_rotation_uses_the_plain_glyph_bound(void)
{
    lv_draw_label_dsc_t label;
    lv_draw_label_dsc_init(&label);
    label.font = &lv_font_montserrat_16;
    label.text = "Transform";
    label.rotation = 450;
    tasks[0].type = LV_DRAW_TASK_TYPE_LABEL;
    tasks[0].draw_dsc = &label;
    tasks[0].next = NULL;
    uint32_t cost[3];
    lv_draw_eve5_degrade_bounds(unit, tasks, NULL, true, cost);
    TEST_ASSERT_LESS_THAN_UINT32(cost[0], cost[2]);
    lv_draw_task_t draw = tasks[0];
    lv_draw_eve5_degrade_dsc_t dsc;
    TEST_ASSERT_TRUE(lv_draw_eve5_degrade_task(&draw, &dsc, 2));
    lv_draw_eve5_dl_start(unit);
    lv_draw_eve5_hal_draw_label(unit, &draw);
    TEST_ASSERT_TRUE(EVE_Cmd_waitFlush(unit->hal));
    EVE_HalContext * phost = unit->hal;
    uint32_t actual = EVE_Hal_rd32(phost, REG_CMD_DL) / 4;
    TEST_ASSERT_GREATER_THAN_UINT32(0, actual);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(cost[2], actual);
    TEST_ASSERT_EQUAL_INT(450, label.rotation);
}

#endif
