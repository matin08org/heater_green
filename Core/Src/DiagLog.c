#include "DiagLog.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static DiagEntry ring[DIAG_LOG_CAPACITY];
static uint16_t head,count;
static uint32_t sequence,overwritten;
void DiagLog_Init(void) { head=count=0U; sequence=overwritten=0U; }
void DiagLog_Write(uint32_t now, DiagLevel level,const char *tag,const char *format,...)
{
    if (!format || (unsigned)level>DIAG_ERROR) return;
    DiagEntry *e=&ring[head];
    e->ms=now; e->sequence=++sequence; e->level=level;
    snprintf(e->tag,sizeof(e->tag),"%s",tag?tag:"SYS");
    va_list args; va_start(args,format);
    vsnprintf(e->text,sizeof(e->text),format,args); va_end(args);
    /* Keep a log entry one semantic message; escape control characters. */
    for (unsigned i=0;e->text[i];i++) if ((unsigned char)e->text[i]<32U) e->text[i]=' ';
    head=(uint16_t)((head+1U)%DIAG_LOG_CAPACITY);
    if (count<DIAG_LOG_CAPACITY) count++; else overwritten++;
}
bool DiagLog_Read(uint16_t index, DiagEntry *entry)
{
    if (!entry || index>=count) return false;
    *entry=ring[(head+DIAG_LOG_CAPACITY-count+index)%DIAG_LOG_CAPACITY];
    return true;
}
uint16_t DiagLog_Count(void) { return count; }
uint32_t DiagLog_Sequence(void) { return sequence; }
uint32_t DiagLog_Overwritten(void) { return overwritten; }
