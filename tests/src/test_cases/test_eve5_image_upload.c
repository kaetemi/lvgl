#if LV_BUILD_TEST
#include "../lvgl.h"
#include "unity/unity.h"
#include "src/drivers/display/eve5/lv_eve5.h"
#include "src/drivers/display/eve5/lv_eve5_image_private.h"
#include "src/draw/eve5/lv_draw_eve5_private.h"

static lv_draw_eve5_unit_t * unit;
static lv_image_dsc_t image;
static lv_eve5_vram_res_t * uploaded;
static EVE_GpuHandle decoded;

void setUp(void)
{
    unit = lv_malloc_zeroed(sizeof(*unit));
    TEST_ASSERT_NOT_NULL(unit);
    unit->hal = lv_eve5_get_hal(lv_display_get_default());
    unit->allocator = lv_eve5_get_allocator(lv_display_get_default());
    lv_memzero(&image, sizeof(image));
    uploaded = NULL;
    decoded = GA_HANDLE_INVALID;
    TEST_ASSERT_TRUE(EVE_Cmd_waitFlush(unit->hal));
}

void tearDown(void)
{
    if(uploaded) {
        EVE_GpuAlloc_Free(unit->allocator, uploaded->gpu_handle);
        lv_draw_eve5_res_set(unit, &image, NULL);
        lv_draw_eve5_res_destroy(uploaded);
    }
    EVE_GpuAlloc_Free(unit->allocator, decoded);
    lv_free(unit);
}

static void assert_format(EVE_HalContext * hal, lv_color_format_t cf,
                          uint16_t format, uint8_t bits, bool conversion)
{
    uint16_t actual_format;
    uint8_t actual_bits;
    bool actual_conversion;
    TEST_ASSERT_TRUE(lv_draw_eve5_get_eve_format_info(hal, cf, &actual_format, &actual_bits, &actual_conversion));
    TEST_ASSERT_EQUAL_UINT16(format, actual_format);
    TEST_ASSERT_EQUAL_UINT8(bits, actual_bits);
    TEST_ASSERT_EQUAL(conversion, actual_conversion);
}

static void assert_mapping(EVE_HalContext * hal, bool truecolor, bool extended)
{
    uint16_t rgb = RGB565, argb = ARGB4, indexed = ARGB4;
    uint8_t rgb_bits = 16, argb_bits = 16, indexed_bits = 16;
#ifdef EVE_SUPPORT_TRUECOLOR
    if(truecolor) {
        rgb = RGB8;
        argb = ARGB8;
        rgb_bits = 24;
        argb_bits = 32;
    }
#endif
#if EVE_SUPPORT_CHIPID >= EVE_BT820
    if(extended) {
        indexed = PALETTEDARGB8;
        indexed_bits = 8;
        assert_format(hal, LV_COLOR_FORMAT_AL88, LA8, 16, false);
    }
#endif
    if(!extended) {
        uint16_t fmt;
        uint8_t bpp;
        bool converts;
        TEST_ASSERT_FALSE(lv_draw_eve5_get_eve_format_info(hal, LV_COLOR_FORMAT_AL88, &fmt, &bpp, &converts));
    }
    assert_format(hal, LV_COLOR_FORMAT_RGB888, rgb, rgb_bits, !truecolor);
    assert_format(hal, LV_COLOR_FORMAT_XRGB8888, rgb, rgb_bits, true);
    assert_format(hal, LV_COLOR_FORMAT_ARGB8888, argb, argb_bits, !truecolor);
    assert_format(hal, LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED, argb, argb_bits, !truecolor);
    assert_format(hal, LV_COLOR_FORMAT_RGB565A8, argb, argb_bits, true);
    assert_format(hal, LV_COLOR_FORMAT_I4, indexed, indexed_bits, true);
}

void test_eve5_cpu_formats_follow_truecolor_capability(void)
{
    assert_mapping(unit->hal, EVE_Hal_supportTrueColor(unit->hal), EVE_Hal_supportRenderTarget(unit->hal));
#if defined(EVE_SUPPORT_TRUECOLOR) && EVE5_HW_IMAGE_DECODE
    TEST_ASSERT_TRUE(lv_draw_eve5_lvgl_bin_cf_supported(LV_COLOR_FORMAT_RGB888));
    TEST_ASSERT_TRUE(lv_draw_eve5_lvgl_bin_cf_supported(LV_COLOR_FORMAT_ARGB8888));
    TEST_ASSERT_TRUE(lv_draw_eve5_lvgl_bin_cf_supported(LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED));
#endif
#ifdef EVE_MULTI_GRAPHICS_TARGET
    /* Mapping is pure: check all three capabilities without changing the device. */
    EVE_HalContext hal = *unit->hal;
    hal.ChipId = EVE_BT817;
    assert_mapping(&hal, false, false);
    hal.ChipId = EVE_BT819;
    assert_mapping(&hal, true, false);
    hal.ChipId = EVE_BT820;
    assert_mapping(&hal, true, true);
#endif
}

static void upload(lv_color_format_t cf, uint8_t * bytes, uint32_t size, uint32_t stride,
                   uint32_t flags, bool premultiply)
{
    if(!EVE_Hal_supportTrueColor(unit->hal)) TEST_IGNORE_MESSAGE("Requires truecolor bitmap support");
    image.header.magic = LV_IMAGE_HEADER_MAGIC;
    image.header.cf = cf;
    image.header.w = 3;
    image.header.h = 3;
    image.header.stride = stride;
    image.header.flags = flags;
    image.data = bytes;
    image.data_size = size;
    uploaded = lv_draw_eve5_upload_image_to_gpu_ex(unit, &image, false, premultiply);
    TEST_ASSERT_NOT_NULL(uploaded);
    TEST_ASSERT_EQUAL_UINT32(GA_INVALID, uploaded->palette_offset);
}

static void read_gpu(uint8_t * bytes)
{
    uint32_t addr = EVE_GpuAlloc_Get(unit->allocator, uploaded->gpu_handle);
    TEST_ASSERT_NOT_EQUAL(GA_INVALID, addr);
    EVE_Hal_rdMem(unit->hal, bytes, addr + uploaded->source_offset, uploaded->base.size);
}

static void roundtrip(uint8_t * bytes, uint32_t size)
{
    lv_draw_buf_t buf;
    lv_memzero(&buf, sizeof(buf));
    buf.header = image.header;
    buf.data = bytes;
    buf.data_size = size;
    TEST_ASSERT_TRUE(lv_draw_eve5_download_image(unit, &buf, uploaded));
}

void test_eve5_cpu_rgb888_preserves_odd_width_rows(void)
{
#ifdef EVE_SUPPORT_TRUECOLOR
    uint8_t src[48], gpu[48], dst[48];
    for(unsigned i = 0; i < sizeof(src); i++) src[i] = (uint8_t)(i * 7 + 3);
    upload(LV_COLOR_FORMAT_RGB888, src, sizeof(src), 16, 0, false);
    TEST_ASSERT_EQUAL_UINT16(RGB8, uploaded->eve_format);
    TEST_ASSERT_EQUAL_UINT32(9, uploaded->stride);
    TEST_ASSERT_EQUAL_UINT32(28, uploaded->base.size);
    TEST_ASSERT_EQUAL_INT(3, eve5_format_bpp(uploaded->eve_format));
    TEST_ASSERT_FALSE(lv_draw_eve5_format_has_alpha(uploaded->eve_format));
    read_gpu(gpu);
    lv_memset(dst, 0xCD, sizeof(dst));
    roundtrip(dst, sizeof(dst));
    for(unsigned y = 0; y < 3; y++) {
        TEST_ASSERT_EQUAL_HEX8_ARRAY(src + y * 16, gpu + y * 9, 9);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(src + y * 16, dst + y * 16, 9);
        TEST_ASSERT_EQUAL_HEX8(0xCD, dst[y * 16 + 9]);
    }
#endif
}

void test_eve5_cpu_xrgb8888_removes_only_x(void)
{
#ifdef EVE_SUPPORT_TRUECOLOR
    uint8_t src[48], gpu[48], dst[48];
    for(unsigned i = 0; i < sizeof(src); i++) src[i] = (uint8_t)(i * 5 + 1);
    upload(LV_COLOR_FORMAT_XRGB8888, src, sizeof(src), 16, 0, false);
    TEST_ASSERT_EQUAL_UINT16(RGB8, uploaded->eve_format);
    TEST_ASSERT_EQUAL_UINT32(9, uploaded->stride);
    read_gpu(gpu);
    roundtrip(dst, sizeof(dst));
    for(unsigned y = 0; y < 3; y++) {
        for(unsigned x = 0; x < 3; x++) {
            TEST_ASSERT_EQUAL_HEX8_ARRAY(src + y * 16 + x * 4, gpu + y * 9 + x * 3, 3);
            TEST_ASSERT_EQUAL_HEX8_ARRAY(src + y * 16 + x * 4, dst + y * 16 + x * 4, 3);
            TEST_ASSERT_EQUAL_HEX8(255, dst[y * 16 + x * 4 + 3]);
        }
    }
#endif
}

static void check_argb(lv_color_format_t cf, uint32_t flags, bool premultiply)
{
#ifdef EVE_SUPPORT_TRUECOLOR
    uint8_t src[48], gpu[48], dst[48];
    for(unsigned i = 0; i < sizeof(src); i++) src[i] = (uint8_t)(i * 5 + 3);
    upload(cf, src, sizeof(src), 16, flags, premultiply);
    TEST_ASSERT_EQUAL_UINT16(ARGB8, uploaded->eve_format);
    TEST_ASSERT_EQUAL_UINT32(12, uploaded->stride);
    TEST_ASSERT_EQUAL_UINT32(36, uploaded->base.size);
    TEST_ASSERT_EQUAL_INT(4, eve5_format_bpp(uploaded->eve_format));
    TEST_ASSERT_TRUE(lv_draw_eve5_format_has_alpha(uploaded->eve_format));
    TEST_ASSERT_EQUAL(premultiply || cf == LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED || flags != 0,
                      uploaded->is_premultiplied);
    read_gpu(gpu);
    roundtrip(dst, sizeof(dst));
    for(unsigned y = 0; y < 3; y++) {
        for(unsigned x = 0; x < 3; x++) {
            const uint8_t * s = src + y * 16 + x * 4;
            const uint8_t * g = gpu + y * 12 + x * 4;
            const uint8_t * d = dst + y * 16 + x * 4;
            TEST_ASSERT_EQUAL_HEX8(s[3], g[3]);
            TEST_ASSERT_EQUAL_HEX8(s[3], d[3]);
            for(unsigned c = 0; c < 3; c++) {
                if(premultiply && !flags && cf != LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED) {
                    TEST_ASSERT_EQUAL_UINT8((s[c] * s[3] + 127) / 255, g[c]);
                    /* Lossy unpremultiplication must reproduce the same GPU pixel on reupload. */
                    TEST_ASSERT_EQUAL_UINT8(g[c], (d[c] * d[3] + 127) / 255);
                }
                else {
                    TEST_ASSERT_EQUAL_HEX8(s[c], g[c]);
                    TEST_ASSERT_EQUAL_HEX8(s[c], d[c]);
                }
            }
        }
    }
#else
    LV_UNUSED(cf);
    LV_UNUSED(flags);
    LV_UNUSED(premultiply);
#endif
}

void test_eve5_cpu_argb8888_preserves_color_and_alpha(void)
{
    check_argb(LV_COLOR_FORMAT_ARGB8888, 0, false);
}

void test_eve5_cpu_argb8888_premultiplies_and_reads_back(void)
{
    check_argb(LV_COLOR_FORMAT_ARGB8888, 0, true);
}

void test_eve5_cpu_premultiplied_format_is_not_multiplied_twice(void)
{
    check_argb(LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED, 0, true);
}

void test_eve5_cpu_premultiplied_flag_is_not_multiplied_twice(void)
{
    check_argb(LV_COLOR_FORMAT_ARGB8888, LV_IMAGE_FLAGS_PREMULTIPLIED, true);
}

void test_eve5_cpu_rgb565a8_preserves_separate_alpha_plane(void)
{
#ifdef EVE_SUPPORT_TRUECOLOR
    uint16_t src_words[18];
    uint8_t * src = (uint8_t *)src_words;
    uint8_t gpu[48], dst[36];
    for(unsigned i = 0; i < sizeof(src_words); i++) src[i] = (uint8_t)(i * 7 + 1);
    upload(LV_COLOR_FORMAT_RGB565A8, src, sizeof(src_words), 8, 0, false);
    TEST_ASSERT_EQUAL_UINT16(ARGB8, uploaded->eve_format);
    TEST_ASSERT_EQUAL_UINT32(12, uploaded->stride);
    read_gpu(gpu);
    roundtrip(dst, sizeof(dst));
    for(unsigned y = 0; y < 3; y++) {
        TEST_ASSERT_EQUAL_HEX8_ARRAY(src + y * 8, dst + y * 8, 6);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(src + 24 + y * 4, dst + 24 + y * 4, 3);
        for(unsigned x = 0; x < 3; x++) {
            TEST_ASSERT_EQUAL_HEX8(src[24 + y * 4 + x], gpu[y * 12 + x * 4 + 3]);
        }
    }
#endif
}

#if EVE5_HW_IMAGE_DECODE && (EVE_SUPPORT_CHIPID >= EVE_BT817)
static void check_hardware_decode(const char * path, uint32_t expected_format, uint32_t bytes_per_pixel,
                                  bool paletted)
{
    EVE_HalContext * phost = unit->hal;
    if(EVE_CHIPID < EVE_BT817) TEST_IGNORE_MESSAGE("GETIMAGE requires BT817+");
    uint32_t addr, palette;
    uint16_t fmt;
    int32_t stride, w, h;
    TEST_ASSERT_TRUE(lv_draw_eve5_try_load_file_image(unit, path,
                                                    &addr, &fmt, &stride, &w, &h, &decoded, &palette));
    /* The loader releases its MediaFIFO staging buffer on return. Pixel data
     * must already be complete, before another coprocessor command is sent. */
    uint32_t size = (uint32_t)stride * h;
    uint8_t * pixels = lv_malloc(size * 2);
    TEST_ASSERT_NOT_NULL(pixels);
    EVE_Hal_rdMem(phost, pixels, addr, size);
    /* Inspect the actual decode, independently of the loader's probe metadata. */
    uint32_t actual_source, actual_format, actual_w, actual_h, actual_palette;
    TEST_ASSERT_TRUE(EVE_CoCmd_getImage(phost, &actual_source, &actual_format, &actual_w, &actual_h,
                                      &actual_palette));
    EVE_Hal_rdMem(phost, pixels + size, addr, size);
    bool complete = lv_memcmp(pixels, pixels + size, size) == 0;
    lv_free(pixels);
    TEST_ASSERT_TRUE_MESSAGE(complete, "Decoded pixels changed after the loader returned");
    TEST_ASSERT_EQUAL_UINT32(expected_format, actual_format);
    TEST_ASSERT_EQUAL_UINT32(actual_format, fmt);
    TEST_ASSERT_EQUAL_UINT32(actual_source, addr);
    TEST_ASSERT_EQUAL_UINT32(actual_w, w);
    TEST_ASSERT_EQUAL_UINT32(actual_h, h);
    TEST_ASSERT_EQUAL_UINT32(actual_w * bytes_per_pixel, stride);
    if(paletted) TEST_ASSERT_EQUAL_UINT32(actual_palette, palette);
    else TEST_ASSERT_EQUAL_UINT32(GA_INVALID, palette);

    /* The decoder's info callback must describe the same rows before open. */
    lv_image_header_t header;
    TEST_ASSERT_EQUAL(LV_RESULT_OK, lv_image_decoder_get_info(path, &header));
    TEST_ASSERT_EQUAL_UINT32(actual_w, header.w);
    TEST_ASSERT_EQUAL_UINT32(actual_h, header.h);
    TEST_ASSERT_EQUAL_UINT32(stride, header.stride);
}
#endif

void test_eve5_hardware_jpeg_keeps_existing_decode_format(void)
{
#if EVE5_HW_IMAGE_DECODE && (EVE_SUPPORT_CHIPID >= EVE_BT817)
    uint32_t format = RGB565, bytes_per_pixel = 2;
    if(EVE_Hal_supportRenderTarget(unit->hal)) {
#ifdef EVE_SUPPORT_TRUECOLOR
        format = ARGB8;
        bytes_per_pixel = 4;
#endif
    }
    check_hardware_decode("A:src/test_assets/test_img_lvgl_logo.jpg", format, bytes_per_pixel, false);
#endif
}

void test_eve5_hardware_png_metadata_matches_decode(void)
{
#if EVE5_HW_IMAGE_DECODE && (EVE_SUPPORT_CHIPID >= EVE_BT817)
    uint32_t format = ARGB4, bytes_per_pixel = 2;
    if(EVE_Hal_supportRenderTarget(unit->hal)) {
#ifdef EVE_SUPPORT_TRUECOLOR
        format = ARGB8;
        bytes_per_pixel = 4;
#endif
    }
    check_hardware_decode("A:src/test_assets/test_img_lvgl_logo.png", format, bytes_per_pixel, false);
#endif
}

void test_eve5_hardware_indexed_png_metadata_matches_decode(void)
{
#if EVE5_HW_IMAGE_DECODE && (EVE_SUPPORT_CHIPID >= EVE_BT817)
    uint32_t format = PALETTED4444;
#if EVE_SUPPORT_CHIPID >= EVE_BT820
    if(EVE_Hal_supportRenderTarget(unit->hal)) {
        format = PALETTEDARGB8;
    }
#endif
    check_hardware_decode("A:src/test_assets/test_img_lvgl_logo_8bit_palette.png", format, 1, true);
#endif
}
#endif
