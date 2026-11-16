#include "RtuMaster.h"
#include <string.h>
enum {IDLE,QUIET,TRANSMIT,REPLY,COMPLETE};
uint16_t Rtu_Crc(const uint8_t *data,size_t size)
{
    uint16_t crc=0xffff;
    for(size_t i=0;i<size;++i){crc^=data[i];for(unsigned b=0;b<8;++b)crc=(uint16_t)((crc>>1)^((crc&1U)?0xa001U:0U));}
    return crc;
}
void Rtu_Init(RtuMaster *m,RtuIo io,uint32_t quiet,uint32_t timeout,uint8_t retries)
{memset(m,0,sizeof(*m));m->io=io;m->quiet_ms=quiet<2U?2U:quiet;m->timeout_ms=timeout?timeout:100U;m->max_retries=retries>3U?3U:retries;}
static void Finish(RtuMaster *m,bool success,uint32_t now)
{m->success=success;m->phase=COMPLETE;m->last_activity_ms=now;if(success)++m->completed;}
static bool Begin(RtuMaster *m,uint8_t slave,uint8_t function,uint16_t address,uint16_t quantity,uint16_t bytes,uint32_t now)
{
    if(!m||m->phase!=IDLE||!m->io.transmit||!m->io.transmitting||!slave||slave>247U)return false;
    m->slave=slave;m->function=function;m->register_address=address;m->quantity=quantity;
    m->tx[0]=slave;m->tx[1]=function;m->tx[2]=(uint8_t)(address>>8);m->tx[3]=(uint8_t)address;
    m->tx[4]=(uint8_t)(quantity>>8);m->tx[5]=(uint8_t)quantity;
    m->tx_length=bytes;m->rx_length=0;m->exception=0;m->retries=0;m->success=false;
    m->phase=QUIET;m->started_ms=now;return true;
}
static void Checksum(RtuMaster *m)
{uint16_t crc=Rtu_Crc(m->tx,m->tx_length-2U);m->tx[m->tx_length-2U]=(uint8_t)crc;m->tx[m->tx_length-1U]=(uint8_t)(crc>>8);}
bool Rtu_Read(RtuMaster *m,uint8_t slave,uint8_t function,uint16_t address,uint16_t quantity,uint32_t now)
{
    if((function!=3U&&function!=4U)||!quantity||quantity>28U||(uint32_t)address+quantity>65536U)return false;
    if(!Begin(m,slave,function,address,quantity,8,now))return false;
    Checksum(m);return true;
}
bool Rtu_WriteRegisters(RtuMaster *m,uint8_t slave,uint16_t address,const uint16_t *values,uint16_t quantity,uint32_t now)
{
    if(!values||!quantity||quantity>59U||(uint32_t)address+quantity>65536U)return false;
    if(!Begin(m,slave,16,address,quantity,(uint16_t)(9U+2U*quantity),now))return false;
    m->tx[6]=(uint8_t)(quantity*2U);
    for(unsigned i=0;i<quantity;++i){m->tx[7+2*i]=(uint8_t)(values[i]>>8);m->tx[8+2*i]=(uint8_t)values[i];}
    Checksum(m);return true;
}
void Rtu_Receive(RtuMaster *m,const uint8_t *bytes,size_t size,uint32_t now)
{
    if(!m||!bytes)return;
    m->last_activity_ms=now;
    if(m->phase!=REPLY)return;
    if(size>sizeof(m->rx)-m->rx_length){++m->protocol_errors;Finish(m,false,now);return;}
    memcpy(m->rx+m->rx_length,bytes,size);m->rx_length+=(uint16_t)size;
    if(m->rx_length<3U)return;
    unsigned expected=(m->rx[1]&0x80U)?5U:m->function==16U?8U:5U+m->rx[2];
    if(expected>sizeof(m->rx)||m->rx_length>expected){++m->protocol_errors;Finish(m,false,now);return;}
    if(m->rx_length<expected)return;
    if(Rtu_Crc(m->rx,expected)!=0U){++m->crc_errors;Finish(m,false,now);return;}
    if(m->rx[0]!=m->slave||((m->rx[1]&0x7fU)!=m->function)){++m->protocol_errors;Finish(m,false,now);return;}
    if(m->rx[1]&0x80U){m->exception=m->rx[2];Finish(m,false,now);return;}
    bool valid=m->function==16U?
       (m->rx[2]==m->tx[2]&&m->rx[3]==m->tx[3]&&m->rx[4]==m->tx[4]&&m->rx[5]==m->tx[5]):
       m->rx[2]==m->quantity*2U;
    if(!valid)++m->protocol_errors;
    Finish(m,valid,now);
}
void Rtu_Process(RtuMaster *m,uint32_t now)
{
    if(!m)return;
    if(m->phase==QUIET&&now-m->last_activity_ms>=m->quiet_ms) {
        if(m->io.transmit(m->io.context,m->tx,m->tx_length)){m->phase=TRANSMIT;m->started_ms=now;}
        else Finish(m,false,now);
    } else if(m->phase==TRANSMIT) {
        if(!m->io.transmitting(m->io.context)){m->phase=REPLY;m->rx_length=0;m->started_ms=now;}
        else if(now-m->started_ms>=m->timeout_ms){++m->timeouts;Finish(m,false,now);}
    } else if(m->phase==REPLY&&now-m->started_ms>=m->timeout_ms) {
        ++m->timeouts;
        if(m->retries<m->max_retries){++m->retries;++m->retry_count;m->phase=QUIET;m->rx_length=0;m->last_activity_ms=now;}
        else Finish(m,false,now);
    }
}
bool Rtu_Busy(const RtuMaster *m){return m&&m->phase!=IDLE;}
bool Rtu_TakeResult(RtuMaster *m,bool *success)
{if(!m||!success||m->phase!=COMPLETE)return false;*success=m->success;m->phase=IDLE;return true;}
