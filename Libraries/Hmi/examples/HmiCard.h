#ifndef HMI_CARD_H
#define HMI_CARD_H
#include "HmiImage.h"
#include "HmiText.h"
/* Immutable snapshot copied by the application before HmiStream_Submit. */
typedef struct {
    const HmiImage2 *background;
    const HmiFont *font;
    HmiPalette2 palette;
    HmiRect screen_rectangle;
    char title[24], value[20];
} HmiCard;
/* Example compositor: a reusable indexed card and two dynamic text fields.
 * Call from HmiStream or directly for a host preview. */
HmiResult HmiCard_Compose(void *snapshot, HmiRect tile, HmiSurface *surface);
#endif
