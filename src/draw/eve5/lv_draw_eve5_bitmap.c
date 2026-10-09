/**
 * @file lv_draw_eve5_bitmap.c
 * Bitmap setup cache for the draw unit's reserved scratch handle.
 *
 * Copyright (C) 2026 Bridgetek Pte Ltd
 * SPDX-License-Identifier: MIT
 */

#include "lv_draw_eve5_private.h"

#if LV_USE_DRAW_EVE5

void lv_draw_eve5_bitmap_invalidate(lv_draw_eve5_unit_t * u)
{
    u->bitmap_scratch.layout = 0;
    u->bitmap_scratch.layout_high = 0;
    u->bitmap_scratch.size = 0;
    u->bitmap_scratch.size_high = 0;
}

void lv_draw_eve5_dl_start(lv_draw_eve5_unit_t * u)
{
    EVE_CoCmd_dlStart(u->hal);
    lv_draw_eve5_bitmap_invalidate(u);
}

static void bitmap_word(lv_draw_eve5_unit_t * u, uint32_t * cached, uint32_t word)
{
    if(*cached == word) return;
    EVE_CoCmd_dl(u->hal, word);
    *cached = word;
}

void lv_draw_eve5_bitmap_layout(lv_draw_eve5_unit_t * u, uint8_t format, uint16_t stride, uint16_t height)
{
    EVE_HalContext * phost = u->hal;
    LV_UNUSED(phost); /* Single-target instruction encodings are constants. */
#if EVE_SUPPORT_CHIPID >= EVE_FT810
    if(EVE_CHIPID >= EVE_FT810) {
        bitmap_word(u, &u->bitmap_scratch.layout_high, BITMAP_LAYOUT_H(stride >> 10, height >> 9));
    }
#endif
    bitmap_word(u, &u->bitmap_scratch.layout, BITMAP_LAYOUT(format, stride, height));
}

void lv_draw_eve5_bitmap_size(lv_draw_eve5_unit_t * u, uint8_t filter, uint8_t wrapx, uint8_t wrapy,
                              uint16_t width, uint16_t height)
{
    EVE_HalContext * phost = u->hal;
    LV_UNUSED(phost);
#if EVE_SUPPORT_CHIPID >= EVE_FT810
    if(EVE_CHIPID >= EVE_FT810) {
        bitmap_word(u, &u->bitmap_scratch.size_high, BITMAP_SIZE_H(width >> 9, height >> 9));
    }
#endif
    bitmap_word(u, &u->bitmap_scratch.size, BITMAP_SIZE(filter, wrapx, wrapy, width, height));
}

#endif /* LV_USE_DRAW_EVE5 */
