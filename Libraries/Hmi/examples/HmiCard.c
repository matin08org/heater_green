#include "HmiCard.h"
HmiResult HmiCard_Compose(void *snapshot, HmiRect tile, HmiSurface *s)
{
    HmiCard *card = snapshot;
    if (!card || !card->background || !card->font ||
        card->screen_rectangle.w != card->background->width ||
        card->screen_rectangle.h != card->background->height ||
        tile.w != s->width || tile.h != s->height ||
        tile.x < card->screen_rectangle.x || tile.y < card->screen_rectangle.y ||
        (int64_t)tile.x + tile.w > (int64_t)card->screen_rectangle.x + card->screen_rectangle.w ||
        (int64_t)tile.y + tile.h > (int64_t)card->screen_rectangle.y + card->screen_rectangle.h)
        return HMI_INVALID;
    int32_t local_x = tile.x - card->screen_rectangle.x;
    int32_t local_y = tile.y - card->screen_rectangle.y;
    HmiResult result = HmiImage2_Draw(s, card->background,
        (HmiRect){local_x,local_y,tile.w,tile.h}, 0,0, &card->palette);
    if (result != HMI_OK) return result;
    if (card->screen_rectangle.w < 16U || card->screen_rectangle.h < 48U) return HMI_INVALID;
    HmiTextStyle style = {card->palette.colors[2],0,HMI_TEXT_CENTER,false};
    HmiRect title = {8-local_x,8-local_y,(uint16_t)(card->screen_rectangle.w-16U),card->font->height};
    result = HmiText_Draw(s,title,card->font,card->title,sizeof(card->title),style);
    if(result != HMI_OK) return result;
    HmiRect value = {8-local_x,36-local_y,(uint16_t)(card->screen_rectangle.w-16U),card->font->height};
    return HmiText_Draw(s,value,card->font,card->value,sizeof(card->value),style);
}
