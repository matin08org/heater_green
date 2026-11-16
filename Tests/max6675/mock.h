#ifndef TEST_MAX6675_MOCK_H
#define TEST_MAX6675_MOCK_H
#define __MAIN_H
#include <stdint.h>
#include <stddef.h>
typedef enum { HAL_OK=0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { HAL_SPI_STATE_RESET=0, HAL_SPI_STATE_READY, HAL_SPI_STATE_BUSY } HAL_SPI_StateTypeDef;
typedef enum { GPIO_PIN_RESET=0, GPIO_PIN_SET } GPIO_PinState;
typedef struct { uint32_t Mode,Direction,DataSize,CLKPolarity,CLKPhase,FirstBit,BaudRatePrescaler; } MockSPIInit;
typedef struct { uint32_t CR2; } MockSPIRegisters;
typedef struct { uint32_t CR; } MockDMARegisters;
typedef struct { MockDMARegisters *Instance; uint32_t State,Lock,ErrorCode; } DMA_HandleTypeDef;
typedef struct { MockSPIRegisters *Instance; MockSPIInit Init; HAL_SPI_StateTypeDef State; uint32_t Lock,ErrorCode; DMA_HandleTypeDef *hdmarx,*hdmatx; } SPI_HandleTypeDef;
typedef struct { uint32_t CTRL,CYCCNT; } MockDWT;
typedef struct { uint32_t DEMCR; } MockCoreDebug;
extern MockDWT mock_dwt;
extern MockCoreDebug mock_core;
#define DWT (&mock_dwt)
#define CoreDebug (&mock_core)
extern uint32_t SystemCoreClock;
#define CoreDebug_DEMCR_TRCENA_Msk 1U
#define DWT_CTRL_CYCCNTENA_Msk 1U
extern MockSPIRegisters mock_spi;
#define SPI2 (&mock_spi)
#define STCP_GPIO_Port ((void*)1)
#define STCP_Pin 1U
#define SPI_MODE_MASTER 1U
#define SPI_DIRECTION_2LINES 2U
#define SPI_DATASIZE_8BIT 8U
#define SPI_POLARITY_LOW 0U
#define SPI_PHASE_1EDGE 0U
#define SPI_FIRSTBIT_MSB 0U
#define SPI_CR1_BR (7U<<3)
#define SPI_BAUDRATEPRESCALER_32 (4U<<3)
#define SPI_BAUDRATEPRESCALER_256 (7U<<3)
#define SPI_IT_TXE 1U
#define SPI_IT_RXNE 2U
#define SPI_IT_ERR 4U
#define HAL_UNLOCKED 0U
#define HAL_SPI_ERROR_NONE 0U
#define HAL_DMA_STATE_READY 1U
#define HAL_DMA_ERROR_NONE 0U
#define DMA_SxCR_EN 1U
#define SPI_CR2_RXDMAEN 1U
#define SPI_CR2_TXDMAEN 2U
#define CLEAR_BIT(r,b) ((r)&=~(b))
#define __HAL_DMA_DISABLE(h) ((h)->Instance->CR=0U)
#define DMA_IT_TC 1U
#define DMA_IT_HT 2U
#define DMA_IT_TE 4U
#define DMA_IT_DME 8U
#define DMA_IT_FE 16U
#define __HAL_DMA_DISABLE_IT(h,b) ((void)(h),(void)(b))
#define __HAL_DMA_CLEAR_FLAG(h,b) ((void)(h),(void)(b))
#define __HAL_DMA_GET_TC_FLAG_INDEX(h) 1U
#define __HAL_DMA_GET_HT_FLAG_INDEX(h) 2U
#define __HAL_DMA_GET_TE_FLAG_INDEX(h) 4U
#define __HAL_DMA_GET_DME_FLAG_INDEX(h) 8U
#define __HAL_DMA_GET_FE_FLAG_INDEX(h) 16U
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t p);
#define __DMB() ((void)0)
#define __HAL_SPI_DISABLE_IT(h,b) ((void)(h),(void)(b))
void MockDisableSPI(void);
#define __HAL_SPI_DISABLE(h) ((void)(h),MockDisableSPI())
#define __HAL_SPI_CLEAR_OVRFLAG(h) ((void)(h))
uint32_t HAL_GetTick(void);
uint32_t HAL_RCC_GetPCLK1Freq(void);
void HAL_GPIO_WritePin(void *port,uint16_t pin,GPIO_PinState value);
HAL_StatusTypeDef HAL_SPI_TransmitReceive_DMA(SPI_HandleTypeDef *spi,uint8_t *tx,uint8_t *rx,uint16_t n);
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *spi);
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *spi);
#endif
