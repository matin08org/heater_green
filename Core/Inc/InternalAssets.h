#ifndef INTERNAL_ASSETS_H
#define INTERNAL_ASSETS_H
#include "main.h"
/* Only the standalone internal-code profile calls this. No erase/status writes. */
extern volatile uint32_t assets_boot_error,assets_read_lanes,assets_jedec;
HAL_StatusTypeDef InternalAssets_Init(void);
#endif
