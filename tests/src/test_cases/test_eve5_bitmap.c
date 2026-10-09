#if LV_BUILD_TEST
#include "../lvgl.h"
#include "unity/unity.h"
#include "src/drivers/display/eve5/lv_eve5.h"
#include "src/draw/eve5/lv_draw_eve5_private.h"

static lv_draw_eve5_unit_t * unit;
static EVE_GpuHandle source;
static EVE_GpuHandle target;
static uint8_t * pixels;
static bool submitted;

void setUp(void)
{
    unit = lv_malloc_zeroed(sizeof(*unit));
    TEST_ASSERT_NOT_NULL(unit);
    unit->hal = lv_eve5_get_hal(lv_display_get_default());
    unit->allocator = lv_eve5_get_allocator(lv_display_get_default());
    source = target = GA_HANDLE_INVALID;
    pixels = NULL;
    submitted = false;
    TEST_ASSERT_TRUE(EVE_Cmd_waitFlush(unit->hal));
    lv_draw_eve5_handle_pool_init(unit);
}

void tearDown(void)
{
    /* Discard the unsubmitted test list; the next renderer starts its own. */
    lv_draw_eve5_dl_start(unit);
    if(submitted && !EVE_Hal_supportRenderTarget(unit->hal)) {
        /* Retire the test's source before releasing its RAM_G storage. */
        EVE_CoDl_clear(unit->hal, 1, 1, 1);
        EVE_CoDl_display(unit->hal);
        EVE_CoCmd_swap(unit->hal);
    }
    TEST_ASSERT_TRUE(EVE_Cmd_waitFlush(unit->hal));
    EVE_GpuAlloc_Free(unit->allocator, source);
    EVE_GpuAlloc_Free(unit->allocator, target);
    lv_free(pixels);
    lv_free(unit);
}

static void assert_dl(const uint32_t * words, size_t count)
{
    EVE_HalContext * phost = unit->hal;
    TEST_ASSERT_TRUE(EVE_Cmd_waitFlush(phost));
    TEST_ASSERT_EQUAL_UINT32(count * 4, EVE_Hal_rd32(phost, REG_CMD_DL));
    for(size_t i = 0; i < count; i++) {
        TEST_ASSERT_EQUAL_HEX32(words[i], EVE_Hal_rd32(phost, RAM_DL + (uint32_t)i * 4));
    }
}

void test_eve5_bitmap_scratch_is_reserved(void)
{
    EVE_HalContext * phost = unit->hal;
    LV_UNUSED(phost);
    uint8_t scratch = unit->bitmap_scratch.handle;
    TEST_ASSERT_LESS_THAN_UINT8(16, scratch);
    TEST_ASSERT_NOT_EQUAL(EVE_CO_SCRATCH_HANDLE, scratch);
    for(unsigned i = 0; i < LV_DRAW_EVE5_HANDLE_CAP * 2; i++) {
        /* Even preferred allocations cannot evict either scratch handle. */
        uint8_t preferred = (i & 1) ? scratch : (uint8_t)EVE_CO_SCRATCH_HANDLE;
        uint8_t h = lv_draw_eve5_handle_alloc(unit, &unit, preferred);
        TEST_ASSERT_NOT_EQUAL(scratch, h);
        TEST_ASSERT_NOT_EQUAL(EVE_CO_SCRATCH_HANDLE, h);
        TEST_ASSERT_TRUE(lv_draw_eve5_handle_check(unit, h, &unit));
    }
    lv_draw_eve5_handle_invalidate_all(unit);
    TEST_ASSERT_TRUE(unit->handle_pool.slots[scratch].reserved);

#if EVE_SUPPORT_CHIPID >= EVE_FT810
    if(EVE_CHIPID >= EVE_FT810) {
        /* Pool setup must also respect a non-default CO scratch choice.
         * A context copy lets us check this without changing device state. */
        EVE_HalContext alternate = *phost;
        alternate.CoScratchHandle = 0;
        unit->hal = &alternate;
        lv_draw_eve5_handle_pool_init(unit);
        unit->hal = phost;
        TEST_ASSERT_EQUAL_UINT8(1, unit->bitmap_scratch.handle);
        TEST_ASSERT_TRUE(unit->handle_pool.slots[0].reserved);
        unit->base_unit.name = "EVE_NORT";
        lv_draw_eve5_handle_coprocessor_reset(&unit->base_unit);
        TEST_ASSERT_TRUE(unit->handle_pool.slots[EVE_CO_SCRATCH_HANDLE].reserved);
        TEST_ASSERT_NOT_EQUAL(EVE_CO_SCRATCH_HANDLE, unit->bitmap_scratch.handle);
    }
#endif
}

void test_eve5_bitmap_cache_tracks_low_and_high_words_independently(void)
{
    EVE_HalContext * phost = unit->hal;
    if(EVE_CHIPID < EVE_FT810) TEST_IGNORE_MESSAGE("High bitmap fields require FT810+");
    lv_draw_eve5_dl_start(unit);
    EVE_CoDl_bitmapHandle(phost, unit->bitmap_scratch.handle);
    lv_draw_eve5_bitmap_layout(unit, L8, 16, 16);
    lv_draw_eve5_bitmap_size(unit, NEAREST, BORDER, BORDER, 16, 16);
    lv_draw_eve5_bitmap_layout(unit, L8, 16, 16);
    lv_draw_eve5_bitmap_size(unit, NEAREST, BORDER, BORDER, 16, 16);
    /* Same low fields, different high fields. Handle attributes persist
     * through RESTORE_CONTEXT, so restoring the small dimensions must emit. */
    EVE_CoDl_saveContext(phost);
    lv_draw_eve5_bitmap_layout(unit, L8, 1040, 528);
    lv_draw_eve5_bitmap_size(unit, NEAREST, BORDER, BORDER, 528, 528);
    EVE_CoDl_restoreContext(phost);
    lv_draw_eve5_bitmap_layout(unit, L8, 16, 16);
    lv_draw_eve5_bitmap_size(unit, NEAREST, BORDER, BORDER, 16, 16);
    /* Change only low fields, then filter/wrap. */
    lv_draw_eve5_bitmap_layout(unit, L4, 8, 16);
    lv_draw_eve5_bitmap_size(unit, NEAREST, BORDER, BORDER, 8, 16);
    lv_draw_eve5_bitmap_size(unit, BILINEAR, REPEAT, BORDER, 8, 16);
    const uint32_t expected[] = {
        BITMAP_HANDLE(unit->bitmap_scratch.handle),
        BITMAP_LAYOUT_H(0, 0), BITMAP_LAYOUT(L8, 16, 16),
        BITMAP_SIZE_H(0, 0), BITMAP_SIZE(NEAREST, BORDER, BORDER, 16, 16),
        SAVE_CONTEXT(), BITMAP_LAYOUT_H(1, 1), BITMAP_SIZE_H(1, 1), RESTORE_CONTEXT(),
        BITMAP_LAYOUT_H(0, 0), BITMAP_SIZE_H(0, 0),
        BITMAP_LAYOUT(L4, 8, 16), BITMAP_SIZE(NEAREST, BORDER, BORDER, 8, 16),
        BITMAP_SIZE(BILINEAR, REPEAT, BORDER, 8, 16),
    };
    assert_dl(expected, sizeof(expected) / sizeof(expected[0]));
}

void test_eve5_bitmap_cache_reestablishes_state_for_each_list_and_reset(void)
{
    EVE_HalContext * phost = unit->hal;
    if(EVE_CHIPID < EVE_FT810) TEST_IGNORE_MESSAGE("High bitmap fields require FT810+");
    const uint32_t expected[] = {
        BITMAP_HANDLE(unit->bitmap_scratch.handle),
        BITMAP_LAYOUT_H(0, 0), BITMAP_LAYOUT(L8, 16, 16),
        BITMAP_SIZE_H(0, 0), BITMAP_SIZE(NEAREST, BORDER, BORDER, 16, 16),
    };
    for(unsigned i = 0; i < 2; i++) {
        lv_draw_eve5_dl_start(unit);
        EVE_CoDl_bitmapHandle(phost, unit->bitmap_scratch.handle);
        lv_draw_eve5_bitmap_layout(unit, L8, 16, 16);
        lv_draw_eve5_bitmap_size(unit, NEAREST, BORDER, BORDER, 16, 16);
        assert_dl(expected, sizeof(expected) / sizeof(expected[0]));
    }

    /* The reset hook invalidates cached attributes while retaining the
     * reservation. It must cause full setup even without a new DL. */
    unit->base_unit.name = "EVE_NORT";
    lv_draw_eve5_handle_coprocessor_reset(&unit->base_unit);
    lv_draw_eve5_bitmap_layout(unit, L8, 16, 16);
    lv_draw_eve5_bitmap_size(unit, NEAREST, BORDER, BORDER, 16, 16);
    TEST_ASSERT_TRUE(EVE_Cmd_waitFlush(phost));
    TEST_ASSERT_EQUAL_UINT32(9 * 4, EVE_Hal_rd32(phost, REG_CMD_DL));
    for(unsigned i = 1; i < 5; i++) {
        TEST_ASSERT_EQUAL_HEX32(expected[i], EVE_Hal_rd32(phost, RAM_DL + (i + 4) * 4));
    }
}

void test_eve5_bitmap_survives_coprocessor_scratch_use(void)
{
    EVE_HalContext * phost = unit->hal;
    source = EVE_GpuAlloc_Alloc(unit->allocator, 256, 0);
    uint32_t src = EVE_GpuAlloc_Get(unit->allocator, source);
    TEST_ASSERT_NOT_EQUAL(GA_INVALID, src);
    uint8_t solid[256];
    lv_memset(solid, 255, sizeof(solid));
    EVE_Hal_wrMem(phost, src, solid, sizeof(solid));
    uint32_t dst = GA_INVALID;
#ifdef EVE_SUPPORT_RENDERTARGET
    if(EVE_Hal_supportRenderTarget(phost)) {
        target = EVE_GpuAlloc_Alloc(unit->allocator, 64 * 16 * 2, GA_ALIGN_128);
        dst = EVE_GpuAlloc_Get(unit->allocator, target);
        TEST_ASSERT_NOT_EQUAL(GA_INVALID, dst);
        EVE_CoCmd_renderTarget(phost, dst, RGB565, 64, 16);
    }
#endif
    lv_draw_eve5_dl_start(unit);
    EVE_CoDl_clearColorRgb(phost, 0, 0, 0);
    EVE_CoDl_clear(phost, 1, 1, 1);
    EVE_CoDl_bitmapHandle(phost, unit->bitmap_scratch.handle);
    EVE_CoDl_bitmapSource(phost, src);
    lv_draw_eve5_bitmap_layout(unit, L8, 16, 16);
    lv_draw_eve5_bitmap_size(unit, NEAREST, BORDER, BORDER, 16, 16);
    EVE_CoDl_colorRgb(phost, 255, 0, 0);
    EVE_CoDl_begin(phost, BITMAPS);
    EVE_CoDl_vertex2f_0(phost, 0, 0);
    EVE_CoDl_end(phost);

    /* Widgets can overwrite the CO scratch handle. The LVGL handle must
     * retain both its source and cached setup across this command. */
    EVE_CoCmd_button(phost, 100, 100, 60, 30, 16, 0, "Test");
    EVE_CoDl_bitmapHandle(phost, unit->bitmap_scratch.handle);
    lv_draw_eve5_bitmap_layout(unit, L8, 16, 16);
    lv_draw_eve5_bitmap_size(unit, NEAREST, BORDER, BORDER, 16, 16);
    EVE_CoDl_colorRgb(phost, 255, 0, 0);
    EVE_CoDl_begin(phost, BITMAPS);
    EVE_CoDl_vertex2f_0(phost, 32, 0);
    EVE_CoDl_end(phost);
    EVE_CoDl_display(phost);
    EVE_CoCmd_swap(phost);
    submitted = true;
#ifdef EVE_SUPPORT_RENDERTARGET
    if(EVE_Hal_supportRenderTarget(phost)) EVE_CoCmd_graphicsFinish(phost);
#endif
    TEST_ASSERT_TRUE(EVE_Cmd_waitFlush(phost));
    if(dst != GA_INVALID) {
        TEST_ASSERT_EQUAL_HEX16(0xf800, EVE_Hal_rd16(phost, dst + (4 * 64 + 4) * 2));
        TEST_ASSERT_EQUAL_HEX16(0xf800, EVE_Hal_rd16(phost, dst + (4 * 64 + 36) * 2));
    }
    else {
        pixels = lv_malloc((size_t)phost->Width * phost->Height * 4);
        TEST_ASSERT_NOT_NULL(pixels);
        /* Read twice: NORT must also render correctly on repeated scanout. */
        for(unsigned i = 0; i < 2; i++) {
            TEST_ASSERT_TRUE(lv_eve5_read_screen(lv_display_get_default(), pixels, phost->Width * 4));
            const uint8_t red[] = { 255, 0, 0 };
            TEST_ASSERT_EQUAL_UINT8_ARRAY(red, pixels + (4 * phost->Width + 4) * 4, 3);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(red, pixels + (4 * phost->Width + 36) * 4, 3);
        }
    }
}
#endif
