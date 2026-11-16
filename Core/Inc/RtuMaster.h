#ifndef RTU_MASTER_H
#define RTU_MASTER_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct {
    void *context;
    bool (*transmit)(void *,const uint8_t *,uint16_t);
    bool (*transmitting)(void *);
} RtuIo;
typedef struct {
    RtuIo io;uint8_t tx[128],rx[64];uint16_t tx_length,rx_length,register_address,quantity;
    uint8_t slave,function,phase,retries,max_retries,exception;
    uint32_t quiet_ms,timeout_ms,last_activity_ms,started_ms;
    uint32_t completed,timeouts,crc_errors,protocol_errors,retry_count;
    bool success;
} RtuMaster;
uint16_t Rtu_Crc(const uint8_t *bytes,size_t length);
void Rtu_Init(RtuMaster *master,RtuIo io,uint32_t quiet_ms,uint32_t timeout_ms,uint8_t retries);
bool Rtu_Read(RtuMaster *master,uint8_t slave,uint8_t function,uint16_t address,uint16_t quantity,uint32_t now);
bool Rtu_WriteRegisters(RtuMaster *master,uint8_t slave,uint16_t address,const uint16_t *values,uint16_t quantity,uint32_t now);
void Rtu_Receive(RtuMaster *master,const uint8_t *bytes,size_t length,uint32_t now);
void Rtu_Process(RtuMaster *master,uint32_t now);
bool Rtu_Busy(const RtuMaster *master);
bool Rtu_TakeResult(RtuMaster *master,bool *success);
#endif
