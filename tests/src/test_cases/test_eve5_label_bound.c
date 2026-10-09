#if LV_BUILD_TEST
#include "../lvgl.h"
#include "unity/unity.h"
#include "src/drivers/display/eve5/lv_eve5.h"
#include "src/drivers/display/eve5/lv_eve5_rom_font.h"
#include "src/draw/eve5/lv_draw_eve5_private.h"

static lv_draw_eve5_unit_t * unit;
static lv_font_t * rom_font;
static lv_font_t * image_font;
static lv_font_t * loaded_font;
static lv_font_t * tiny_font;
static lv_font_t * freetype_font;
static lv_font_t * emoji_font;
static lv_layer_t layer;
static lv_draw_task_t task;
static lv_draw_label_dsc_t label;
static char options[315];
static unsigned metric_calls;
static unsigned bitmap_calls;

static bool count_metrics(const lv_font_t * font, lv_font_glyph_dsc_t * glyph,
                          uint32_t letter, uint32_t next)
{
    metric_calls++;
    return lv_font_get_glyph_dsc_fmt_txt(font, glyph, letter, next);
}

static LV_IMAGE_DSC_CONST void * count_bitmaps(lv_font_glyph_dsc_t * glyph, lv_draw_buf_t * buf)
{
    LV_UNUSED(glyph);
    LV_UNUSED(buf);
    bitmap_calls++;
    return NULL;
}

void setUp(void)
{
    unit = lv_malloc_zeroed(sizeof(*unit));
    TEST_ASSERT_NOT_NULL(unit);
    unit->hal = lv_eve5_get_hal(lv_display_get_default());
    unit->allocator = lv_eve5_get_allocator(lv_display_get_default());
    lv_draw_eve5_handle_pool_init(unit);
    lv_draw_eve5_rom_font_init(unit);
    rom_font = NULL;
    image_font = NULL;
    loaded_font = NULL;
    tiny_font = NULL;
    freetype_font = NULL;
    emoji_font = NULL;
    lv_memzero(&layer, sizeof(layer));
    layer.buf_area = (lv_area_t) {0, 0, 799, 479};
    lv_memzero(&task, sizeof(task));
    task.type = LV_DRAW_TASK_TYPE_LABEL;
    task.preferred_draw_unit_id = DRAW_UNIT_ID_EVE5;
    task.state = LV_DRAW_TASK_STATE_QUEUED;
    task.target_layer = &layer;
    task.draw_dsc = &label;
    task.area = (lv_area_t) {42, -415, 81, 1584};
    task.clip_area = (lv_area_t) {42, 200, 81, 249};
    layer.draw_task_head = &task;
    lv_draw_label_dsc_init(&label);
    label.font = &lv_font_montserrat_16;
    label.line_space = 16;
    for(unsigned i = 0; i < 63; i++) lv_memcpy(options + 5 * i, "0@Tg\n", 5);
    options[314] = '\0';
    label.text = options;
}

void tearDown(void)
{
    /* No test list is submitted to scanout. Retire it before freeing fonts. */
    lv_draw_eve5_dl_start(unit);
    TEST_ASSERT_TRUE(EVE_Cmd_waitFlush(unit->hal));
    lv_draw_eve5_res_deinit(unit);
    lv_eve5_rom_font_destroy(rom_font);
    if(image_font) lv_imgfont_destroy(image_font);
    lv_binfont_destroy(loaded_font);
    if(tiny_font) lv_tiny_ttf_destroy(tiny_font);
    if(freetype_font) lv_freetype_font_delete(freetype_font);
    if(emoji_font) lv_freetype_font_delete(emoji_font);
    lv_free(unit);
}

/* Check real coprocessor expansion for RGB and both alpha paths, rather than
 * comparing the estimator with a second copy of its line-count algorithm. */
static void assert_render_bound(void)
{
    uint32_t bound = lv_draw_eve5_label_dl_bound(&task, true);
    EVE_HalContext * phost = unit->hal;
    for(unsigned pass = 0; pass < 3; pass++) {
        lv_draw_eve5_dl_start(unit);
        if(pass == 0) lv_draw_eve5_hal_draw_label(unit, &task);
        else lv_draw_eve5_alpha_draw_label(unit, &task, pass == 2);
        TEST_ASSERT_TRUE(EVE_Cmd_waitFlush(phost));
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(bound, EVE_Hal_rd32(phost, REG_CMD_DL) / 4);
    }
}

void test_clipped_label_keeps_following_tasks(void)
{
    TEST_ASSERT_GREATER_THAN_UINT32(2048, lv_draw_eve5_label_dl_bound(&task, false));
    TEST_ASSERT_LESS_THAN_UINT32(256, lv_draw_eve5_label_dl_bound(&task, true));
    assert_render_bound();

    lv_draw_fill_dsc_t fill;
    lv_draw_fill_dsc_init(&fill);
    lv_draw_task_t next = task;
    next.type = LV_DRAW_TASK_TYPE_FILL;
    next.draw_dsc = &fill;
    next.area = next.clip_area = (lv_area_t) {100, 100, 119, 119};
    task.next = &next;
    TEST_ASSERT_TRUE(lv_draw_eve5_range_fits_dl(unit, &task, NULL, EVE5_DL_FINISH_NORT));
    lv_draw_eve5_slice_t slice = {0};
    lv_draw_eve5_budget_t budget = {0};
    budget.reserve = EVE5_DL_FINISH_NORT;
    budget.can_split = true;
    lv_draw_eve5_dl_start(unit);
    TEST_ASSERT_EQUAL_INT(2, lv_draw_eve5_render_tasks(unit, &layer, true, true, &slice, &budget));
    TEST_ASSERT_NULL(budget.stop);
    TEST_ASSERT_EQUAL(LV_DRAW_TASK_STATE_FINISHED, next.state);
    task.next = NULL;
}

void test_clipped_label_wrapping_offsets_and_edges(void)
{
    label.text = "One two three four five six seven eight nine ten\n"
                 "One two three four five six seven eight nine ten\n"
                 "One two three four five six seven eight nine ten";
    const lv_text_flag_t flags[] = {LV_TEXT_FLAG_NONE, LV_TEXT_FLAG_EXPAND, LV_TEXT_FLAG_FIT};
    const int32_t offsets[] = {-19, 0, 17, 300};
    label.line_space = 3;
    for(unsigned f = 0; f < 3; f++) {
        label.flag = flags[f];
        for(unsigned o = 0; o < 4; o++) {
            label.ofs_y = offsets[o];
            for(int y = 29; y <= 31; y++) {
                task.area = (lv_area_t) {20, -20, 91, 500};
                task.clip_area = (lv_area_t) {30, y, 70, y + 20};
                assert_render_bound();
            }
        }
    }
}

void test_clipped_label_decor_recolor_missing_and_rotated_glyphs(void)
{
    label.text = "#00ff00 offscreen text\nmore text\nvisible \xEF\xBF\xBF text#\nlast line";
    label.flag = LV_TEXT_FLAG_EXPAND | LV_TEXT_FLAG_RECOLOR;
    label.decor = LV_TEXT_DECOR_UNDERLINE | LV_TEXT_DECOR_STRIKETHROUGH;
    label.sel_start = 0;
    label.sel_end = 100;
    label.line_space = 3;
    label.opa = 129;
    task.area = (lv_area_t) {20, -38, 300, 400};
    task.clip_area = (lv_area_t) {30, 0, 200, 20};
    assert_render_bound();
    label.rotation = 450;
    assert_render_bound();
}

void test_clipped_label_preserves_hint_and_explicit_length(void)
{
    label.flag = LV_TEXT_FLAG_EXPAND;
    lv_draw_label_hint_t hint = {.line_start = 50, .y = 320, .coord_y = -415};
    label.hint = &hint;
    lv_draw_label_hint_t saved = hint;
    uint32_t bound = lv_draw_eve5_label_dl_bound(&task, true);
    TEST_ASSERT_EQUAL_MEMORY(&saved, &hint, sizeof(hint));
    TEST_ASSERT_LESS_THAN_UINT32(256, bound);
    assert_render_bound();
    hint = (lv_draw_label_hint_t) {.line_start = 95, .y = 608, .coord_y = -415};
    label.text_length = 100;
    TEST_ASSERT_EQUAL_UINT32(lv_draw_eve5_label_dl_bound(&task, false),
                            lv_draw_eve5_label_dl_bound(&task, true));
    assert_render_bound();
    label.hint = NULL;
    label.text_length = 20;
    assert_render_bound();
}

void test_clipped_label_rom_and_direct_cmdtext(void)
{
    rom_font = lv_eve5_rom_font_create(unit->hal, 26);
    TEST_ASSERT_NOT_NULL(rom_font);
    label.font = rom_font;
    label.flag = LV_TEXT_FLAG_EXPAND;
    TEST_ASSERT_LESS_THAN_UINT32(lv_draw_eve5_label_dl_bound(&task, false),
                                lv_draw_eve5_label_dl_bound(&task, true));
    assert_render_bound();

    /* A metrics-only font takes the heuristic whole-string CMD_TEXT path. */
    lv_font_t metrics = lv_font_montserrat_16;
    metrics.get_glyph_bitmap = NULL;
    label.font = &metrics;
    label.text = "one\ntwo\nthree\nfour";
    TEST_ASSERT_EQUAL_UINT32(lv_draw_eve5_label_dl_bound(&task, false),
                            lv_draw_eve5_label_dl_bound(&task, true));
    assert_render_bound();
}

void test_label_bound_refines_only_when_needed_without_loading_bitmaps(void)
{
    lv_font_t font = lv_font_montserrat_16;
    font.get_glyph_dsc = count_metrics;
    font.get_glyph_bitmap = count_bitmaps;
    label.font = &font;
    options[4] = '\0';
    metric_calls = bitmap_calls = 0;
    uint32_t cheap = lv_draw_eve5_label_dl_bound(&task, false);
    unsigned cheap_calls = metric_calls;
    TEST_ASSERT_LESS_THAN_UINT32(2048, cheap);
    metric_calls = 0;
    TEST_ASSERT_TRUE(lv_draw_eve5_range_fits_dl(unit, &task, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(cheap_calls, metric_calls);
    TEST_ASSERT_EQUAL_UINT(0, bitmap_calls);

    options[4] = '\n';
    metric_calls = 0;
    cheap = lv_draw_eve5_label_dl_bound(&task, false);
    cheap_calls = metric_calls;
    TEST_ASSERT_GREATER_THAN_UINT32(2048, cheap);
    metric_calls = 0;
    TEST_ASSERT_TRUE(lv_draw_eve5_range_fits_dl(unit, &task, NULL, 0));
    TEST_ASSERT_GREATER_THAN_UINT(cheap_calls, metric_calls);
    TEST_ASSERT_EQUAL_UINT(0, bitmap_calls);
}

void test_compressed_label_fits_after_existing_display_list(void)
{
    LV_FONT_DECLARE(test_font_montserrat_ascii_3bpp_compressed);
    label.font = &test_font_montserrat_ascii_3bpp_compressed;
    label.text = "3bpp compressed font: the quick brown fox jumps over the lazy dog";
    label.line_space = 0;
    task.area = (lv_area_t) {55, 402, 743, 423};
    task.clip_area = (lv_area_t) {50, 397, 748, 423};
    uint32_t bound = lv_draw_eve5_label_dl_bound(&task, false);
    TEST_ASSERT_LESS_THAN_UINT32(1024, bound);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(bound, lv_draw_eve5_label_dl_bound(&task, true));
    assert_render_bound();
    label.rotation = 450;
    assert_render_bound();
    label.rotation = 0;

    /* Reproduce the space left before label_color's final row. Fill with raw
     * color words so optimizer deduplication cannot remove the prefix. */
    EVE_HalContext * phost = unit->hal;
    lv_draw_eve5_dl_start(unit);
    for(unsigned i = 0; i < 705; i++) EVE_CoCmd_dl(phost, COLOR_RGB(255, 255, 255));
    lv_draw_eve5_slice_t slice = {0};
    lv_draw_eve5_budget_t budget = {0};
    budget.reserve = EVE5_DL_FINISH_NORT;
    budget.can_split = true;
    TEST_ASSERT_EQUAL_INT(1, lv_draw_eve5_render_tasks(unit, &layer, true, true, &slice, &budget));
    TEST_ASSERT_NULL(budget.stop);
    TEST_ASSERT_EQUAL(LV_DRAW_TASK_STATE_FINISHED, task.state);
    TEST_ASSERT_TRUE(EVE_Cmd_waitFlush(phost));
    TEST_ASSERT_GREATER_THAN_UINT32(705, EVE_Hal_rd32(phost, REG_CMD_DL) / 4);
    TEST_ASSERT_LESS_THAN_UINT32(1024, EVE_Hal_rd32(phost, REG_CMD_DL) / 4);
}

void test_dynamic_fmt_txt_label_uses_bitmap_bound(void)
{
    lv_binfont_dsc_t source = {
        .path = "A:src/test_assets/test_font_1.fnt",
        .dynamic_glyph_load = true,
    };
    loaded_font = lv_binfont_create_ex(&source);
    TEST_ASSERT_NOT_NULL(loaded_font);
    label.font = loaded_font;
    label.text = "Hello";
    task.area = task.clip_area = (lv_area_t) {20, 20, 300, 100};
    TEST_ASSERT_LESS_THAN_UINT32(128, lv_draw_eve5_label_dl_bound(&task, false));
    assert_render_bound();
}

void test_tiny_ttf_labels_fit_together_after_refinement(void)
{
    tiny_font = lv_tiny_ttf_create_file("A:src/test_assets/test_gpos_one.ttf", 60);
    TEST_ASSERT_NOT_NULL(tiny_font);
    label.font = tiny_font;
    label.text = "ĄJ Ąg Ąģ Ąj Ąȷ Qȷ ąj ąȷ gȷ ģȷ ıȷ ųȷ vȷ Va Vá Vą Vf Vﬂ V.";
    label.line_space = 0;
    task.area = task.clip_area = (lv_area_t) {50, 20, 749, 220};
    TEST_ASSERT_GREATER_THAN_UINT32(2048, lv_draw_eve5_label_dl_bound(&task, false));
    TEST_ASSERT_LESS_THAN_UINT32(1024, lv_draw_eve5_label_dl_bound(&task, true));
    assert_render_bound();
    label.rotation = 450;
    assert_render_bound();
    label.rotation = 0;

    lv_draw_task_t next = task;
    next.area = next.clip_area = (lv_area_t) {50, 250, 749, 450};
    task.next = &next;
    lv_draw_eve5_dl_start(unit);
    EVE_HalContext * phost = unit->hal;
    for(unsigned i = 0; i < 8; i++) EVE_CoCmd_dl(phost, COLOR_RGB(255, 255, 255));
    lv_draw_eve5_slice_t slice = {0};
    lv_draw_eve5_budget_t budget = {0};
    budget.reserve = EVE5_DL_FINISH_NORT;
    budget.can_split = true;
    TEST_ASSERT_EQUAL_INT(2, lv_draw_eve5_render_tasks(unit, &layer, true, true, &slice, &budget));
    TEST_ASSERT_NULL(budget.stop);
    TEST_ASSERT_EQUAL(LV_DRAW_TASK_STATE_FINISHED, next.state);
    TEST_ASSERT_TRUE(EVE_Cmd_waitFlush(phost));
    TEST_ASSERT_GREATER_THAN_UINT32(8, EVE_Hal_rd32(phost, REG_CMD_DL) / 4);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(2048 - EVE5_DL_FINISH_NORT, EVE_Hal_rd32(phost, REG_CMD_DL) / 4);
    task.next = NULL;
}

void test_binary_font_with_tiny_ttf_fallback_bound(void)
{
    loaded_font = lv_binfont_create("A:src/test_assets/test_font_1.fnt");
    tiny_font = lv_tiny_ttf_create_file("A:src/test_files/fonts/noto/NotoSansSC-Regular.ttf", 24);
    TEST_ASSERT_NOT_NULL(loaded_font);
    TEST_ASSERT_NOT_NULL(tiny_font);
    loaded_font->fallback = tiny_font;
    label.font = loaded_font;
    label.text = "这是一段中文。\nThis is a English text.\nSymbols: " LV_SYMBOL_OK LV_SYMBOL_CLOSE;
    label.line_space = 0;
    task.area = task.clip_area = (lv_area_t) {20, 20, 700, 200};
    lv_font_glyph_dsc_t glyph;
    TEST_ASSERT_TRUE(lv_font_get_glyph_dsc(label.font, &glyph, 0x8FD9, 0));
    TEST_ASSERT_EQUAL_PTR(tiny_font, glyph.resolved_font);
    TEST_ASSERT_EQUAL(LV_FONT_GLYPH_FORMAT_A8, glyph.format);
    TEST_ASSERT_GREATER_THAN_UINT32(2048, lv_draw_eve5_label_dl_bound(&task, false));
    TEST_ASSERT_LESS_THAN_UINT32(1024, lv_draw_eve5_label_dl_bound(&task, true));
    assert_render_bound();
}

void test_freetype_bound_preserves_color_image_glyphs(void)
{
    freetype_font = lv_freetype_font_create("./src/test_files/fonts/noto/NotoSansSC-Regular.ttf",
                                          LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 24, LV_FREETYPE_FONT_STYLE_NORMAL);
    emoji_font = lv_freetype_font_create("../examples/libs/freetype/NotoColorEmoji-32.subset.ttf",
                                       LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 12, LV_FREETYPE_FONT_STYLE_NORMAL);
    TEST_ASSERT_NOT_NULL(freetype_font);
    TEST_ASSERT_NOT_NULL(emoji_font);
    emoji_font->fallback = freetype_font;
    label.font = emoji_font;
    label.text = "这是一段中文。\nFreeType bitmap glyphs and color emoji: 😀 😀 😀";
    label.line_space = 0;
    task.area = task.clip_area = (lv_area_t) {20, 20, 780, 200};
    lv_font_glyph_dsc_t glyph;
    TEST_ASSERT_TRUE(lv_font_get_glyph_dsc(label.font, &glyph, 'F', 0));
    TEST_ASSERT_EQUAL_PTR(freetype_font, glyph.resolved_font);
    TEST_ASSERT_EQUAL(LV_FONT_GLYPH_FORMAT_A8, glyph.format);
    TEST_ASSERT_TRUE(lv_font_get_glyph_dsc(label.font, &glyph, 0x1F600, 0));
    TEST_ASSERT_EQUAL_PTR(emoji_font, glyph.resolved_font);
    TEST_ASSERT_EQUAL(LV_FONT_GLYPH_FORMAT_IMAGE, glyph.format);
    TEST_ASSERT_GREATER_THAN_UINT32(2048, lv_draw_eve5_label_dl_bound(&task, false));
    TEST_ASSERT_LESS_THAN_UINT32(1024, lv_draw_eve5_label_dl_bound(&task, true));
    assert_render_bound();
    label.rotation = 450;
    assert_render_bound();
}

static const void * image_glyph_path(const lv_font_t * font, uint32_t letter, uint32_t next,
                                   int32_t * offset_y, void * user_data)
{
    LV_UNUSED(font);
    LV_UNUSED(next);
    LV_UNUSED(offset_y);
    LV_UNUSED(user_data);
    LV_IMAGE_DECLARE(emoji_F617);
    return letter == 0xF617 ? &emoji_F617 : NULL;
}

void test_compressed_label_keeps_image_glyph_fallback_bound(void)
{
    LV_FONT_DECLARE(test_font_montserrat_ascii_3bpp_compressed);
    lv_font_t compressed = test_font_montserrat_ascii_3bpp_compressed;
    lv_font_t fallback = lv_font_montserrat_16;
    compressed.fallback = &fallback;
    fallback.fallback = NULL;
    label.font = &compressed;
    label.text = "a\xEF\x98\x97z";
    task.area = task.clip_area = (lv_area_t) {20, 20, 300, 120};
    uint32_t without_image = lv_draw_eve5_label_dl_bound(&task, false);
    image_font = lv_imgfont_create(80, image_glyph_path, NULL);
    TEST_ASSERT_NOT_NULL(image_font);
    fallback.fallback = image_font;
    lv_font_glyph_dsc_t glyph;
    TEST_ASSERT_TRUE(lv_font_get_glyph_dsc(label.font, &glyph, 0xF617, 0));
    TEST_ASSERT_EQUAL(LV_FONT_GLYPH_FORMAT_IMAGE, glyph.format);
    TEST_ASSERT_GREATER_THAN_UINT32(without_image, lv_draw_eve5_label_dl_bound(&task, false));
    assert_render_bound();
    label.rotation = 450;
    assert_render_bound();
}
#endif
