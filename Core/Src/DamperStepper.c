#include "DamperStepper.h"

/* Exclusive ownership: TIM3 PWM2 CH3 -> PC8, TRGO UPDATE -> TIM2 ITR2.
 * TIM2 external-clock mode 1 counts completed PWM periods. CNT starts at 1,
 * ARR = distance, so overflow occurs after exactly distance pulses. Avoiding
 * ARR=0 also supports a single-pulse move (STM32 blocks a zero-ARR counter).
 * TIM2 OPM clears CEN on overflow. Its TRGO ENABLE gates TIM3 via ITR1.
 * Thus hardware freezes PWM2 in its LOW phase without waiting for the ISR.
 * MSM is deliberately disabled on both timers (no synchronization loop).
 * TIM1 (phase PWM/reset), TIM4 (buzzer) and TIM6 are untouched. See RM0402, TIM2..5.
 */
static volatile uint8_t initialized;
static volatile uint8_t running;
static volatile uint8_t starting;
static uint32_t enable_ms;
static volatile int32_t leg_start;
static volatile int32_t leg_end;
static int32_t endpoint;
static volatile DamperStepper_EndAction_t end_action;

static uint32_t Lock(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    return mask;
}

static void Unlock(uint32_t mask)
{
    __set_PRIMASK(mask);
}

static void ArmLeg(int32_t from, int32_t to)
{
    int64_t delta = (int64_t)to - from;
    uint32_t distance = (uint32_t)((delta > 0) ? delta : -delta);

    TIM2->CR1 &= ~TIM_CR1_CEN; /* Close the hardware gate first. */
    TIM3->CR1 &= ~TIM_CR1_CEN;
    TIM3->CNT = 0U;
    TIM2->ARR = distance;
    TIM2->CNT = 1U;
    TIM2->SR = 0U;
    TIM3->SR = 0U;
    leg_start = from;
    leg_end = to;
    HAL_GPIO_WritePin(DIR_GPIO_Port, DIR_Pin,
                      (to > from) ? GPIO_PIN_SET : GPIO_PIN_RESET);

    /* PWM2 starts LOW. The first half-period provides DIR setup time.
     * No UG here: TIM3 UG would falsely clock the pulse counter. */
    TIM3->CR1 |= TIM_CR1_CEN;
    TIM2->CR1 |= TIM_CR1_CEN;
}

HAL_StatusTypeDef DamperStepper_SetSpeedRPM(float rpm)
{
    RCC_ClkInitTypeDef clocks;
    uint32_t latency;
    uint32_t clock_hz;
    uint32_t divider;
    uint32_t ticks;
    double period;
    uint32_t mask = Lock();

    if (!initialized) { Unlock(mask); return HAL_ERROR; }
    if (running) { Unlock(mask); return HAL_BUSY; }
    /* This comparison also rejects NaN, infinities and nonpositive RPM. */
    if (!(rpm > 0.0f && rpm <= 7500.0f))
    {
        Unlock(mask);
        return HAL_ERROR;
    }
    HAL_RCC_GetClockConfig(&clocks, &latency);
    clock_hz = HAL_RCC_GetPCLK1Freq();
    if (clocks.APB1CLKDivider != RCC_HCLK_DIV1)
        clock_hz *= 2U;

    period = (double)clock_hz * 60.0 / ((double)rpm * DAMPER_STEPS_PER_REVOLUTION);
    if (period > 65536.0 * 65536.0 || period < 2.0)
    {
        Unlock(mask);
        return HAL_ERROR;
    }
    divider = (uint32_t)(period / 65536.0);
    if (period > (double)divider * 65536.0) divider++;
    if (divider < 1U) divider = 1U;
    if (divider > 65536U) { Unlock(mask); return HAL_ERROR; }
    ticks = (uint32_t)(period / divider + 0.5);
    if (ticks < 2U || ticks > 65536U ||
        (double)(ticks / 2U) * divider * 1000000.0 < (double)clock_hz * 5.0)
    {
        Unlock(mask);
        return HAL_ERROR;
    }

    TIM3->CR1 &= ~TIM_CR1_CEN;
    /* Suppress TRGO during UG, which loads the prescaler immediately. */
    TIM3->CR2 = 0U;
    TIM3->PSC = divider - 1U;
    TIM3->ARR = ticks - 1U;
    TIM3->CCR3 = ticks / 2U;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0U;
    TIM3->CR2 = TIM_TRGO_UPDATE;
    Unlock(mask);
    return HAL_OK;
}

HAL_StatusTypeDef DamperStepper_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    if (running) return HAL_BUSY;
    initialized = 0U;
    starting=0U;
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_TIM2_FORCE_RESET();
    __HAL_RCC_TIM3_FORCE_RESET();
    __HAL_RCC_TIM2_RELEASE_RESET();
    __HAL_RCC_TIM3_RELEASE_RESET();

    HAL_GPIO_WritePin(GPIOC, ENSTEP1_Pin | ENSTEP2_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(DIR_GPIO_Port, DIR_Pin, GPIO_PIN_RESET);
    gpio.Pin = DIR_Pin | ENSTEP1_Pin | ENSTEP2_Pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);

    TIM2->CR1 = TIM_CR1_OPM | TIM_CR1_URS;
    TIM2->PSC = 0U;
    TIM2->CR2 = TIM_TRGO_ENABLE;
    TIM2->SMCR = TIM_TS_ITR2 | TIM_SLAVEMODE_EXTERNAL1;
    TIM2->DIER = TIM_DIER_UIE;
    TIM3->CR1 = TIM_CR1_URS;
    TIM3->SMCR = TIM_TS_ITR1 | TIM_SLAVEMODE_GATED;
    TIM3->CCMR2 = TIM_OCMODE_PWM2; /* OC3M, channel 3 active HIGH. */
    TIM3->CCER = TIM_CCER_CC3E;
    /* No TIM3 interrupt: pulse generation and counting run in hardware. */
    leg_start = 0;
    leg_end = 0;
    endpoint = 0;
    end_action = DAMPER_END_STOP;
    initialized = 1U;
    if (DamperStepper_SetSpeedRPM(18.75f) != HAL_OK)
    {
        initialized = 0U;
        return HAL_ERROR;
    }

    gpio.Pin = STEP_Pin;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(STEP_GPIO_Port, &gpio);
    TIM2->SR = 0U;
    HAL_NVIC_ClearPendingIRQ(TIM2_IRQn);
    HAL_NVIC_SetPriority(TIM2_IRQn, 1U, 0U);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    return HAL_OK;
}

HAL_StatusTypeDef DamperStepper_SetEndpoint(int32_t position,
                                          DamperStepper_EndAction_t action)
{
    uint32_t mask = Lock();
    if (!initialized) { Unlock(mask); return HAL_ERROR; }
    if (running) { Unlock(mask); return HAL_BUSY; }
    if (action != DAMPER_END_STOP && action != DAMPER_END_REVERSE)
    {
        Unlock(mask);
        return HAL_ERROR;
    }
    endpoint = position;
    end_action = action;
    Unlock(mask);
    return HAL_OK;
}

HAL_StatusTypeDef DamperStepper_Start(void)
{
    int32_t from;
    uint32_t mask = Lock();
    if (!initialized) { Unlock(mask); return HAL_ERROR; }
    if (running) { Unlock(mask); return HAL_BUSY; }
    from = leg_start;
    if (from == endpoint)
    {
        HAL_GPIO_WritePin(ENSTEP2_GPIO_Port, ENSTEP2_Pin, GPIO_PIN_SET);
        Unlock(mask);
        return HAL_OK;
    }
    /* Keep position queries valid during the driver-enable wait. */
    leg_end = endpoint;
    TIM2->CNT = 1U;
    TIM2->SR = 0U;
    running = 1U; /* Reserve configuration while waiting for driver enable. */
    HAL_GPIO_WritePin(ENSTEP2_GPIO_Port, ENSTEP2_Pin, GPIO_PIN_RESET);
    starting=1U;enable_ms=HAL_GetTick();
    Unlock(mask);
    return HAL_OK;
}
void DamperStepper_Process(void)
{
    uint32_t mask=Lock();
    if (starting && (uint32_t)(HAL_GetTick()-enable_ms)>=2U) {
        starting=0U;
        if (running) ArmLeg(leg_start,endpoint);
    }
    Unlock(mask);
}

int32_t DamperStepper_GetPosition(void)
{
    int32_t result;
    uint32_t mask = Lock();
    if (!running)
        result = leg_start;
    else
    {
        uint32_t count = TIM2->CNT - 1U;
        /* Overflow resets CNT to zero; UIF marks the completed endpoint. */
        if (TIM2->SR & TIM_SR_UIF)
            result = leg_end;
        else
            result = (int32_t)((int64_t)leg_start +
                     ((leg_end > leg_start) ? (int64_t)count : -(int64_t)count));
    }
    Unlock(mask);
    return result;
}

uint8_t DamperStepper_IsRunning(void)
{
    return running;
}
void DamperStepper_Stop(void)
{
    uint32_t mask=Lock();
    if (!initialized) { Unlock(mask); return; }
    TIM2->CR1 &= ~TIM_CR1_CEN;
    TIM3->CR1 &= ~TIM_CR1_CEN;
    int32_t position=DamperStepper_GetPosition();
    TIM3->CCER &= ~TIM_CCER_CC3E;
    running=0U; leg_start=leg_end=endpoint=position;
    starting=0U;
    end_action=DAMPER_END_STOP;
    TIM2->SR=0U; TIM3->CNT=0U;
    TIM3->CCER |= TIM_CCER_CC3E;
    HAL_GPIO_WritePin(ENSTEP2_GPIO_Port,ENSTEP2_Pin,GPIO_PIN_SET);
    Unlock(mask);
}

void DamperStepper_EndpointIRQHandler(void)
{
    int32_t from;
    int32_t to;
    uint32_t mask;
    if (!(TIM2->SR & TIM_SR_UIF)) return;
    mask = Lock();
    TIM2->SR = ~TIM_SR_UIF;
    if (!running) { Unlock(mask); return; }
    /* Hardware has already closed the gate after the final falling edge. */
    TIM3->CR1 &= ~TIM_CR1_CEN;
    from = leg_start;
    to = leg_end;
    leg_start = to;
    if (end_action == DAMPER_END_REVERSE)
        ArmLeg(to, from);
    else
    {
        HAL_GPIO_WritePin(ENSTEP2_GPIO_Port, ENSTEP2_Pin, GPIO_PIN_SET);
        running = 0U;
    }
    Unlock(mask);
}

