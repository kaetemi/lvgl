/**
 * @file lv_draw_eve5_degrade.c
 * NORT quality planning. Restore the longest possible high-quality prefix of
 * a lower-quality list: equivalent to degrading from the tail, without a
 * reverse list, allocations, or additions to LVGL's draw tasks.
 *
 * Copyright (C) 2026 Bridgetek Pte Ltd
 * SPDX-License-Identifier: MIT
 */
#include "lv_draw_eve5_private.h"

#if LV_USE_DRAW_EVE5

/* Integrate linearly interpolated premultiplied color and alpha over the
 * stop positions. Factors of six keep the product integral exact in integers.
 * Weighting color by coverage preserves the mean premultiplied color. */
static void gradient_average(const lv_grad_dsc_t * grad, lv_color_t * color, lv_opa_t * opa)
{
    uint64_t alpha = 0, red = 0, green = 0, blue = 0;
    for(uint32_t i = 0; i <= grad->stops_count; i++) {
        const lv_grad_stop_t * a = &grad->stops[i ? i - 1 : 0];
        const lv_grad_stop_t * b = &grad->stops[i < grad->stops_count ? i : i - 1];
        int32_t left = i ? a->frac : 0;
        int32_t right = i < grad->stops_count ? b->frac : 255;
        uint32_t width = (uint32_t)LV_MAX(right - left, 0);
        uint32_t wa = 2 * a->opa + b->opa;
        uint32_t wb = a->opa + 2 * b->opa;
        alpha += (uint64_t)width * 3 * (a->opa + b->opa);
        red += (uint64_t)width * (wa * a->color.red + wb * b->color.red);
        green += (uint64_t)width * (wa * a->color.green + wb * b->color.green);
        blue += (uint64_t)width * (wa * a->color.blue + wb * b->color.blue);
    }
    if(alpha) {
        color->red = (uint8_t)(red / alpha);
        color->green = (uint8_t)(green / alpha);
        color->blue = (uint8_t)(blue / alpha);
    }
    else *color = lv_color_black();
    *opa = (lv_opa_t)((uint64_t)*opa * alpha / (6 * 255 * 255));
}

/* Operate only on a caller-owned task copy and descriptor storage. The
 * original descriptors, layout, and clips remain available for planning. */
bool lv_draw_eve5_degrade_task(lv_draw_task_t * task, lv_draw_eve5_degrade_dsc_t * dsc, uint8_t level)
{
    if(level == 0) return false;
    switch(task->type) {
        case LV_DRAW_TASK_TYPE_FILL: {
                const lv_draw_fill_dsc_t * src = task->draw_dsc;
                if(src->grad.dir == LV_GRAD_DIR_NONE || !src->grad.stops_count) return false;
                dsc->fill = *src;
                gradient_average(&src->grad, &dsc->fill.color, &dsc->fill.opa);
                dsc->fill.grad.dir = LV_GRAD_DIR_NONE;
                task->draw_dsc = &dsc->fill;
                return true;
            }
        case LV_DRAW_TASK_TYPE_TRIANGLE: {
                const lv_draw_triangle_dsc_t * src = task->draw_dsc;
                if(src->grad.dir == LV_GRAD_DIR_NONE || !src->grad.stops_count) return false;
                dsc->triangle = *src;
                gradient_average(&src->grad, &dsc->triangle.color, &dsc->triangle.opa);
                dsc->triangle.grad.dir = LV_GRAD_DIR_NONE;
                task->draw_dsc = &dsc->triangle;
                return true;
            }
        case LV_DRAW_TASK_TYPE_LABEL: {
                const lv_draw_label_dsc_t * src = task->draw_dsc;
                if(level < 2 || src->rotation == 0) return false;
                dsc->label = *src;
                dsc->label.rotation = 0;
                task->draw_dsc = &dsc->label;
                return true;
            }
        case LV_DRAW_TASK_TYPE_LETTER: {
                const lv_draw_letter_dsc_t * src = task->draw_dsc;
                if(level < 2 || (!src->rotation && src->scale_x == LV_SCALE_NONE && src->scale_y == LV_SCALE_NONE &&
                                 !src->skew_x && !src->skew_y)) return false;
                dsc->letter = *src;
                dsc->letter.rotation = dsc->letter.skew_x = dsc->letter.skew_y = 0;
                dsc->letter.scale_x = dsc->letter.scale_y = LV_SCALE_NONE;
                task->draw_dsc = &dsc->letter;
                return true;
            }
        case LV_DRAW_TASK_TYPE_IMAGE:
        case LV_DRAW_TASK_TYPE_LAYER: {
                const lv_draw_image_dsc_t * src = task->draw_dsc;
                if(level < 2 || (!src->rotation && src->scale_x == LV_SCALE_NONE && src->scale_y == LV_SCALE_NONE &&
                                 !src->skew_x && !src->skew_y)) return false;
                dsc->image = *src;
                /* Removing a transform must not enable a previously ignored
                 * clip-radius mask, which could increase the DL cost. */
                dsc->image.clip_radius = eve5_image_clip_radius(src);
                dsc->image.rotation = dsc->image.skew_x = dsc->image.skew_y = 0;
                dsc->image.scale_x = dsc->image.scale_y = LV_SCALE_NONE;
                task->draw_dsc = &dsc->image;
                task->_real_area = task->area;
                return true;
            }
        default:
            return false;
    }
}

static lv_draw_task_t * group_next(lv_draw_task_t * t, const lv_draw_task_t * end)
{
    lv_draw_task_t * border = t->type == LV_DRAW_TASK_TYPE_FILL ? lv_draw_eve5_fill_matching_border(t, end) : NULL;
    return border ? border->next : t->next;
}

static bool queued(const lv_draw_task_t * t)
{
    return t->preferred_draw_unit_id == DRAW_UNIT_ID_EVE5 && t->state == LV_DRAW_TASK_STATE_QUEUED;
}

void lv_draw_eve5_degrade_replan(lv_draw_eve5_unit_t * u, lv_draw_task_t * start,
                               const lv_draw_task_t * end, uint32_t available, lv_draw_eve5_degrade_plan_t * plan)
{
    plan->boundary[0] = plan->boundary[1] = NULL;
    uint8_t base = 0;
    while(base < 2 && plan->remaining[base] > available) base++;
    plan->level = base;
    if(base == 0 || plan->remaining[base] > available) return;

    uint64_t total = plan->remaining[base];
    plan->level = base - 1;
    for(lv_draw_task_t * t = start; t && t != end;) {
        if(!queued(t)) {
            t = t->next;
            continue;
        }
        uint32_t cost[3];
        lv_draw_eve5_degrade_bounds(u, t, end, plan->refined, cost);
        total += cost[base - 1] - cost[base];
        if(total > available) {
            plan->boundary[base - 1] = t;
            break;
        }
        t = group_next(t, end);
    }
}

void lv_draw_eve5_degrade_plan(lv_draw_eve5_unit_t * u, lv_draw_task_t * start,
                             const lv_draw_task_t * end, uint32_t available, lv_draw_eve5_degrade_plan_t * plan)
{
    lv_memzero(plan, sizeof(*plan));
    /* Cheap whole-string bounds first. Only a list under pressure needs the
     * existing visible-line refinement. All three levels share that scan. */
    for(unsigned pass = 0; pass < 2; pass++) {
        plan->remaining[0] = plan->remaining[1] = plan->remaining[2] = 0;
        for(lv_draw_task_t * t = start; t && t != end;) {
            if(!queued(t)) {
                t = t->next;
                continue;
            }
            uint32_t cost[3];
            lv_draw_eve5_degrade_bounds(u, t, end, plan->refined, cost);
            for(unsigned i = 0; i < 3; i++) plan->remaining[i] += cost[i];
            t = group_next(t, end);
        }
        if(plan->remaining[0] <= available || pass != 0) break;
        plan->refined = true;
    }
    lv_draw_eve5_degrade_replan(u, start, end, available, plan);
}

#endif /* LV_USE_DRAW_EVE5 */
