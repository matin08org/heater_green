#include "KeyInput.h"
#include <string.h>
static void Emit(KeyInput *k, KeyCode event)
{
    if (k->count==sizeof(k->queue)) { k->dropped++; return; }
    k->queue[(k->head+k->count)%sizeof(k->queue)]=(uint8_t)event;
    k->count++;
}
void KeyInput_Init(KeyInput *k, uint32_t now)
{ memset(k,0,sizeof(*k)); k->last_ms=now; }
void KeyInput_RequireRelease(KeyInput *k)
{
    k->armed=k->releasing=k->chord=k->chord_fired=false;
    k->pending=k->head=k->count=0U;
}
bool KeyInput_Read(KeyInput *k, KeyCode *event)
{
    if (!k->count || !event) return false;
    *event=(KeyCode)k->queue[k->head];
    k->head=(uint8_t)((k->head+1U)%sizeof(k->queue)); k->count--;
    return true;
}
void KeyInput_Update(KeyInput *k, uint8_t raw, uint32_t now)
{
    raw&=15U;
    if ((uint32_t)(now-k->last_ms)>KEY_MAX_SAMPLE_GAP_MS) {
        k->gaps++;
        KeyInput_RequireRelease(k); /* No invented 5s hold across a CPU stall. */
        k->candidate=raw;
        for (unsigned i=0;i<4U;i++) k->candidate_ms[i]=now;
    }
    k->last_ms=now;
    uint8_t old=k->stable;
    for (unsigned i=0;i<4U;i++) {
        uint8_t bit=KEY_MASK(i);
        if ((raw&bit)!=(k->candidate&bit)) {
            k->candidate^=bit; k->candidate_ms[i]=now;
        }
        if ((uint32_t)(now-k->candidate_ms[i])>=KEY_DEBOUNCE_MS)
            k->stable=(k->stable&~bit)|(k->candidate&bit);
    }
    if (!k->armed) {
        if (raw || k->stable) k->releasing=false;
        else if (!k->releasing) { k->releasing=true; k->release_ms=now; }
        else if ((uint32_t)(now-k->release_ms)>=KEY_DEBOUNCE_MS) k->armed=true;
        return;
    }
    const uint8_t combo=KEY_MASK(KEY_OK)|KEY_MASK(KEY_CANCEL);
    if (k->chord_fired) {
        k->pending=0U;
        if (!k->stable) k->chord=k->chord_fired=false;
        return; /* Long OK and its release never become a short OK. */
    }
    if (k->stable==KEY_MASK(KEY_OK)) {
        if (!k->chord) {k->chord=true;k->chord_ms=now;}
        if ((uint32_t)(now-k->chord_ms)>=KEY_DEBUG_HOLD_MS) {
            k->pending=0U;Emit(k,KEY_DEBUG);k->chord_fired=true;return;
        }
    } else k->chord=false;
    if (k->stable && (k->stable&(k->stable-1U))) { k->pending=0U; return; }
    uint8_t down=k->stable&~old, up=old&~k->stable;
    k->pending|=down&combo;
    for (unsigned i=0;i<2U;i++) {
        uint8_t bit=KEY_MASK(i);
        if ((up&bit) && (k->pending&bit)) { Emit(k,(KeyCode)i); k->pending&=~bit; }
    }
    for (unsigned i=KEY_UP;i<=KEY_DOWN;i++) {
        uint8_t bit=KEY_MASK(i);
        if (down&bit) { Emit(k,(KeyCode)i); k->repeat_ms[i]=now; }
        else if ((k->stable&bit) &&
                 (uint32_t)(now-k->candidate_ms[i])>=KEY_REPEAT_DELAY_MS &&
                 (uint32_t)(now-k->repeat_ms[i])>=KEY_REPEAT_MS) {
            Emit(k,(KeyCode)i); k->repeat_ms[i]=now; /* No catch-up bursts. */
        }
    }
}
