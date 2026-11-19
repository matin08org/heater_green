#include "UiNavigation.h"
#include <string.h>
void UiNavigation_Init(UiNavigation *n) { memset(n,0,sizeof(*n));n->focus=1;n->fan_ccr=1000;n->fan_max=1000; }
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
    if (n->page==UI_PAGE_FAN) {
        if(key==KEY_CANCEL) {
            if(n->editing){n->editing=false;return NAV_NONE;}
            n->page=UI_PAGE_SETTINGS;n->focus=5;return NAV_PAGE;
        }
        if(key==KEY_OK) {
            if(n->focus==3U)return NAV_FAN_OFF;
            if(n->focus==4U){n->page=UI_PAGE_SETTINGS;n->focus=5;return NAV_PAGE;}
            if(!n->editing) {
                n->fan_draft=n->focus==0U?n->fan_ccr:n->focus==1U?n->fan_min:n->fan_max;
                n->editing=true;return NAV_NONE;
            }
            n->editing=false;
            if(n->focus==0U){n->fan_ccr=n->fan_draft;return NAV_FAN_APPLY;}
            if(n->focus==1U)n->fan_min=n->fan_draft;else n->fan_max=n->fan_draft;
            if(n->fan_ccr<n->fan_min)n->fan_ccr=n->fan_min;
            if(n->fan_ccr>n->fan_max)n->fan_ccr=n->fan_max;
            return NAV_FAN_LIMITS;
        }
        if(key==KEY_UP||key==KEY_DOWN) {
            if(!n->editing)n->focus=(uint8_t)((n->focus+(key==KEY_UP?4U:1U))%5U);
            else {
                uint16_t low=n->focus==0U?n->fan_min:n->focus==2U?n->fan_min+50U:0U;
                uint16_t high=n->focus==0U?n->fan_max:n->focus==1U?n->fan_max-50U:1000U;
                int next=(int)n->fan_draft+(key==KEY_UP?50:-50);
                n->fan_draft=(uint16_t)(next<low?low:next>high?high:next);
            }
        }
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
            if (n->focus==3U) return NAV_RELAY_TEST;
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
        else if (n->focus==5U) {n->page=UI_PAGE_FAN;n->focus=0;return NAV_PAGE;}
        else { n->page=UI_PAGE_HOME; n->focus=1; return NAV_PAGE; }
    } else if (key==KEY_UP || key==KEY_DOWN) {
        if (!n->editing) n->focus=(uint8_t)((n->focus+(key==KEY_UP?6U:1U))%7U);
        else if (n->focus==0U) n->draft_auto=!n->draft_auto;
        else if (key==KEY_UP && n->draft_co2<=4990U) n->draft_co2+=10U;
        else if (key==KEY_DOWN && n->draft_co2>=110U) n->draft_co2-=10U;
    }
    return NAV_NONE;
}
