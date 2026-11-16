#ifndef KEY_INPUT_H
#define KEY_INPUT_H
#include <stdbool.h>
#include <stdint.h>
#define KEY_DEBOUNCE_MS 30U
#define KEY_REPEAT_DELAY_MS 500U
#define KEY_REPEAT_MS 150U
#define KEY_DEBUG_HOLD_MS 5000U
#define KEY_MAX_SAMPLE_GAP_MS 50U
typedef enum { KEY_OK, KEY_CANCEL, KEY_UP, KEY_DOWN, KEY_DEBUG } KeyCode;
#define KEY_MASK(k) ((uint8_t)(1U<<(k)))
typedef struct {
    uint32_t candidate_ms[4], repeat_ms[4], last_ms, release_ms, chord_ms;
    uint32_t gaps, dropped;
    uint8_t candidate, stable, pending, queue[16], head, count;
    bool armed, releasing, chord, chord_fired;
} KeyInput;
/* Input bits mean pressed. Board adapter handles active-LOW electrical inputs.
 * Call at least every 50ms; unobserved gaps never count toward a long hold.
 * Short OK/Cancel emit on release. OK alone held 5s emits DEBUG once and
 * consumes release. UP/DOWN emit on press plus bounded repeat. */
void KeyInput_Init(KeyInput *k, uint32_t now);
void KeyInput_Update(KeyInput *k, uint8_t pressed, uint32_t now);
bool KeyInput_Read(KeyInput *k, KeyCode *event);
void KeyInput_RequireRelease(KeyInput *k);
#endif
