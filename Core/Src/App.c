#include "App.h"
#include "main.h"



App_Status_t App_Init(void)
{
    App_Status_t first_error = APP_OK;
    uint8_t hc595_ready = 0U;

    /* Initialize independent modules even if another module fails.
     * MAX6675 depends on HC595 for its chip-select logic, so it is attempted
     * only after a successful HC595 initialization. */
    if (HC595_Init() == HAL_OK)
    {
        hc595_ready = 1U;
    }
    else
    {
        first_error = APP_ERROR_HC595;
    }

    if (hc595_ready != 0U)
    {
        if (MAX6675_Init() != MAX6675_OK)
        {
            if (first_error == APP_OK)
            {
                first_error = APP_ERROR_MAX6675;
            }
        }
    }


    /* Initialize motion without commanding any automatic travel. */
    if (DamperStepper_Init()!=HAL_OK && first_error==APP_OK) first_error=APP_ERROR_DAMPER;
    /* Buzzer is independent of SPI2/HC595/MAX6675; always attempt its init. */
    if (Buzzer_Init() != BUZZER_OK)
    {
        if (first_error == APP_OK)
        {
            first_error = APP_ERROR_BUZZER;
        }
    }

    return first_error;
}

void App_Process(void)
{
    DamperStepper_Process();
    MAX6675_Status_t status = MAX6675_Process(NULL);
    if (status == MAX6675_NOT_INITIALIZED)
        (void)MAX6675_Init();
    Buzzer_Process();
}
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *spi)
{
    HC595_SPIError(spi);
}



