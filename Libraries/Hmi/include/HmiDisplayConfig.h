#ifndef HMI_DISPLAY_CONFIG_H
#define HMI_DISPLAY_CONFIG_H
/* A two-row buffer required 160 DMA/window sequences per full page. Sixteen
 * rows reduce this to twenty while keeping bounded rendering and no framebuffer.
 * 2 x 480 x 16 x 2 = 30,720 bytes; STM32F412 has 256 KiB SRAM. */
#define HMI_DISPLAY_WIDTH 480U
#define HMI_DISPLAY_HEIGHT 320U
#define HMI_DISPLAY_TILE_ROWS 16U
#define HMI_DISPLAY_BUFFER_PIXELS (HMI_DISPLAY_WIDTH*HMI_DISPLAY_TILE_ROWS)
/* Temporary visual test: red border for 500 ms, original border for 500 ms. */
#define HMI_DISPLAY_TEST_TARGET_BLINK 0
#define HMI_DISPLAY_TEST_BLINK_HALF_MS 500U
/* Temporary manual PA8 test: Home UP/DOWN adjust CCR1 by 100, 0..1000. */
#define HMI_DISPLAY_TEST_PWM_CONTROL 0
#endif
