// Home: family routines on the watch. https://github.com/lstux/HomePlanner
#include "config.h"

#include "homeplanner_app.h"
#include "hp_ui.h"

#include "gui/app.h"
#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"

static uint32_t homeplanner_tile_num;
static icon_t *homeplanner_app = NULL;

LV_IMG_DECLARE( hp_icon_64px );

static void enter_homeplanner_event_cb( lv_obj_t * obj, lv_event_t event );

/* registers itself: no change needed in the firmware */
static int registed = app_autocall_function( &homeplanner_app_setup, APP_PRIO( APP_GROUP_GENERAL, 4 ) );

void homeplanner_app_setup( void ) {
    if ( !registed )
        return;

    homeplanner_tile_num = mainbar_add_app_tile( 1, 1, "home app" );
    homeplanner_app = app_register( "home", &hp_icon_64px, enter_homeplanner_event_cb );
    hp_ui_setup( homeplanner_tile_num );
}

static void enter_homeplanner_event_cb( lv_obj_t * obj, lv_event_t event ) {
    if ( event == LV_EVENT_CLICKED ) {
        app_hide_indicator( homeplanner_app );
        mainbar_jump_to_tilenumber( homeplanner_tile_num, LV_ANIM_OFF, true );
    }
}
