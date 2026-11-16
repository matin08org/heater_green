#ifndef HMI_ST7796_H
#define HMI_ST7796_H
#include "HmiStream.h"
#include "spi.h"
/* Dedicated owner of SPI1 in the new HMI. Do not run this together with
 * lcdPort, st7796.c or LCDPatternBench. GPIO/DMA/SPI MX init stays unchanged.
 * Commands and pixels both use PCLK2/4 = 16MHz on this 64MHz board. */
typedef struct {
    volatile uint8_t dma_done, dma_error;
    uint8_t phase, init_step, part;
    bool transferring;
    uint32_t deadline, transfer_started, errors;
    HmiRect rectangle;
    const uint16_t *pixels;
    uint8_t command, parameters[4];
} HmiSt7796;
/* Exactly one hardware instance. Context must outlive all DMA transfers. */
HmiResult HmiSt7796_Init(HmiSt7796 *lcd);
HmiTransport HmiSt7796_Transport(HmiSt7796 *lcd);
bool HmiSt7796_Ready(const HmiSt7796 *lcd);
#endif
