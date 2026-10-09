/**
 * @file lv_test_eve5.c
 *
 */

#if LV_BUILD_TEST
#include "lv_test_eve5.h"

#if LV_USE_EVE5

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../unity/unity.h"
#include "../../src/drivers/display/eve5/lv_eve5.h"
#include "../../src/draw/eve5/lv_draw_eve5.h"
#include "../../src/draw/lv_draw_buf_private.h"
#if LV_USE_OS
    #include "../../src/osal/lv_os_private.h"
#endif

static EVE_HalContext s_host;
static EVE_GpuAlloc s_allocator;
static bool s_opened;
static uint32_t s_frame_layer_tasks_refused;
static uint32_t s_frame_layer_refusals;
static uint32_t s_last_render_layer_tasks_refused;
static uint32_t s_offscreen_layer_refusals;
static uint32_t s_frame_layer_fallbacks_before;
static uint32_t s_frame_layer_fallbacks;
static uint32_t s_last_render_layer_fallbacks;
static uint32_t s_offscreen_layer_fallbacks;
static uint32_t s_degraded_before[2];
static uint32_t s_degraded_frame[2];

static bool idle_cb(EVE_HalContext * phost);
static bool read_screen_cb(lv_display_t * disp, uint8_t * buf, uint32_t stride);
static void buf_track_install(void);
static void buf_track_report(void);
static void buf_track_refr_start_cb(lv_event_t * e);
static void frame_start_cb(lv_event_t * e);
static void frame_ready_cb(lv_event_t * e);

lv_display_t * lv_test_eve5_display_create(int32_t hor_res, int32_t ver_res)
{
    EVE_HalContext * phost = &s_host;

    /* Keep the output in order with the emulator's, and complete when a
     * sanitizer ends the process */
    setvbuf(stdout, NULL, _IOLBF, 0);

    /* Count the CPU memory of draw buffers, from the display's on */
    buf_track_install();

    EVE_Hal_initialize();
    if(!EVE_Util_openDeviceInteractive(phost, NULL)) {
        TEST_PRINTF("EVE5: could not open the EVE device");
        exit(EXIT_FAILURE);
    }
    s_opened = true;
    phost->CbCmdWait = idle_cb;

    /* The reference images are 800x480, the WVGA preset */
    EVE_BootupParameters bootup;
    EVE_Util_bootupDefaults(phost, &bootup);
    EVE_ConfigParameters config;
    if(!EVE_Util_bootup(phost, &bootup)) {
        TEST_PRINTF("EVE5: could not boot the EVE device");
        exit(EXIT_FAILURE);
    }
    EVE_Util_configDefaults(phost, &config, EVE_DISPLAY_WVGA_800x480_74Hz);
    if(!EVE_Util_config(phost, &config)) {
        TEST_PRINTF("EVE5: could not configure the EVE display");
        exit(EXIT_FAILURE);
    }
    if((int32_t)phost->Width != hor_res || (int32_t)phost->Height != ver_res) {
        TEST_PRINTF("EVE5: the display is %dx%d instead of %dx%d", (int)phost->Width, (int)phost->Height,
                    (int)hor_res, (int)ver_res);
        exit(EXIT_FAILURE);
    }

    /* LV_TEST_EVE5_RENDER_MODE=full renders full frames into the double
     * buffered swapchain instead of tiles */
    const char * mode_env = getenv("LV_TEST_EVE5_RENDER_MODE");
    lv_eve5_render_mode_t mode = (mode_env && !strcmp(mode_env, "full")) ?
                                 LV_EVE5_RENDER_MODE_FULL : LV_EVE5_RENDER_MODE_PARTIAL;
    lv_display_t * disp = lv_eve5_create_ex(phost, &s_allocator, mode);
    if(disp == NULL) {
        TEST_PRINTF("EVE5: could not create the display");
        exit(EXIT_FAILURE);
    }
    lv_draw_eve5_init(phost, &s_allocator);

    /* The display driver sets EVE_millis as the tick source, but the tests
     * advance time themselves with lv_tick_inc() */
    lv_tick_set_cb(NULL);

    lv_test_screenshot_set_read_cb(read_screen_cb);
    lv_display_add_event_cb(disp, buf_track_refr_start_cb, LV_EVENT_REFR_START, NULL);
    lv_display_add_event_cb(disp, frame_start_cb, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(disp, frame_ready_cb, LV_EVENT_RENDER_READY, NULL);
    return disp;
}

void lv_test_eve5_case_begin(void)
{
    /* Canvases are often drawn before a screen refresh. Keep their refusals
     * across this case's captures, without attributing a previous case's
     * off-screen drawing to the next one. The displayed frame is unchanged. */
    s_last_render_layer_tasks_refused = lv_draw_eve5_get_stats()->layer_tasks_refused;
    s_offscreen_layer_refusals = 0;
    s_last_render_layer_fallbacks = lv_draw_get_layer_fallback_count(NULL);
    s_offscreen_layer_fallbacks = 0;
}

void lv_test_eve5_close(void)
{
    if(!s_opened) return;
    /* What the draw unit moved between CPU and EVE memory, for comparing
     * configurations, such as with and without VRAM residency */
    buf_track_report();
    const lv_draw_eve5_stats_t * stats = lv_draw_eve5_get_stats();
    printf("EVE5 stats: uploads=%" LV_PRIu32 " upload_bytes=%llu hw_decodes=%" LV_PRIu32
           " downloads=%" LV_PRIu32 " download_bytes=%llu hashes=%" LV_PRIu32 " hash_bytes=%llu"
           " layer_tasks_refused=%" LV_PRIu32 " degraded_tasks_1=%" LV_PRIu32 " degraded_tasks_2=%" LV_PRIu32 "\n",
           stats->uploads, (unsigned long long)stats->upload_bytes, stats->hw_decodes,
           stats->downloads, (unsigned long long)stats->download_bytes,
           stats->hashes, (unsigned long long)stats->hash_bytes, stats->layer_tasks_refused,
           stats->degraded_tasks[0], stats->degraded_tasks[1]);
    lv_draw_eve5_deinit();
    EVE_Hal_close(&s_host);
    EVE_Hal_release();
    s_opened = false;
}

/*
 * The CPU memory of draw buffers (layers, canvases, snapshots, decoded images,
 * glyphs, the display's buffers), counted through the allocation callbacks of
 * LVGL's draw buffer handlers, which this wraps. The sizes are what the
 * callbacks are asked for. The buffers a screenshot comparison decodes its
 * reference image into are counted apart: those allocated from the moment it
 * reads the frame (read_screen_cb) until they're all freed, or the next
 * refresh starts.
 */

typedef struct {
    void * ptr;
    size_t size;
    bool screenshot;
} buf_track_entry_t;

typedef struct {
    lv_draw_buf_malloc_cb_t malloc_cb;
    lv_draw_buf_free_cb_t free_cb;
} buf_track_handlers_t;

static struct {
    buf_track_entry_t * table;  /* Open addressing, capacity a power of two */
    size_t capacity;
    size_t count;
    uint64_t allocs, alloc_bytes, frees, free_bytes, live_bytes, peak_bytes;
    uint64_t screenshot_allocs, screenshot_bytes;
    uint32_t screenshot_live;
    bool in_screenshot;
    buf_track_handlers_t orig[3];
#if LV_USE_OS
    lv_mutex_t mutex;
#endif
} s_buf_track;

static size_t buf_track_slot(const void * ptr)
{
    uintptr_t k = (uintptr_t)ptr;
    k ^= k >> 17;
    k *= 0x9E3779B97F4A7C15ULL;
    return (size_t)(k >> 20) & (s_buf_track.capacity - 1);
}

static void buf_track_insert(void * ptr, size_t size, bool screenshot)
{
    if(s_buf_track.count * 4 >= s_buf_track.capacity * 3) {
        buf_track_entry_t * old = s_buf_track.table;
        size_t old_capacity = s_buf_track.capacity;
        s_buf_track.capacity = old_capacity ? old_capacity * 2 : 4096;
        s_buf_track.table = calloc(s_buf_track.capacity, sizeof(buf_track_entry_t));
        s_buf_track.count = 0;
        for(size_t i = 0; i < old_capacity; i++) {
            if(old[i].ptr) buf_track_insert(old[i].ptr, old[i].size, old[i].screenshot);
        }
        free(old);
    }
    size_t i = buf_track_slot(ptr);
    while(s_buf_track.table[i].ptr) i = (i + 1) & (s_buf_track.capacity - 1);
    s_buf_track.table[i].ptr = ptr;
    s_buf_track.table[i].size = size;
    s_buf_track.table[i].screenshot = screenshot;
    s_buf_track.count++;
}

/* The size of a tracked pointer, which is forgotten, or 0 for another */
static size_t buf_track_remove(void * ptr, bool * screenshot)
{
    if(s_buf_track.capacity == 0) return 0;
    size_t mask = s_buf_track.capacity - 1;
    size_t i = buf_track_slot(ptr);
    while(s_buf_track.table[i].ptr != ptr) {
        if(s_buf_track.table[i].ptr == NULL) return 0;
        i = (i + 1) & mask;
    }
    size_t size = s_buf_track.table[i].size;
    *screenshot = s_buf_track.table[i].screenshot;
    /* Move up the entries that follow in the run */
    size_t hole = i;
    for(size_t j = (i + 1) & mask; s_buf_track.table[j].ptr != NULL; j = (j + 1) & mask) {
        size_t home = buf_track_slot(s_buf_track.table[j].ptr);
        if(((j - home) & mask) >= ((j - hole) & mask)) {
            s_buf_track.table[hole] = s_buf_track.table[j];
            hole = j;
        }
    }
    s_buf_track.table[hole].ptr = NULL;
    s_buf_track.count--;
    return size;
}

static void * buf_track_malloc(int set, size_t size, lv_color_format_t cf)
{
    void * ptr = s_buf_track.orig[set].malloc_cb(size, cf);
    if(ptr == NULL) return NULL;
#if LV_USE_OS
    lv_mutex_lock(&s_buf_track.mutex);
#endif
    buf_track_insert(ptr, size, s_buf_track.in_screenshot);
    if(s_buf_track.in_screenshot) {
        s_buf_track.screenshot_allocs++;
        s_buf_track.screenshot_bytes += size;
        s_buf_track.screenshot_live++;
    }
    else {
        s_buf_track.allocs++;
        s_buf_track.alloc_bytes += size;
        s_buf_track.live_bytes += size;
        if(s_buf_track.live_bytes > s_buf_track.peak_bytes) s_buf_track.peak_bytes = s_buf_track.live_bytes;
    }
#if LV_USE_OS
    lv_mutex_unlock(&s_buf_track.mutex);
#endif
    return ptr;
}

static void buf_track_free(int set, void * ptr)
{
    if(ptr != NULL) {
#if LV_USE_OS
        lv_mutex_lock(&s_buf_track.mutex);
#endif
        bool screenshot = false;
        size_t size = buf_track_remove(ptr, &screenshot);
        if(screenshot) {
            /* The comparison is done when its buffers are */
            if(--s_buf_track.screenshot_live == 0) s_buf_track.in_screenshot = false;
        }
        else if(size != 0) {
            s_buf_track.frees++;
            s_buf_track.free_bytes += size;
            s_buf_track.live_bytes -= size;
        }
#if LV_USE_OS
        lv_mutex_unlock(&s_buf_track.mutex);
#endif
    }
    s_buf_track.orig[set].free_cb(ptr);
}

/* One pair of wrappers per handler set: the free callback isn't told the set */
#define BUF_TRACK_SET(n) \
    static void * buf_track_malloc_##n(size_t size, lv_color_format_t cf) { return buf_track_malloc(n, size, cf); } \
    static void buf_track_free_##n(void * ptr) { buf_track_free(n, ptr); }
BUF_TRACK_SET(0)
BUF_TRACK_SET(1)
BUF_TRACK_SET(2)

static void buf_track_install(void)
{
#if LV_USE_OS
    lv_mutex_init(&s_buf_track.mutex);
#endif
    lv_draw_buf_handlers_t * sets[3] = { lv_draw_buf_get_handlers(), lv_draw_buf_get_font_handlers(),
                                         lv_draw_buf_get_image_handlers()
                                       };
    lv_draw_buf_malloc_cb_t mallocs[3] = { buf_track_malloc_0, buf_track_malloc_1, buf_track_malloc_2 };
    lv_draw_buf_free_cb_t frees[3] = { buf_track_free_0, buf_track_free_1, buf_track_free_2 };
    for(int i = 0; i < 3; i++) {
        s_buf_track.orig[i].malloc_cb = sets[i]->buf_malloc_cb;
        s_buf_track.orig[i].free_cb = sets[i]->buf_free_cb;
        sets[i]->buf_malloc_cb = mallocs[i];
        sets[i]->buf_free_cb = frees[i];
    }
}

static void buf_track_refr_start_cb(lv_event_t * e)
{
    LV_UNUSED(e);
#if LV_USE_OS
    lv_mutex_lock(&s_buf_track.mutex);
#endif
    s_buf_track.in_screenshot = false;
#if LV_USE_OS
    lv_mutex_unlock(&s_buf_track.mutex);
#endif
}

/* After lv_deinit: what's still allocated was never freed */
static void buf_track_report(void)
{
    printf("EVE5 buffers: allocs=%llu alloc_bytes=%llu frees=%llu free_bytes=%llu peak_bytes=%llu live_bytes=%llu"
           " screenshot_allocs=%llu screenshot_bytes=%llu\n",
           (unsigned long long)s_buf_track.allocs, (unsigned long long)s_buf_track.alloc_bytes,
           (unsigned long long)s_buf_track.frees, (unsigned long long)s_buf_track.free_bytes,
           (unsigned long long)s_buf_track.peak_bytes, (unsigned long long)s_buf_track.live_bytes,
           (unsigned long long)s_buf_track.screenshot_allocs, (unsigned long long)s_buf_track.screenshot_bytes);
    free(s_buf_track.table);
    s_buf_track.table = NULL;
    s_buf_track.capacity = 0;
    s_buf_track.count = 0;
#if LV_USE_OS
    lv_mutex_delete(&s_buf_track.mutex);
#endif
}

static void frame_start_cb(lv_event_t * e)
{
    /* Empty refreshes keep the displayed frame and its diagnosis. Mark only
     * actual rendering starts so reports can scope overflow logs to captures. */
    uint32_t refused = lv_draw_eve5_get_stats()->layer_tasks_refused;
    s_offscreen_layer_refusals += refused - s_last_render_layer_tasks_refused;
    s_frame_layer_tasks_refused = refused;
    s_frame_layer_refusals = 0;
    uint32_t fallbacks = lv_draw_get_layer_fallback_count(lv_event_get_target(e));
    s_offscreen_layer_fallbacks += fallbacks - s_last_render_layer_fallbacks;
    s_frame_layer_fallbacks_before = fallbacks;
    s_frame_layer_fallbacks = 0;
    for(unsigned i = 0; i < 2; i++) {
        s_degraded_before[i] = lv_draw_eve5_get_stats()->degraded_tasks[i];
        s_degraded_frame[i] = 0;
    }
    printf("EVE5 frame: begin\n");
}

static void frame_ready_cb(lv_event_t * e)
{
    uint32_t refused = lv_draw_eve5_get_stats()->layer_tasks_refused;
    s_frame_layer_refusals = refused - s_frame_layer_tasks_refused;
    s_last_render_layer_tasks_refused = refused;
    uint32_t fallbacks = lv_draw_get_layer_fallback_count(lv_event_get_target(e));
    s_frame_layer_fallbacks = fallbacks - s_frame_layer_fallbacks_before;
    s_last_render_layer_fallbacks = fallbacks;
    for(unsigned i = 0; i < 2; i++) {
        s_degraded_frame[i] = lv_draw_eve5_get_stats()->degraded_tasks[i] - s_degraded_before[i];
    }
}

static bool read_screen_cb(lv_display_t * disp, uint8_t * buf, uint32_t stride)
{
    /* REFR_READY may invalidate the screen after NORT reclaimed assets from
     * the previous scanout. Drain those redraws before capturing, without
     * ticking time or forcing clean frames to render again. Also cover reads
     * through screenshot_compare_core and screenshot_read. */
    unsigned redraws = 0;
    while(lv_eve5_has_pending_refresh(disp) && redraws < 8) {
        lv_refr_now(disp);
        redraws++;
    }
    if(lv_eve5_has_pending_refresh(disp)) {
        printf("EVE5 capture: did not settle after %u redraws\n", redraws);
        return false;
    }

    /* A screenshot comparison: the buffers it decodes the reference into
     * aren't the test's */
#if LV_USE_OS
    lv_mutex_lock(&s_buf_track.mutex);
#endif
    s_buf_track.in_screenshot = true;
#if LV_USE_OS
    lv_mutex_unlock(&s_buf_track.mutex);
#endif
    bool ok = lv_eve5_read_screen(disp, buf, stride);
    if(ok) {
        printf("EVE5 capture: degraded_tasks_1=%" LV_PRIu32 " degraded_tasks_2=%" LV_PRIu32 "\n",
               s_degraded_frame[0], s_degraded_frame[1]);
        printf("EVE5 capture: settled redraws=%u alloc_failures=%u\n", redraws,
               (unsigned)EVE_GpuAlloc_GetAllocFailedCount(lv_eve5_get_allocator(disp)));
        /* Keep lifetime transfer totals intact. Recovery redraws replace this
         * baseline, while empty refreshes and repeated captures preserve it. */
        printf("EVE5 capture: layer_tasks_refused=%" LV_PRIu32 " offscreen_layer_tasks_refused=%" LV_PRIu32
               " layer_effects_skipped=%" LV_PRIu32 " offscreen_layer_effects_skipped=%" LV_PRIu32 "\n",
               s_frame_layer_refusals, s_offscreen_layer_refusals +
               lv_draw_eve5_get_stats()->layer_tasks_refused - s_last_render_layer_tasks_refused,
               s_frame_layer_fallbacks, s_offscreen_layer_fallbacks +
               lv_draw_get_layer_fallback_count(disp) - s_last_render_layer_fallbacks);
    }
    return ok;
}

static bool idle_cb(EVE_HalContext * phost)
{
    /* Free the GPU memory whose last use the coprocessor has completed */
    EVE_GpuAlloc_UpdateFree(&s_allocator, EVE_Cmd_syncCompleted(phost));
    return true;
}

#endif /*LV_USE_EVE5*/
#endif /*LV_BUILD_TEST*/
