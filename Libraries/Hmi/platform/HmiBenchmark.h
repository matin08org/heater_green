#ifndef HMI_BENCHMARK_H
#define HMI_BENCHMARK_H
#include "HmiImage.h"
typedef struct { uint32_t cycles, cpu_hz; } HmiImageTiming;
/* Optional board benchmark. Executes exactly ONE conversion, with interrupts
 * enabled, no SPI transfer and no loops waiting for hardware. Existing DWT
 * timing (MAX6675 etc.) is preserved: CYCCNT is never reset or disabled.
 * microseconds = cycles * 1000000 / cpu_hz, calculated outside measured code. */
HmiResult HmiBenchmark_Image2(HmiSurface *surface, const HmiImage2 *image,
                             HmiRect source, const HmiPalette2 *palette,
                             HmiImageTiming *timing);
#endif
