#include "UiNavigation.h"
#include <string.h>
void UiNavigation_Init(UiNavigation *n) { memset(n,0,sizeof(*n));n->focus=1; }
UiAction UiNavigation_Key(UiNavigation *n,KeyCode key,bool auto_mode,uint16_t co2)
{
    if(key!=KEY_OK)n->output_confirm=false;
    if (key==KEY_DEBUG) {
        if (n->page==UI_PAGE_DEBUG) return NAV_NONE;
        n->page=UI_PAGE_DEBUG; n->editing=false; return NAV_PAGE;
    }
    if (n->page==UI_PAGE_DEBUG) {
        if (key==KEY_CANCEL) { n->page=UI_PAGE_HOME; n->focus=1; return NAV_PAGE; }
        if (key==KEY_UP) return NAV_SCROLL_UP;
        if (key==KEY_DOWN) return NAV_SCROLL_DOWN;
        if (key==KEY_OK) return NAV_LIVE;
        return NAV_NONE;
    }
    if (n->page==UI_PAGE_SERVICE) {
        if (key==KEY_CANCEL) {n->page=UI_PAGE_HOME;n->focus=1;return NAV_PAGE;}
        if (key==KEY_UP) n->focus=(n->focus+6U)%7U;
        if (key==KEY_DOWN) n->focus=(n->focus+1U)%7U;
        if (key==KEY_OK) {
            if (n->focus==0U) return NAV_STEP_PLUS;
            if (n->focus==1U) return NAV_STEP_MINUS;
            if (n->focus==2U) return NAV_MOTOR_STOP;
            if (n->focus==3U) return NAV_BEEP;
            if (n->focus==4U) return NAV_CONFIRM_EXHAUST;
            if (n->focus==5U) return NAV_RESET;
            n->page=UI_PAGE_HOME;n->focus=1;return NAV_PAGE;
        }
        return NAV_NONE;
    }
    if (n->page==UI_PAGE_HOME) {
        if (key==KEY_UP) n->focus=0; /* RUN */
        if (key==KEY_DOWN) n->focus=1; /* STOP */
        if (key==KEY_OK) return n->focus==0?NAV_START:NAV_STOP;
        if (key==KEY_CANCEL) {n->page=UI_PAGE_SETTINGS;n->focus=0;return NAV_PAGE;}
        return NAV_NONE;
    }
    if (key==KEY_CANCEL) {
        if (n->editing) n->editing=false; /* Roll back the uncommitted draft. */
        else { n->page=UI_PAGE_HOME; n->focus=1; return NAV_PAGE; }
    } else if (key==KEY_OK) {
        if (n->editing) { n->editing=false; return n->focus==0U?NAV_SAVE_MODE:NAV_SAVE_CO2; }
        if (n->focus<2U) { n->draft_auto=auto_mode; n->draft_co2=co2; n->editing=true; }
        else if (n->focus==2U) return NAV_START;
        else if (n->focus==3U) return NAV_STOP;
        else if (n->focus==4U) {n->page=UI_PAGE_SERVICE;n->focus=0;return NAV_PAGE;}
        else { n->page=UI_PAGE_HOME; n->focus=1; return NAV_PAGE; }
    } else if (key==KEY_UP || key==KEY_DOWN) {
        if (!n->editing) n->focus=(uint8_t)((n->focus+(key==KEY_UP?5U:1U))%6U);
        else if (n->focus==0U) n->draft_auto=!n->draft_auto;
        else if (key==KEY_UP && n->draft_co2<=4990U) n->draft_co2+=10U;
        else if (key==KEY_DOWN && n->draft_co2>=110U) n->draft_co2-=10U;
    }
    return NAV_NONE;
}
