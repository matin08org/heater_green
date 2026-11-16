#ifndef HMI_HOME_H
#define HMI_HOME_H
#include "HmiPages.h"
/* Presentation-only cached values. No hardware reads and no actuator commands. */
typedef struct {
    bool automatic,configured,inhibited,running,starting,stopping,stopped_proven,faulted;
    bool process_proven,bypass_proven,co_valid,co_calibrated,co2_valid,co2_link_stale;
    uint16_t co_ppm,co_limit_ppm,co_accept_ppm,co2_ppm,target_ppm;
} HmiHomeState;
void HmiHome_Apply(HmiPageView *view,const HmiHomeState *state);
/* Four RGB565 entries: [0] background, [1] border, [2] title, [3] graphic.
 * Each panel stores 2-bit indices, not four separate copies of the bitmap.
 * UI/main context only, never ISR. Applied to the next immutable render snapshot.
 * NULL restores the configured palette; unknown panel IDs return false. */
bool HmiHome_SetPanelPalette(const char *panel_id,const uint16_t colors[4]);
#endif
