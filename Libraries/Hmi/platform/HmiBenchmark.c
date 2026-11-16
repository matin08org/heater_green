#include "HmiBenchmark.h"
#include "stm32f4xx.h"
HmiResult HmiBenchmark_Image2(HmiSurface *s, const HmiImage2 *i,
                             HmiRect source, const HmiPalette2 *p, HmiImageTiming *timing)
{
    if (!timing) return HMI_INVALID;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB(); __ISB();
    uint32_t start = DWT->CYCCNT;
    HmiResult result = HmiImage2_Draw(s,i,source,0,0,p);
    timing->cycles = DWT->CYCCNT - start;
    timing->cpu_hz = SystemCoreClock;
    return result;
}
