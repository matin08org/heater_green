#include "InternalAssets.h"
#include "AssetManifest.h"
/* Pin/flash-bank selection and EBh format match the supplied W25Q16 loader:
 * PA6/7+PC4/5 AF10, PB1+PC11 AF9, bank2. Program/read-only resource mapping;
 * never execute this init while application code lives in external memory. */
volatile uint32_t assets_boot_error,assets_read_lanes,assets_jedec;
extern const uint32_t asset_expected_crc;
extern const uint8_t __assets_start,__assets_end;
#define DEADLINE_MS 20U
static int Wait(uint32_t flags,uint32_t value)
{
    uint32_t start=HAL_GetTick();
    while ((QUADSPI->SR&flags)!=value) {
        if ((uint32_t)(HAL_GetTick()-start)>=DEADLINE_MS) return 0;
    }
    return 1;
}
static int Command(uint8_t instruction,uint8_t *bytes,uint32_t count)
{
    if (!Wait(QUADSPI_SR_BUSY,0U)) return 0;
    QUADSPI->FCR=0x1FU;
    QUADSPI->DLR=count?count-1U:0U;
    QUADSPI->CCR=instruction|(1U<<8)|(count?((1U<<24)|(1U<<26)):0U);
    uint32_t started=HAL_GetTick();
    for (uint32_t i=0;i<count;i++) {
        while (!(QUADSPI->SR&QUADSPI_SR_FLEVEL)) {
            if ((QUADSPI->SR&QUADSPI_SR_TEF) || (uint32_t)(HAL_GetTick()-started)>=DEADLINE_MS) return 0;
        }
        bytes[i]=*(volatile uint8_t*)&QUADSPI->DR;
    }
    if (!Wait(QUADSPI_SR_TCF,QUADSPI_SR_TCF) || !Wait(QUADSPI_SR_BUSY,0U)) return 0;
    QUADSPI->FCR=0x1FU;
    return 1;
}
static int Valid(void)
{
    const volatile uint32_t *header=(const volatile uint32_t*)0x90000000U;
    uint32_t length=(uint32_t)((uintptr_t)&__assets_end-(uintptr_t)&__assets_start);
    return AssetManifest_Valid(header,&__assets_start,length,asset_expected_crc);
}
static void Map(uint8_t quad)
{
    QUADSPI->ABR=0xFFU;
    /* Instruction every transaction; 24-bit addresses. Quad protocol is the
     * loader's EB/4-line address+alternate FF/4 dummy/4-line data format. */
    QUADSPI->CCR=quad?(0xEBU|(1U<<8)|(3U<<10)|(2U<<12)|(3U<<14)|(4U<<18)|(3U<<24)|(3U<<26)):
        (0x0BU|(1U<<8)|(1U<<10)|(2U<<12)|(8U<<18)|(1U<<24)|(3U<<26));
    __DSB();__ISB();
}
HAL_StatusTypeDef InternalAssets_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();__HAL_RCC_GPIOB_CLK_ENABLE();__HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_QSPI_CLK_ENABLE();__HAL_RCC_QSPI_FORCE_RESET();__HAL_RCC_QSPI_RELEASE_RESET();
    GPIO_InitTypeDef p={0};p.Mode=GPIO_MODE_AF_PP;p.Pull=GPIO_PULLUP;p.Speed=GPIO_SPEED_FREQ_VERY_HIGH;
    p.Pin=GPIO_PIN_6|GPIO_PIN_7;p.Alternate=GPIO_AF10_QSPI;HAL_GPIO_Init(GPIOA,&p);
    p.Pin=GPIO_PIN_4|GPIO_PIN_5;HAL_GPIO_Init(GPIOC,&p);
    p.Pin=GPIO_PIN_1;p.Alternate=GPIO_AF9_QSPI;HAL_GPIO_Init(GPIOB,&p);
    p.Pin=GPIO_PIN_11;HAL_GPIO_Init(GPIOC,&p);
    /* 64-MHz HCLK/2 =32MHz; conservative CS high4 cycles, half-cycle sampling. */
    QUADSPI->DCR=(20U<<16)|(3U<<8);
    QUADSPI->CR=(1U<<24)|(3U<<8)|QUADSPI_CR_SSHIFT|QUADSPI_CR_FSEL|QUADSPI_CR_EN;
    if (!Command(0x66U,0,0) || !Command(0x99U,0,0)) {assets_boot_error=1U;return HAL_ERROR;}
    HAL_Delay(1U); /* Flash reset recovery, startup only. */
    uint8_t id[3],status;
    if (!Command(0x9FU,id,3) || id[0]!=0xEFU || id[2]!=0x15U) {assets_boot_error=2U;return HAL_ERROR;}
    assets_jedec=((uint32_t)id[0]<<16)|((uint32_t)id[1]<<8)|id[2];
    if (!Command(0x35U,&status,1U)) {assets_boot_error=3U;return HAL_ERROR;}
    uint8_t quad=(status&2U)!=0U;assets_read_lanes=quad?4U:1U;
    Map(quad);
    if (Valid()) return HAL_OK;
    /* Retry read/CRC at16MHz, without rewriting QE or any flash data. */
    QUADSPI->CR|=QUADSPI_CR_ABORT;
    if (!Wait(QUADSPI_SR_BUSY,0U)) {assets_boot_error=4U;return HAL_ERROR;}
    QUADSPI->FCR=0x1FU;
    MODIFY_REG(QUADSPI->CR,QUADSPI_CR_PRESCALER,3U<<24);
    Map(quad);
    if (!Valid()) {assets_boot_error=5U;return HAL_ERROR;}
    return HAL_OK;
}
