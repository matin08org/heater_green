#ifndef DIAG_LOG_H
#define DIAG_LOG_H
#include <stdbool.h>
#include <stdint.h>
#define DIAG_LOG_CAPACITY 128U
#define DIAG_LOG_MESSAGE 88U
typedef enum { DIAG_DEBUG, DIAG_INFO, DIAG_WARN, DIAG_ERROR } DiagLevel;
typedef struct { uint32_t sequence,ms; DiagLevel level; char tag[8],text[DIAG_LOG_MESSAGE]; } DiagEntry;
/* Single main-context writer/reader; no formatting/log calls in IRQs.
 * Ring is bounded, overwrites oldest. RAM only; never writes external flash. */
void DiagLog_Init(void);
void DiagLog_Write(uint32_t now, DiagLevel level,const char *tag,const char *format,...)
    __attribute__((format(printf,4,5)));
bool DiagLog_Read(uint16_t oldest_index, DiagEntry *entry);
uint16_t DiagLog_Count(void);
uint32_t DiagLog_Sequence(void);
uint32_t DiagLog_Overwritten(void);
#endif
