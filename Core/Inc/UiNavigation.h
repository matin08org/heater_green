#ifndef UI_NAVIGATION_H
#define UI_NAVIGATION_H
#include "KeyInput.h"
typedef enum { UI_PAGE_HOME, UI_PAGE_SETTINGS, UI_PAGE_DEBUG, UI_PAGE_SERVICE, UI_PAGE_FAN } UiPage;
typedef enum { NAV_NONE, NAV_PAGE, NAV_SAVE_MODE, NAV_SAVE_CO2, NAV_START,
               NAV_STOP, NAV_RESET, NAV_SCROLL_UP, NAV_SCROLL_DOWN, NAV_LIVE,
               NAV_STEP_PLUS,NAV_STEP_MINUS,NAV_MOTOR_STOP,NAV_RELAY_TEST,NAV_CONFIRM_EXHAUST,
               NAV_TEST_CONFIRM,NAV_TEST_ARM,NAV_TEST_DISARM,
               NAV_FAN_APPLY,NAV_FAN_LIMITS,NAV_FAN_OFF } UiAction;
typedef struct { UiPage page; uint8_t focus; bool editing; uint16_t draft_co2; bool draft_auto,output_confirm;
    uint16_t fan_ccr,fan_min,fan_max,fan_draft;
} UiNavigation;
void UiNavigation_Init(UiNavigation *n);
UiAction UiNavigation_Key(UiNavigation *n,KeyCode key,bool current_auto,uint16_t current_co2);
#endif
