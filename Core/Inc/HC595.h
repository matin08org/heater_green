#ifndef HC595_H
#define HC595_H
#include "main.h"
extern SPI_HandleTypeDef hspi2;
#define HC595_SPI_HANDLE hspi2
#define HC595_STCP_GPIO_Port STCP_GPIO_Port
#define HC595_STCP_Pin STCP_Pin
#define HC595_OUTPUT_COUNT 8U /* One populated 595; full chain image = one byte. */
typedef enum { HC595_QA=0, HC595_QB, HC595_QC, HC595_QD, HC595_QE, HC595_QF, HC595_QG, HC595_QH } HC595_Output_t;
typedef enum { HC595_OFF=0, HC595_ON=1 } HC595_State_t;
HAL_StatusTypeDef HC595_Init(void);
/* Main-context service. Public writes enqueue changes; HAL_OK means accepted,
 * not latched. Q0 is reserved: pin/toggle requests for Q0 fail; byte/all writes
 * preserve it. Updates requested during a sensor read are deferred. */
void HC595_Process(void);
HAL_StatusTypeDef HC595_WritePin(HC595_Output_t output, HC595_State_t state);
HAL_StatusTypeDef HC595_TogglePin(HC595_Output_t output);
HAL_StatusTypeDef HC595_WriteByte(uint8_t data);
HAL_StatusTypeDef HC595_SetAll(void);
HAL_StatusTypeDef HC595_ClearAll(void);
HAL_StatusTypeDef HC595_Update(void);
uint8_t HC595_GetShadow(void);
HC595_State_t HC595_GetPinState(HC595_Output_t output);
void HC595_SPIError(SPI_HandleTypeDef *spi);
typedef struct {
    uint32_t errors,latches,last_latch_ms,last_error;
    uint8_t state,shadow,latched,initialized,dirty;
} HC595_Diagnostics;
void HC595_GetDiagnostics(HC595_Diagnostics *out);
#endif
