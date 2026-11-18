#include "StartOverride.h"
#include <string.h>

#define FILL(field, bit, value) \
    do { if (!effective->field) { effective->field = (value); mask |= (bit); } } while (0)

uint32_t StartOverride_Config(const BurnerConfig *profile, BurnerConfig *effective)
{
    uint32_t mask = 0U;
    if (!effective) return 0U;
    if (!profile) { memset(effective, 0, sizeof(*effective)); return 0U; }
    *effective = *profile;
    if (!effective->commissioned) { effective->commissioned = true; mask |= SO_PH_COMMISSIONED; }
    FILL(required_purge_ms, SO_PH_REQUIRED_PURGE, effective->purge_ms);
    /* Placeholders below are NOT design values. Safety thresholds are set on the
     * low (nuisance-lockout) side so that, once CO is calibrated, a wrong
     * placeholder trips the burner instead of letting it run. */
    FILL(airflow_timeout_ms, SO_PH_AIRFLOW_TIMEOUT, 5000U);
    FILL(co_min_wait_ms, SO_PH_CO_MIN_WAIT, 30000U);
    FILL(co_timeout_ms, SO_PH_CO_TIMEOUT, 300000U);
    FILL(co_accept_ppm, SO_PH_CO_ACCEPT, 50U);
    FILL(co_limit_ppm, SO_PH_CO_LIMIT, 100U);
    FILL(co_trip_ppm, SO_PH_CO_TRIP, 200U);
    FILL(initial_measure_delay_ms, SO_PH_MEASURE_DELAY, 60000U);
    FILL(cooling_timeout_ms, SO_PH_COOLING_TIMEOUT, 600000U);
    FILL(air_settle_ms, SO_PH_AIR_SETTLE, 30000U);
    FILL(air_initial_percent, SO_PH_AIR_INITIAL, 50U);
    FILL(max_adjustments, SO_PH_MAX_ADJUST, 5U);
    return mask;
}

const char *StartOverride_PlaceholderText(unsigned bit)
{
    static const char *const text[SO_PH_COUNT] = {
        "commissioned=true", "required_purge_ms", "airflow_timeout_ms=5000",
        "co_min_wait_ms=30000", "co_timeout_ms=300000", "co_accept_ppm=50",
        "co_limit_ppm=100", "co_trip_ppm=200", "initial_measure_delay_ms=60000",
        "cooling_timeout_ms=600000", "air_settle_ms=30000", "air_initial_percent=50",
        "max_adjustments=5" };
    return bit < SO_PH_COUNT ? text[bit] : "?";
}

void StartOverride_Inputs(const BurnerInputs *real, const BurnerOutputs *requested,
                          bool fan_sync, bool proof_inputs_connected,
                          BurnerInputs *effective)
{
    if (!real || !effective) return;
    *effective = *real;
    if (proof_inputs_connected) return;
    effective->interlocks_ok = true;
    effective->detector_healthy = true;
    effective->airflow_proof_valid = true;
    effective->ventilation_safe = true;
    effective->airflow = requested && requested->combustion_fan && fan_sync;
    effective->fuel_off_proven = !requested || !requested->gas;
    effective->ignition_off_proven = !requested || !requested->ignition;
    effective->fan_stopped_proven = !requested || !requested->combustion_fan;
}
