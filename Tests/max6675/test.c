#include "MAX6675.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
SPI_HandleTypeDef hspi2;
MockSPIRegisters mock_spi;
static MockDMARegisters dma_rx_regs,dma_tx_regs;
static DMA_HandleTypeDef dma_rx={.Instance=&dma_rx_regs},dma_tx={.Instance=&dma_tx_regs};
MockDWT mock_dwt;
MockCoreDebug mock_core;
uint32_t SystemCoreClock=64000000U;
static uint32_t mask, tick_base;
static uint64_t now_us, raised[2], last_select_us;
static uint8_t *pending_rx, sent, latched=0U, active, stall, inject_error, fail_select;
static uint16_t size, words[2]={ (401U<<3)|1U, (802U<<3)|1U };
static uint64_t finish_us;
static unsigned transactions, latches, reads[2], recoveries;
uint32_t __get_PRIMASK(void) { return mask; }
void __disable_irq(void) { mask=1; }
void __set_PRIMASK(uint32_t p) { mask=p; }
uint32_t HAL_GetTick(void) { return tick_base+(uint32_t)(now_us/1000U); }
uint32_t HAL_RCC_GetPCLK1Freq(void) { return 32000000U; }
void MockDisableSPI(void) { active=0; stall=0; recoveries++; }
HAL_StatusTypeDef HAL_SPI_TransmitReceive_DMA(SPI_HandleTypeDef *spi,uint8_t *tx,uint8_t *rx,uint16_t n)
{
    assert(!active); /* Enforces exclusive SPI2 ownership. */
    assert(n==1U || n==2U);
    if (n==1U && fail_select) { fail_select=0; return HAL_ERROR; }
    if (n==2U) {
        unsigned sensor=(latched&1U)?1U:0U;
        assert(now_us-last_select_us>=MAX6675_CS_SETTLE_US);
        assert(now_us-raised[sensor]>=500000U);
        reads[sensor]++;
    }
    spi->State=HAL_SPI_STATE_BUSY;
    dma_rx_regs.CR=dma_tx_regs.CR=DMA_SxCR_EN;
    transactions++;
    pending_rx=rx; sent=tx[0]; size=n; active=1;
    finish_us=now_us+n*8U; /* 1 MHz: 8 clocks per byte. */
    return HAL_OK;
}
void HAL_GPIO_WritePin(void *port,uint16_t pin,GPIO_PinState value)
{
    (void)port; (void)pin;
    if (value==GPIO_PIN_RESET) return;
    assert(!active && size==1U); /* Never latch leftovers from a sensor read. */
    uint8_t old=latched;
    latched=sent;
    if ((old^latched)&1U) {
        last_select_us=now_us;
        raised[(latched&1U)?0U:1U]=now_us;
    }
    latches++;
}
static void Step(void)
{
    now_us++;
    mock_dwt.CYCCNT=(uint32_t)(now_us*64U);
    if (active && !stall && now_us>=finish_us) {
        active=0;
        dma_rx_regs.CR=dma_tx_regs.CR=0U;
        hspi2.State=HAL_SPI_STATE_READY;
        if (inject_error && size==2U) {
            inject_error=0;
            HC595_SPIError(&hspi2);
        } else {
            uint16_t word=words[(latched&1U)?1:0];
            pending_rx[0]=(uint8_t)(word>>8);
            if (size==2U) pending_rx[1]=(uint8_t)word;
            HAL_SPI_TxRxCpltCallback(&hspi2);
        }
    }
    (void)MAX6675_Process(NULL);
}
static void Advance(uint32_t us) { while(us--) Step(); }
static MAX6675_Snapshot_t Snapshot(void) { MAX6675_Snapshot_t s; MAX6675_GetSnapshot(&s); return s; }
int main(int argc,char **argv)
{
    (void)argv;
    if (argc>1 && !strcmp(argv[1],"wrap")) tick_base=UINT32_MAX-100U;
    hspi2.Instance=SPI2;
    hspi2.hdmarx=&dma_rx;
    hspi2.hdmatx=&dma_tx;
    hspi2.State=HAL_SPI_STATE_READY;
    hspi2.Init=(MockSPIInit){SPI_MODE_MASTER,SPI_DIRECTION_2LINES,SPI_DATASIZE_8BIT,
                             SPI_POLARITY_LOW,SPI_PHASE_1EDGE,SPI_FIRSTBIT_MSB,SPI_BAUDRATEPRESCALER_32};
    assert(HC595_Init()==HAL_OK);
    hspi2.Init.BaudRatePrescaler=(3U<<3); /* 2 MHz exceeds the chosen 1 MHz ceiling. */
    assert(MAX6675_Init()==MAX6675_SPI_CONFIG_ERROR);
    hspi2.Init.BaudRatePrescaler=SPI_BAUDRATEPRESCALER_32;
    if (argc>1 && !strcmp(argv[1],"startup")) fail_select=1;
    assert(MAX6675_Init()==MAX6675_OK);
    if (argc>1 && !strcmp(argv[1],"startup")) {
        assert(MAX6675_Process(NULL)==MAX6675_SPI_ERROR);
        MAX6675_Snapshot_t failed=Snapshot();
        assert(!failed.sensor[0].has_valid_sample && failed.sensor[0].last_status==MAX6675_SPI_ERROR);
        assert(MAX6675_Init()==MAX6675_OK);
    }
    MAX6675_Snapshot_t s=Snapshot();
    assert(!s.sensor[0].has_valid_sample && !s.sensor[1].has_valid_sample);
    assert(HC595_WritePin(HC595_QA,HC595_OFF)==HAL_ERROR);
    assert(HC595_TogglePin(HC595_QA)==HAL_ERROR);
    assert(HC595_WriteByte(0xAAU)==HAL_OK && HC595_GetShadow()==0xABU);
    Advance(500000U);
    assert(reads[0]==0 && reads[1]==0);
    /* Queue output changes inside the protected latch->read interval. */
    while (!reads[0]) {
        Step();
        if (!(latched&1U)) assert(HC595_WriteByte(0xFEU)==HAL_OK);
    }
    unsigned held_latches=latches;
    assert(HC595_ClearAll()==HAL_OK && !(HC595_GetShadow()&1U));
    Advance(8U);
    assert(latches==held_latches); /* No output transaction during the read. */
    Advance(600000U);
    s=Snapshot();
    assert(s.sensor[0].temperature_q4==401U && s.sensor[0].temperature_c==100.25f);
    assert(s.sensor[1].temperature_q4==802U && s.sensor[1].temperature_c==200.5f);
    assert(s.sensor[0].valid && s.sensor[1].valid);
    unsigned idle_transactions=transactions;
    for(unsigned n=0;n<100;n++){assert(HC595_WriteByte(HC595_GetShadow())==HAL_OK);Step();}
    assert(transactions==idle_transactions); /* Process polling must not flood SPI2. */
    assert(HC595_Update()==HAL_OK);Advance(30U);assert(transactions==idle_transactions+1U);
    uint32_t last_valid=s.sensor[0].last_valid_ms;
    words[0]|=4U;
    Advance(1000000U);
    s=Snapshot();
    assert(s.sensor[0].last_status==MAX6675_OPEN_THERMOCOUPLE);
    assert(s.sensor[0].temperature_q4==401U && s.sensor[0].last_valid_ms==last_valid);
    assert(s.sensor[0].has_valid_sample && !s.sensor[0].valid);
    assert(s.sensor[0].open_error_count>=1U);
    /* A read failure must retain the previous valid sensor 1 temperature. */
    inject_error=1;
    Advance(1000000U);
    s=Snapshot();
    assert(s.sensor[0].last_status==MAX6675_SPI_ERROR && s.sensor[0].temperature_q4==401U);
    assert(s.sensor[0].spi_error_count>=1U);
    assert(s.sensor[1].valid); /* Other sensor continues after a failed read. */
    /* Deliberately lose the IRQ callback: watchdog recovers, no partial latch. */
    while (!(active && size==2U)) Step();
    stall=1;
    unsigned before=recoveries;
    Advance(11000U);
    assert(recoveries>before);
    s=Snapshot();
    assert(s.sensor[(latched&1U)?1:0].last_status==MAX6675_SPI_ERROR);
    /* Reject a CS transfer before its latch; Q0 must stay unchanged on retries. */
    uint8_t before_q=latched&1U;
    fail_select=1;
    Advance(495000U);
    assert((latched&1U)==before_q);
    Advance(1100000U); /* A later retry and the alternate conversion must recover. */
    s=Snapshot();
    assert(s.sensor[1].has_valid_sample);
    words[0]=1U; /* A real zero-degree reading is valid; D0 is ignored. */
    Advance(1100000U);
    s=Snapshot();
    assert(s.sensor[0].has_valid_sample && s.sensor[0].valid && s.sensor[0].temperature_q4==0U);
    unsigned saved_transactions=transactions;
    for (unsigned i=0;i<100;i++) s=Snapshot();
    assert(transactions==saved_transactions); /* Snapshot never touches SPI. */
    now_us+=2100000U; /* No Process: simulate a stopped acquisition task. */
    s=Snapshot();
    assert(s.sensor[0].stale && s.sensor[1].stale);
    assert(!s.sensor[0].valid && !s.sensor[1].valid);
    printf("PASS (%s): conversion/settle timing, exact 16 clocks, Q0 ownership, deferred outputs, D0/D2, last-good retention, SPI errors/timeouts, recovery, stale, cached snapshots\n",argc>1?argv[1]:"normal");
    return 0;
}
