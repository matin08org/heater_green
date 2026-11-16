#ifndef HMI_DIAG_H
#define HMI_DIAG_H
#include "HmiText.h"
#include "KeyInput.h"
#include "DiagLog.h"
#define HMI_DIAG_LINES 16U
#define HMI_DIAG_COLUMNS 64U
typedef enum {HMI_DIAG_LOG,HMI_DIAG_INPUTS,HMI_DIAG_OUTPUTS,HMI_DIAG_ADC,
    HMI_DIAG_SENSORS,HMI_DIAG_COMMS,HMI_DIAG_PROCESS,HMI_DIAG_SYSTEM,HMI_DIAG_SECTIONS} HmiDiagSection;
typedef struct {
    bool open, live;
    uint16_t scroll;
    uint32_t seen_sequence, revision;
    HmiDiagSection section;
} HmiDiag;
/* Immutable render snapshot. Capture only when the prior stream job has
 * completed. Live log writes never mutate a buffer being rendered/transmitted. */
typedef struct {
    const HmiFont *font;
    uint16_t width, height;
    uint8_t line_count, columns;
    uint16_t background;
    char header[HMI_DIAG_COLUMNS], status[HMI_DIAG_COLUMNS], footer[HMI_DIAG_COLUMNS];
    char lines[HMI_DIAG_LINES][HMI_DIAG_COLUMNS];
    DiagLevel levels[HMI_DIAG_LINES];
} HmiDiagView;
void HmiDiag_Init(HmiDiag *diag);
/* Returns true only when entering/leaving the page. Long OK is ignored while
 * already in diag. Short OK selects the next section; arrows browse entries.
 * UP pauses LOG tail-following, DOWN to newest restores LIVE. */
bool HmiDiag_Key(HmiDiag *diag, KeyCode key);
HmiResult HmiDiag_Capture(HmiDiag *diag, HmiDiagView *view,
                         const HmiFont *font, uint16_t width, uint16_t height,
                         const char *status);
/* Same tile compositor interface as HmiStream. Also supports cropped tiles. */
HmiResult HmiDiag_Compose(void *snapshot, HmiRect tile, HmiSurface *surface);
#endif
