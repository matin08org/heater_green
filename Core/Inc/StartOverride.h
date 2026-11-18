#ifndef START_OVERRIDE_H
#define START_OVERRIDE_H
#include "BurnerControl.h"

/* START OVERRIDE (owner decision, see Docs/Status.md).
 * 1 = START is never refused because of commissioning, parameters, proof
 *     wiring, calibration or stale-readiness checks; gas/ignition/fan are
 *     requested for real. Those checks are still computed and published as
 *     warnings (Home / DIAG / log).
 * 0 = previous behaviour (START blocked until commissioned).
 * Compile with -DBURNER_START_OVERRIDE=0 to build the old behaviour. */
#ifndef BURNER_START_OVERRIDE
#define BURNER_START_OVERRIDE 1
#endif

/* Bits describing which BurnerConfig fields were NOT supplied by the board
 * profile and received an UNVALIDATED placeholder. Placeholders are only
 * written into fields that are zero in the profile, so any value the owner
 * enters in Assets/board.json always wins. */
enum {
    SO_PH_COMMISSIONED = 1U << 0,
    SO_PH_REQUIRED_PURGE = 1U << 1,
    SO_PH_AIRFLOW_TIMEOUT = 1U << 2,
    SO_PH_CO_MIN_WAIT = 1U << 3,
    SO_PH_CO_TIMEOUT = 1U << 4,
    SO_PH_CO_ACCEPT = 1U << 5,
    SO_PH_CO_LIMIT = 1U << 6,
    SO_PH_CO_TRIP = 1U << 7,
    SO_PH_MEASURE_DELAY = 1U << 8,
    SO_PH_COOLING_TIMEOUT = 1U << 9,
    SO_PH_AIR_SETTLE = 1U << 10,
    SO_PH_AIR_INITIAL = 1U << 11,
    SO_PH_MAX_ADJUST = 1U << 12
};
#define SO_PH_COUNT 13U

/* Copies the profile and fills the unset parameters. Returns SO_PH_* bits. */
uint32_t StartOverride_Config(const BurnerConfig *profile, BurnerConfig *effective);
const char *StartOverride_PlaceholderText(unsigned bit);

/* Builds the inputs the sequencer sees. The real inputs (flame PA1, flame and
 * cooler temperatures, CO, CO2, daylight, damper feedback, input age) are
 * copied unchanged. Only the five proof groups that have NO acquisition path
 * on this board are derived from what the board itself requested:
 *   interlocks / detector health / airflow-proof validity / ventilation safe
 *       -> assumed OK
 *   airflow        -> fan requested AND phase-sync of the fan drive present
 *   fuel/ignition/fan "stopped" proofs -> the corresponding request is OFF
 * If proof_inputs_connected is true the real inputs are passed through. */
void StartOverride_Inputs(const BurnerInputs *real, const BurnerOutputs *requested,
                          bool fan_sync, bool proof_inputs_connected,
                          BurnerInputs *effective);
#endif
