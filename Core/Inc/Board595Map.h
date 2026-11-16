#ifndef BOARD_595_MAP_H
#define BOARD_595_MAP_H

#include "HC595.h"

/* U18 / 74HC595D: connectivity transcribed from the supplied schematic.
 * User confirmed relay drivers are active HIGH. Connectivity/polarity does
 * not constitute permission to energize fuel or other loads.
 * Use the HC595 shared-shadow API; never bypass it to latch these outputs.
 */
#define BOARD595_MAX6675_CS      HC595_QA /* Q0, pin 15; MAX6675 owner only */
#define BOARD595_IGNITION        HC595_QB /* Q1, pin 1: ign */
#define BOARD595_EXCHANGER_FAN   HC595_QC /* Q2, pin 2: large exchanger fan */
#define BOARD595_COOLER_FAN      BOARD595_EXCHANGER_FAN /* legacy alias: NOT the sample fan */
#define BOARD595_SAMPLE_VALVE_1  HC595_QD /* Q3, pin 3: shir_1_smpl_colr */
#define BOARD595_SAMPLE_VALVE_2  HC595_QE /* Q4, pin 4: shir_2_smpl_colr */
#define BOARD595_FAN_Q5          HC595_QF /* Q5, pin 5: fan_pakskonande */
#define BOARD595_DISTRIBUTION_FAN BOARD595_FAN_Q5 /* Confirmed by user: distribution fan */
#define BOARD595_AUXILIARY       HC595_QG /* Q6, pin 6: Auxiliary_Output */
#define BOARD595_SAMPLE_COOLER_FAN BOARD595_AUXILIARY /* confirmed Q6: small sample-cooler fan */
#define BOARD595_GAS_VALVE       HC595_QH /* Q7, pin 7: shir_gas */
#define BOARD595_RELAY_ON        HC595_ON
#define BOARD595_RELAY_OFF       HC595_OFF

/* Q5 must not be silently substituted for MCU PA8 (fan_ehtragh).
 * HC595_WritePin queues a change; it does not guarantee immediate gas closure
 * or a zero-cross-aligned edge. Fuel isolation needs an independent verified
 * safety path.
 */

#endif
