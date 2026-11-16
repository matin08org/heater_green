#include "HmiSt7796.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
MockSPI mock_spi;
static MockDMA mock_dma;
static DMA_HandleTypeDef dma={.Instance=&mock_dma};
SPI_HandleTypeDef hspi1={.Instance=&mock_spi,.hdmatx=&dma};
static uint32_t tick,pins,clock_hz=64000000U;
static unsigned sends,command_count,pixel_count;
static const uint8_t *source;
static uint16_t length;
static uint8_t command,window_bytes[8];
static bool sending,init_finished,fail_start;
uint32_t HAL_GetTick(void) { return tick; }
uint32_t HAL_RCC_GetPCLK2Freq(void) { return clock_hz; }
void HAL_GPIO_WritePin(void *port,uint32_t pin,GPIO_PinState state)
{
    (void)port;
    assert(!sending); /* DC/CS/reset must not change while bits are on wire. */
    if(state) pins|=pin;else pins&=~pin;
}
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *d)
{
    assert(!(d->Instance->CR&DMA_SxCR_EN));assert(d->Init.MemInc==DMA_MINC_ENABLE);
    assert(d->Init.MemDataAlignment==d->Init.PeriphDataAlignment);return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_Transmit_DMA(SPI_HandleTypeDef *s,uint8_t *bytes,uint16_t n)
{
    assert(!sending && !(pins&CS_Pin));assert(s->Init.BaudRatePrescaler==SPI_BAUDRATEPRESCALER_4);
    if(fail_start) return HAL_ERROR;
    if(!(pins&DC_Pin)) {
        assert(n==1 && s->Init.DataSize==SPI_DATASIZE_8BIT);
        command=*bytes;++command_count;
        if(command==0x29) init_finished=true;
    } else if(s->Init.DataSize==SPI_DATASIZE_16BIT) {
        assert(init_finished && command==0x2c && n==6);
        assert(dma.Init.MemDataAlignment==DMA_MDATAALIGN_HALFWORD);
        assert(((const uint16_t *)bytes)[0]==0xf800);pixel_count+=n;
    } else if(init_finished) {
        assert(n==4);
        if(command==0x2a) memcpy(window_bytes,bytes,4);
        else { assert(command==0x2b);memcpy(window_bytes+4,bytes,4); }
    }
    sending=true;source=bytes;length=n;++sends;
    mock_spi.SR=SPI_FLAG_BSY|SPI_FLAG_TXE;mock_dma.CR|=DMA_SxCR_EN;
    return HAL_OK;
}
static void Finish(HmiTransport *t)
{
    assert(sending && source && length);
    uint32_t before=sends;
    mock_dma.CR&=~DMA_SxCR_EN;dma.XferCpltCallback(&dma);
    /* DMA TC is earlier than the last SPI bit. No next command is allowed. */
    assert(t->poll(t->context)==HMI_BUSY && sends==before);
    sending=false;mock_spi.SR=SPI_FLAG_TXE;
}
int main(void)
{
    HmiSt7796 lcd;
    clock_hz=32000000U;assert(HmiSt7796_Init(&lcd)==HMI_INVALID);clock_hz=64000000U;
    assert(HmiSt7796_Init(&lcd)==HMI_OK);
    assert((mock_spi.CR1&SPI_CR1_BR)==0x08U); /* actual F412 BR[2:0]=001 => /4 */
    HmiTransport t=HmiSt7796_Transport(&lcd);
    assert(t.poll(t.context)==HMI_BUSY && !sends && !(pins&RESET_Pin));
    tick=99;assert(t.poll(t.context)==HMI_BUSY && !sends);
    tick=100;assert(t.poll(t.context)==HMI_BUSY && (pins&RESET_Pin));
    tick=219;assert(t.poll(t.context)==HMI_BUSY && !sends);
    uint16_t pixels[]={0xf800,0x07e0,0x001f,0xffff,0,1};
    assert(t.start(t.context,(HmiRect){258,17,3,2},pixels)==HMI_BUSY);
    for(unsigned n=0;n<10000 && !HmiSt7796_Ready(&lcd);++n) {
        ++tick;if(sending) Finish(&t);(void)t.poll(t.context);
    }
    assert(HmiSt7796_Ready(&lcd) && init_finished && command_count==17);
    assert(t.start(t.context,(HmiRect){479,0,2,1},pixels)==HMI_INVALID);
    unsigned before_start=sends;
    assert(t.start(t.context,(HmiRect){258,17,3,2},pixels)==HMI_OK);
    assert(sending&&sends==before_start+1U); /* no idle tick before the first command */
    assert(t.start(t.context,(HmiRect){0,0,1,1},pixels)==HMI_BUSY);
    for(unsigned n=0;n<30 && !HmiSt7796_Ready(&lcd);++n) {
        ++tick;if(sending) Finish(&t);(void)t.poll(t.context);
    }
    assert(HmiSt7796_Ready(&lcd) && pixel_count==6 && (pins&CS_Pin));
    uint8_t expected[]={1,2,1,4,0,17,0,18};assert(!memcmp(window_bytes,expected,8));
    assert(t.start(t.context,(HmiRect){0,0,3,2},pixels)==HMI_OK);
    (void)t.poll(t.context);tick+=100;
    assert(t.poll(t.context)==HMI_IO_ERROR && lcd.errors==1);
    assert(t.start(t.context,(HmiRect){0,0,3,2},pixels)==HMI_IO_ERROR);
    /* Hardware error latch, safe explicit reinit after stopped wire. */
    sending=false;mock_spi.SR=SPI_FLAG_TXE;
    assert(HmiSt7796_Init(&lcd)==HMI_OK);
    tick+=220;(void)t.poll(t.context);tick+=120;fail_start=true;
    assert(t.poll(t.context)==HMI_IO_ERROR);
    puts("PASS STM32 transport: reset timing, async init/commands, 16MHz, RGB565 halfword DMA, window bytes, BSY drain and timeout/start errors");
}
