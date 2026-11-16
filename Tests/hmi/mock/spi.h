#ifndef HMI_MOCK_SPI_H
#define HMI_MOCK_SPI_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef struct { uint32_t CR1,CR2,SR; } MockSPI;
typedef struct { uint32_t CR; } MockDMA;
typedef struct DMA_HandleTypeDef DMA_HandleTypeDef;
struct DMA_HandleTypeDef {
    MockDMA *Instance;
    struct { uint32_t MemInc,Mode,PeriphDataAlignment,MemDataAlignment,FIFOMode,Priority; } Init;
    void (*XferCpltCallback)(DMA_HandleTypeDef *);
    void (*XferErrorCallback)(DMA_HandleTypeDef *);
    void (*XferHalfCpltCallback)(DMA_HandleTypeDef *);
};
typedef struct {
    MockSPI *Instance;
    DMA_HandleTypeDef *hdmatx;
    struct { uint32_t DataSize,BaudRatePrescaler; } Init;
    uint32_t TxXferCount,State,ErrorCode;
} SPI_HandleTypeDef;
extern SPI_HandleTypeDef hspi1;
extern MockSPI mock_spi;
#define SPI1 (&mock_spi)
#define SPI_CR2_TXDMAEN 1U
#define SPI_CR1_DFF 0x800U
#define SPI_CR1_BR 0x38U
#define SPI_DATASIZE_8BIT 0U
#define SPI_DATASIZE_16BIT 0x800U
#define SPI_BAUDRATEPRESCALER_4 0x08U
#define SPI_FLAG_BSY 1U
#define SPI_FLAG_TXE 2U
#define DMA_SxCR_EN 1U
#define DMA_PDATAALIGN_BYTE 0U
#define DMA_MDATAALIGN_BYTE 0U
#define DMA_PDATAALIGN_HALFWORD 2U
#define DMA_MDATAALIGN_HALFWORD 2U
#define DMA_MINC_ENABLE 1U
#define DMA_NORMAL 0U
#define DMA_FIFOMODE_DISABLE 0U
#define DMA_PRIORITY_HIGH 1U
#define HAL_SPI_STATE_READY 0U
#define HAL_SPI_ERROR_NONE 0U
#define DMA_IT_HT 1U
#define SPI_IT_ERR 2U
#define CS_GPIO_Port ((void *)1)
#define DC_GPIO_Port ((void *)1)
#define RESET_GPIO_Port ((void *)1)
#define CS_Pin 1U
#define DC_Pin 2U
#define RESET_Pin 4U
typedef enum { GPIO_PIN_RESET,GPIO_PIN_SET } GPIO_PinState;
typedef enum { HAL_OK,HAL_ERROR,HAL_BUSY } HAL_StatusTypeDef;
#define CLEAR_BIT(r,b) ((r)&=~(b))
#define MODIFY_REG(r,m,v) ((r)=((r)&~(m))|(v))
#define __HAL_SPI_DISABLE(s) ((void)(s))
#define __HAL_SPI_DISABLE_IT(s,b) ((void)(s),(void)(b))
#define __HAL_DMA_DISABLE_IT(s,b) ((void)(s),(void)(b))
#define __HAL_DMA_DISABLE(d) ((d)->Instance->CR&=~DMA_SxCR_EN)
#define __HAL_SPI_GET_FLAG(s,b) ((s)->Instance->SR&(b))
static inline uint32_t __get_PRIMASK(void) { return 0; }
static inline void __disable_irq(void) {}
static inline void __set_PRIMASK(uint32_t p) { (void)p; }
uint32_t HAL_GetTick(void);
uint32_t HAL_RCC_GetPCLK2Freq(void);
void HAL_GPIO_WritePin(void *,uint32_t,GPIO_PinState);
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *);
HAL_StatusTypeDef HAL_SPI_Transmit_DMA(SPI_HandleTypeDef *,uint8_t *,uint16_t);
#endif
