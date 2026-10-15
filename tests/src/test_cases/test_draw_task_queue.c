#if LV_BUILD_TEST
#include "../lvgl.h"
#include "../../lvgl_private.h"
#include "unity/unity.h"

static lv_draw_unit_t * saved_units;
static uint32_t saved_unit_count;
static lv_layer_t layer;
static const lv_area_t area = {0, 0, 15, 15};

void setUp(void)
{
    saved_units = LV_GLOBAL_DEFAULT()->draw_info.unit_head;
    saved_unit_count = LV_GLOBAL_DEFAULT()->draw_info.unit_cnt;
    LV_GLOBAL_DEFAULT()->draw_info.unit_head = NULL;
    LV_GLOBAL_DEFAULT()->draw_info.unit_cnt = 0;
    lv_layer_init(&layer);
}

void tearDown(void)
{
    lv_draw_layer_deinit(&layer);
    LV_GLOBAL_DEFAULT()->draw_info.unit_head = saved_units;
    LV_GLOBAL_DEFAULT()->draw_info.unit_cnt = saved_unit_count;
}

static lv_draw_task_t * append(void)
{
    return lv_draw_add_task(&layer, &area, LV_DRAW_TASK_TYPE_FILL);
}

void test_task_queue_append_after_out_of_order_completion(void)
{
    lv_draw_task_t * a = append();
    lv_draw_task_t * b = append();
    lv_draw_task_t * c = append();
    b->state = LV_DRAW_TASK_STATE_FINISHED;
    lv_draw_dispatch_layer(NULL, &layer);
    TEST_ASSERT_EQUAL_PTR(a, layer.draw_task_head);
    TEST_ASSERT_EQUAL_PTR(c, a->next);
    TEST_ASSERT_EQUAL_PTR(c, layer.draw_task_tail);

    /*Complete the tail while earlier work remains, then append again.*/
    c->state = LV_DRAW_TASK_STATE_FINISHED;
    lv_draw_dispatch_layer(NULL, &layer);
    TEST_ASSERT_EQUAL_PTR(a, layer.draw_task_tail);
    TEST_ASSERT_NULL(a->next);
    lv_draw_task_t * d = append();
    TEST_ASSERT_EQUAL_PTR(d, a->next);
    TEST_ASSERT_EQUAL_PTR(d, layer.draw_task_tail);

    a->state = LV_DRAW_TASK_STATE_FINISHED;
    d->state = LV_DRAW_TASK_STATE_FINISHED;
    lv_draw_dispatch_layer(NULL, &layer);
    TEST_ASSERT_NULL(layer.draw_task_head);
    TEST_ASSERT_NULL(layer.draw_task_tail);
    lv_draw_task_t * e = append();
    TEST_ASSERT_EQUAL_PTR(e, layer.draw_task_head);
    TEST_ASSERT_EQUAL_PTR(e, layer.draw_task_tail);
    TEST_ASSERT_NULL(e->next);
}

void test_task_queue_deinit_clears_retained_tail(void)
{
    append();
    append();
    lv_draw_layer_deinit(&layer);
    TEST_ASSERT_NULL(layer.draw_task_head);
    TEST_ASSERT_NULL(layer.draw_task_tail);
    lv_draw_task_t * task = append();
    TEST_ASSERT_EQUAL_PTR(task, layer.draw_task_head);
    TEST_ASSERT_EQUAL_PTR(task, layer.draw_task_tail);
    TEST_ASSERT_NULL(task->next);
}

void test_eve5_collection_preserves_blocked_and_foreign_tasks(void)
{
    lv_draw_unit_t * unit = saved_units;
    while(unit && (!unit->name || lv_strcmp(unit->name, "EVE5_BT820"))) unit = unit->next;
    if(!unit) TEST_IGNORE_MESSAGE("Requires the EVE5 render-target draw unit");
    layer.display = lv_display_get_default();
    layer.color_format = LV_COLOR_FORMAT_RGB565;
    lv_draw_task_t * waiting = append();
    waiting->preference_score = 100;
    unit->evaluate_cb(unit, waiting);
    uint8_t id = waiting->preferred_draw_unit_id;
    TEST_ASSERT_NOT_EQUAL(LV_DRAW_UNIT_NONE, id);
    lv_draw_task_t * blocked = append();
    blocked->preferred_draw_unit_id = id;
    blocked->state = LV_DRAW_TASK_STATE_BLOCKED;
    lv_draw_task_t * queued = append();
    queued->preferred_draw_unit_id = id;
    queued->state = LV_DRAW_TASK_STATE_QUEUED;
    lv_draw_task_t * foreign = append();
    foreign->preferred_draw_unit_id = LV_DRAW_UNIT_NONE;

    TEST_ASSERT_EQUAL_INT32(1, unit->dispatch_cb(unit, &layer));
    TEST_ASSERT_EQUAL(LV_DRAW_TASK_STATE_QUEUED, waiting->state);
    TEST_ASSERT_EQUAL(LV_DRAW_TASK_STATE_BLOCKED, blocked->state);
    TEST_ASSERT_EQUAL(LV_DRAW_TASK_STATE_QUEUED, queued->state);
    TEST_ASSERT_EQUAL(LV_DRAW_TASK_STATE_WAITING, foreign->state);
    TEST_ASSERT_EQUAL_INT32(LV_DRAW_UNIT_IDLE, unit->dispatch_cb(unit, &layer));

    lv_draw_task_t * next = append();
    next->preferred_draw_unit_id = id;
    TEST_ASSERT_EQUAL_INT32(1, unit->dispatch_cb(unit, &layer));
    TEST_ASSERT_EQUAL(LV_DRAW_TASK_STATE_QUEUED, next->state);
    TEST_ASSERT_EQUAL(LV_DRAW_TASK_STATE_BLOCKED, blocked->state);
    TEST_ASSERT_EQUAL(LV_DRAW_TASK_STATE_WAITING, foreign->state);
}
#endif
