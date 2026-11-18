#if defined(FW_CUSTOM_HMI) && defined(FW_FINAL_APPLICATION)
#include "BoardRuntime.h"
#include "HmiMonitorIo.h"
#include "DiagLog.h"
#include "Board595Map.h"
#include "stm32f4xx_ll_adc.h"
#include "BoardBus.h"
#include "BoardActuation.h"
#include "StartOverride.h"
#include <string.h>
static BurnerControl burner;
static BurnerInputs inputs;      /* what the sequencer sees (effective) */
static BurnerInputs real_inputs; /* measured only; used for the warning list */
static BurnerConfig effective_config;
static uint32_t override_placeholders;
/* No MCU acquisition path exists for airflow/interlock/detector/off-proofs. */
static const bool proof_inputs_connected=false;
static BurnerAppSnapshot published;
static BoardAnalogValues analog_values;
static uint32_t analog_ms,analog_sequence;
static bool auto_armed;
static BurnerCommandResult last_start=BURNER_COMMAND_CONFIG_REQUIRED;
static uint32_t StartBlockers(const BoardActuationSnapshot *act,uint32_t now)
{
    /* Existing board has PA1 flame and Hall only. External supervisor and
     * pressure switch have no MCU connection. This is a capability fact. */
    const BurnerHardwareStatus hardware={.proof_inputs_connected=proof_inputs_connected,
        .fan_calibrated=act->fan_calibrated,.fan_sync=act->fan_sync,
        .damper_referenced=act->referenced,.actuation_fault=act->fault,
        .co_calibrated=board_analog_calibration.calibrated};
#if BURNER_START_OVERRIDE
    /* Warnings describe the real state: profile as generated, measured inputs. */
    BurnerControl view=burner;view.config=board_burner_profile;
    return Burner_StartBlockers(&view,&real_inputs,&hardware,now);
#else
    return Burner_StartBlockers(&burner,&inputs,&hardware,now);
#endif
}
static void LogPlaceholders(uint32_t now)
{
    for(unsigned bit=0;bit<SO_PH_COUNT;++bit)if(override_placeholders&(1UL<<bit))
        DiagLog_Write(now,DIAG_WARN,"OVERRIDE","placeholder %s",StartOverride_PlaceholderText(bit));
}
static void Publish(void)
{
    BoardActuationSnapshot act;BoardActuation_Get(&act);
    BurnerControl *visible=&burner;
    BurnerAppSnapshot s={.state=visible->state,.fault=visible->fault,.command_source=visible->last_command_source,
        .fault_source=visible->fault_source,.mode=visible->mode,.requested=visible->out,
        .last_start_result=last_start,.fault_sequence=visible->fault_sequence,.co2_target_ppm=visible->co2_target_ppm,
        .start_blockers=StartBlockers(&act,HAL_GetTick()),
        .configured=Burner_ConfigValid(&visible->config),.actuators_inhibited=act.inhibited,.simulated=false,
        .output_test=false,
        .sample_state=visible->sample_state,.air_percent=visible->out.air_percent,.air_adjustments=visible->air_adjustments,
        .bypass_proven=inputs.bypass_proven,.process_proven=inputs.process_proven,
        .stopped_proven=visible->state==BURNER_IDLE&&inputs.fuel_off_proven&&inputs.ignition_off_proven&&inputs.fan_stopped_proven};
    uint32_t mask=__get_PRIMASK();__disable_irq();published=s;__set_PRIMASK(mask);
}
void BurnerApp_GetSnapshot(BurnerAppSnapshot *out)
{if(out){uint32_t mask=__get_PRIMASK();__disable_irq();*out=published;__set_PRIMASK(mask);}}
void BoardRuntime_GetAnalog(BoardAnalogValues *values,uint32_t *sample_ms)
{if(values)*values=analog_values;if(sample_ms)*sample_ms=analog_ms;}
void BurnerApp_Init(void)
{
    BoardActuation_Init();
    auto_armed=false;
#if BURNER_START_OVERRIDE
    override_placeholders=StartOverride_Config(&board_burner_profile,&effective_config);
    Burner_Init(&burner,&effective_config,HAL_GetTick());
#else
    Burner_Init(&burner,&board_burner_profile,HAL_GetTick());
#endif
    memset(&inputs,0,sizeof(inputs));memset(&real_inputs,0,sizeof(real_inputs));Publish();
#if BURNER_START_OVERRIDE
    DiagLog_Write(HAL_GetTick(),DIAG_WARN,"CONTROL","START OVERRIDE ACTIVE: START is not blocked by commissioning/proof checks (shown as warnings)");
    DiagLog_Write(HAL_GetTick(),DIAG_WARN,"CONTROL","No airflow/interlock/detector proofs: assumed from fan request+sync. Independent hardware cutoff is the only fuel isolation");
    LogPlaceholders(HAL_GetTick());
#else
    DiagLog_Write(HAL_GetTick(),DIAG_WARN,"CONTROL","Unified burner supervisor active; physical outputs inhibited until commissioning");
#endif
}
void BurnerApp_ServiceInputs(const MAX6675_Snapshot_t *tc,const CO2_Snapshot *co2)
{
    uint32_t now=HAL_GetTick();HmiMonitorIoSnapshot io;HmiMonitorIo_Get(&io);
    if(io.adc_sequence!=analog_sequence) {
        HmiDiagAnalog electrical;HmiDiag_Analog(io.raw,io.adc_started&&board_burner_profile.input_max_age_ms&&now-io.adc_ms<board_burner_profile.input_max_age_ms,
            *VREFINT_CAL_ADDR,*TEMPSENSOR_CAL1_ADDR,*TEMPSENSOR_CAL2_ADDR,&electrical);
        SensorMath_Convert(io.raw,&electrical,&board_analog_calibration,&analog_values);
        analog_ms=io.adc_ms;analog_sequence=io.adc_sequence;
    }
    if(!io.adc_started||!board_burner_profile.input_max_age_ms||now-analog_ms>=board_burner_profile.input_max_age_ms)
        analog_values.co_valid=analog_values.me2_temperature_valid=analog_values.mixer_temperature_valid=false;
    inputs.captured_ms=now;
    BoardActuation_Service(now);
    BoardActuationSnapshot act;BoardActuation_Get(&act);
    inputs.bypass_proven=act.bypass_proven;inputs.process_proven=act.process_proven;
    inputs.flame=HAL_GPIO_ReadPin(Flame_Detector_GPIO_Port,Flame_Detector_Pin)==GPIO_PIN_SET;
    inputs.flame_temp_valid=tc&&tc->sensor[0].valid&&tc->sensor[0].temperature_q4<=INT16_MAX;
    inputs.cooler_temp_valid=tc&&tc->sensor[1].valid&&tc->sensor[1].temperature_q4<=INT16_MAX;
    if(inputs.flame_temp_valid)inputs.flame_temp_q4=(int16_t)tc->sensor[0].temperature_q4;
    if(inputs.cooler_temp_valid)inputs.cooler_temp_q4=(int16_t)tc->sensor[1].temperature_q4;
    inputs.co_valid=analog_values.co_valid;inputs.co_ppm=analog_values.co_ppm;inputs.co_sample_ms=analog_ms;
    inputs.co2_valid=co2&&co2->valid;inputs.co2_ppm=co2?co2->ppm:0;
    BoardBusSnapshot bus;BoardBus_Get(&bus);inputs.daylight_valid=bus.lux_valid;inputs.daylight=bus.daylight;
    real_inputs=inputs;
#if BURNER_START_OVERRIDE
    StartOverride_Inputs(&real_inputs,&burner.out,act.fan_sync,proof_inputs_connected,&inputs);
#endif
    /* Unknown safety/airflow/daylight/position proof stays invalid; GPIO level,
     * commanded position and a queued relay image cannot fabricate feedback. */
    BurnerState old=burner.state;BurnerSampleState sample=burner.sample_state;uint8_t air=burner.out.air_percent;
    uint32_t auto_blockers=StartBlockers(&act,now);
#if BURNER_START_OVERRIDE
    auto_blockers&=BURNER_BLOCK_BUSY; /* only a running/locked cycle prevents a new one */
#endif
    if(auto_blockers==0U&&Burner_AutoStartRequested(&burner,&inputs,now,auto_armed)) {
        last_start=Burner_RequestStart(&burner,BURNER_AUTO,&inputs,now);
        DiagLog_Write(now,DIAG_INFO,"AUTO","Demand START result=%u CO2=%u target=%u",last_start,inputs.co2_ppm,burner.co2_target_ppm);
    }
    if(act.fault||(burner.out.combustion_fan&&(!act.fan_sync||!act.fan_calibrated)))
        Burner_ReportActuationFault(&burner,&inputs,now);
    Burner_Process(&burner,&inputs,now);
    /* Missing external safety proofs remain false; no commanded output is
     * substituted for real airflow/fuel-off/detector-health feedback. */
    BoardActuation_Apply(&burner.out,Burner_ConfigValid(&burner.config)&&
        inputs.interlocks_ok&&inputs.detector_healthy&&inputs.airflow_proof_valid&&
        inputs.ventilation_safe&&!act.fault,now);
    /* A failure discovered while applying the request must reach the control
     * state too: never leave the display/PLC at RUNNING with inhibited outputs. */
    BoardActuation_Get(&act);
    if(act.fault)Burner_ReportActuationFault(&burner,&inputs,now);
    if(old!=burner.state)DiagLog_Write(now,burner.state==BURNER_LOCKOUT?DIAG_ERROR:DIAG_INFO,"BURNER",
        "%s -> %s fault=%s",Burner_StateText(old),Burner_StateText(burner.state),Burner_FaultText(burner.fault));
    if(sample!=burner.sample_state)DiagLog_Write(now,DIAG_INFO,"SAMPLE","%s -> %s",Burner_SampleText(sample),Burner_SampleText(burner.sample_state));
    if(air!=burner.out.air_percent)DiagLog_Write(now,DIAG_WARN,"AIR","request %u -> %u percent; trial=%u",air,burner.out.air_percent,burner.air_adjustments);
    Publish();
}
void BurnerApp_Process(const MAX6675_Snapshot_t *tc,const CO2_Snapshot *co2) {BurnerApp_ServiceInputs(tc,co2);}
BurnerCommandResult BurnerApp_Start(BurnerSource source)
{
    BoardActuationSnapshot act;BoardActuation_Get(&act);
    uint32_t blockers=StartBlockers(&act,HAL_GetTick());
#if BURNER_START_OVERRIDE
    if(blockers&&!(blockers&BURNER_BLOCK_BUSY)) {
        DiagLog_Write(HAL_GetTick(),DIAG_WARN,"OVERRIDE","START proceeds despite blockers=%08lx: %s",(unsigned long)blockers,Burner_StartBlockText(blockers));
        LogPlaceholders(HAL_GetTick());
        blockers=0U;
    }
#endif
    if(blockers) {
        last_start=(blockers&BURNER_BLOCK_BUSY)?BURNER_COMMAND_BUSY:
            (blockers&(BURNER_BLOCK_PARAMETERS|BURNER_BLOCK_COMMISSIONING|BURNER_BLOCK_PROOF_CONNECTION))?
            BURNER_COMMAND_CONFIG_REQUIRED:BURNER_COMMAND_INHIBITED;
        Publish();
        DiagLog_Write(HAL_GetTick(),DIAG_WARN,"CMD","START blocked=%08lx: %s",(unsigned long)blockers,Burner_StartBlockText(blockers));
        return last_start;
    }
    last_start=Burner_RequestStart(&burner,source,&inputs,HAL_GetTick());Publish();
    if(last_start==BURNER_COMMAND_ACCEPTED&&burner.mode==BURNER_AUTO_MODE)auto_armed=true;
    DiagLog_Write(HAL_GetTick(),last_start==BURNER_COMMAND_ACCEPTED?DIAG_INFO:DIAG_WARN,"CMD","START source=%u result=%u",(unsigned)source,(unsigned)last_start);return last_start;
}
void BurnerApp_Stop(BurnerSource source)
{if(source==BURNER_HMI||source==BURNER_PLC)auto_armed=false;
 Burner_RequestStop(&burner,source,HAL_GetTick());
 /* Queue fuel/ignition OFF immediately, retaining only the sequencer's fan
  * request. Actual shared-SPI latch is asynchronous, not a hardware cutoff. */
 (void)HC595_WritePin(BOARD595_GAS_VALVE,HC595_OFF);
 (void)HC595_WritePin(BOARD595_IGNITION,HC595_OFF);
 Publish();DiagLog_Write(HAL_GetTick(),DIAG_INFO,"CMD","STOP source=%u gas request OFF",(unsigned)source);}
bool BurnerApp_AcknowledgeFault(void)
{bool ok=Burner_AcknowledgeFault(&burner,&inputs,HAL_GetTick());Publish();DiagLog_Write(HAL_GetTick(),ok?DIAG_INFO:DIAG_WARN,"CMD","Fault ACK=%u",ok);return ok;}
bool BurnerApp_SetMode(BurnerMode mode)
{if(mode!=BURNER_MANUAL_MODE&&mode!=BURNER_AUTO_MODE)return false;
 if(mode!=burner.mode)auto_armed=mode==BURNER_AUTO_MODE;
 burner.mode=mode;Publish();return true;}
bool BurnerApp_SetCO2Target(uint16_t ppm)
{if(ppm<100U||ppm>5000U||ppm%10U)return false;
 burner.co2_target_ppm=ppm;Publish();return true;}
#endif
