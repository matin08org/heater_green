#include "HmiCard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern const HmiImage2 demo_card;
extern const HmiFont demo_font;
int main(int argc,char **argv)
{
    assert(argc==2);
    uint16_t full[120*80], tiles[120*80];
    HmiCard card={.background=&demo_card,.font=&demo_font,.screen_rectangle={40,30,120,80},.title="FLAME TEMP",.value="700.25 C"};
    const uint16_t palette[]={0x0841,0x07e0,0xffff,0x07e0};
    HmiPalette2_Set(&card.palette,palette);
    HmiSurface s={full,120*80,120,80,120};
    assert(HmiCard_Compose(&card,card.screen_rectangle,&s)==HMI_OK);
    for(unsigned y=0;y<80;y+=4) {
        HmiSurface tile={tiles+y*120,480,120,4,120};
        assert(HmiCard_Compose(&card,(HmiRect){40,30+(int)y,120,4},&tile)==HMI_OK);
    }
    assert(!memcmp(full,tiles,sizeof(full)));
    FILE *file=fopen(argv[1],"wb");assert(file);
    assert(fwrite(full,sizeof(full),1,file)==1);assert(!fclose(file));
    puts("PASS real asset pipeline: PNG -> 2400-byte I2 -> RGB565 + generated ASCII font; full and four-row rendering match");
    return 0;
}
