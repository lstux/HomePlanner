// Home on the watch: today's routine, steps to tick, countdown, synchronization.
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <string>

#ifndef NATIVE_64BIT
    #include <Arduino.h>
    #include <FS.h>
    #include <HTTPClient.h>
    #include <SPIFFS.h>
    #include <WiFi.h>
    #include "hardware/wifictl.h"
#endif

#include "hp_core.h"
#include "hp_config.h"
#include "hp_ui.h"

#include "gui/keyboard.h"
#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"

LV_FONT_DECLARE( hp_16px );
LV_FONT_DECLARE( hp_22px );
LV_IMG_DECLARE( hp_fox_content_64 );
LV_IMG_DECLARE( hp_fox_bravo_64 );
LV_IMG_DECLARE( hp_fox_comfort_64 );
LV_IMG_DECLARE( hp_rabbit_content_64 );
LV_IMG_DECLARE( hp_rabbit_think_64 );

#define HP_CONFIG_CACHE     "/hp_family.json"
#define HP_PROGRESS_CACHE   "/hp_day.json"
#define HP_MAX_STEPS        24
#define HP_MARGIN           6
#define HP_GAUGE_H          22
#define HP_VALID_TIME       1767225600      /* 2026-01-01: older means the clock was never set */
#define HP_SYNC_DELAY_MS    3000            /* grouped ticks before sending */
#define HP_WIFI_WAIT_MS     12000
#define HP_MAX_ANSWER       ( 120 * 1024 )

static hp_settings_t hp_settings;
static hp::Config hp_config;
static hp::Progress hp_progress;
static std::string hp_config_json;

static lv_obj_t *hp_tile, *hp_page, *hp_exit_btn, *hp_refresh_btn, *hp_setup_btn;
static lv_obj_t *hp_head_img, *hp_head_text, *hp_status_lbl;
static lv_obj_t *hp_gauge, *hp_gauge_lbl, *hp_gauge_fill, *hp_gauge_dot, *hp_timer_lbl, *hp_go_btn;
static int hp_shown_next = -1, hp_shown_state = -1;
static time_t hp_shown_second = 0;
static bool hp_active = false;
static lv_obj_t *hp_url_area, *hp_key_area;
static lv_obj_t *hp_member_btn, *hp_routine_btn;
static lv_obj_t *hp_step_btn[ HP_MAX_STEPS ];
static lv_style_t hp_style16, hp_style22;
static lv_task_t *hp_task;

static bool hp_setup_screen = false;
static bool hp_rebuild = false;             /* screens are rebuilt by the task, never inside an event */
static int hp_manual_routine = -1;
static int hp_shown_routine = -1;
static int hp_shown_minute = -1;
static uint32_t hp_sync_at = 0;             /* lv_tick of the next synchronization, 0 none */
static char hp_status[ 64 ] = "";

/* one synchronization at a time, in its own task */
struct hp_job_t {
    std::string url, key, member;
    hp::Progress progress;
    hp::SyncResult result;
};
static hp_job_t *hp_job = NULL;
static volatile bool hp_job_done = false;

/* ---- time, storage ------------------------------------------------------ */

static bool hp_now( struct tm *info ) {
    time_t now = time( NULL );
    localtime_r( &now, info );
    return( now >= HP_VALID_TIME );
}

static int64_t hp_now_ms( void ) {
    struct timeval tv;
    gettimeofday( &tv, NULL );
    return( (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000 );
}

static bool hp_read_file( const char *name, std::string &out ) {
#ifndef NATIVE_64BIT
    fs::File file = SPIFFS.open( name, FILE_READ );
    if ( !file )
        return( false );
    out.resize( file.size() );
    size_t got = out.empty() ? 0 : file.read( (uint8_t*)&out[ 0 ], out.size() );
    file.close();
    out.resize( got );
    return( got > 0 );
#else
    return( false );
#endif
}

static void hp_write_file( const char *name, const std::string &data ) {
#ifndef NATIVE_64BIT
    fs::File file = SPIFFS.open( name, FILE_WRITE );
    if ( !file )
        return;
    if ( file.write( (const uint8_t*)data.data(), data.size() ) != data.size() )
        snprintf( hp_status, sizeof( hp_status ), "M\xC3\xA9moire de la montre pleine" );
    file.close();
#endif
}

/* a new day starts with nothing ticked */
static void hp_new_day( void ) {
    struct tm now;
    if ( !hp_now( &now ) )
        return;
    char date[ 16 ];
    strftime( date, sizeof( date ), "%Y-%m-%d", &now );
    if ( hp_progress.date != date ) {
        hp_progress.date = date;
        hp_progress.done.clear();
        hp_manual_routine = -1;
        hp_rebuild = true;
    }
}

/* ---- synchronization ---------------------------------------------------- */

#ifndef NATIVE_64BIT

/* collects the answer, whatever the transfer encoding */
class hp_sink_t : public Stream {
    public:
    explicit hp_sink_t( std::string &out ) : out_( out ) {}
    size_t write( uint8_t c ) override { return( write( &c, 1 ) ); }
    size_t write( const uint8_t *data, size_t size ) override {
        if ( out_.size() + size > HP_MAX_ANSWER )
            return( 0 );
        out_.append( (const char*)data, size );
        return( size );
    }
    int available() override { return( 0 ); }
    int read() override { return( -1 ); }
    int peek() override { return( -1 ); }
    void flush() override {}
    private:
    std::string &out_;
};

class hp_http_t : public hp::Transport {
    public:
    hp_http_t( const std::string &url, const std::string &key ) : url_( url ), key_( key ) {}
    int get( const char *path, std::string &response ) override { return( request( path, NULL, response ) ); }
    int post( const char *path, const std::string &body, std::string &response ) override { return( request( path, &body, response ) ); }

    private:
    int request( const char *path, const std::string *body, std::string &response ) {
        HTTPClient http;
        String url = String( url_.c_str() ) + path;
        response.clear();
        if ( !http.begin( url ) )
            return( -1 );
        http.setTimeout( 8000 );
        http.addHeader( "X-HomePlanner-Key", key_.c_str() );
        if ( body )
            http.addHeader( "Content-Type", "application/json" );
        int code = body ? http.POST( (uint8_t*)body->data(), body->size() ) : http.GET();
        if ( code == 200 ) {
            hp_sink_t sink( response );
            if ( http.writeToStream( &sink ) < 0 )
                code = -1;
        }
        http.end();
        return( code );
    }
    std::string url_, key_;
};

static void hp_worker( void *arg ) {
    hp_job_t *job = (hp_job_t*)arg;

    if ( !WiFi.isConnected() ) {
        wifictl_on();
        for ( int waited = 0; waited < HP_WIFI_WAIT_MS && !WiFi.isConnected(); waited += 250 )
            vTaskDelay( 250 / portTICK_PERIOD_MS );
    }
    if ( WiFi.isConnected() ) {
        hp_http_t http( job->url, job->key );
        job->result = hp::synchronize( http, job->member, job->progress, esp_random() );
    }
    hp_job_done = true;
    vTaskDelete( NULL );
}

#endif

static void hp_set_status( const char *text ) {
    snprintf( hp_status, sizeof( hp_status ), "%s", text );
    if ( hp_status_lbl )
        lv_label_set_text( hp_status_lbl, hp_status );
}

static void hp_start_sync( void ) {
    struct tm now;
    if ( hp_job || hp_setup_screen )
        return;
    if ( !*hp_settings.url || !*hp_settings.key ) {
        hp_set_status( "R\xC3\xA8gle l'adresse et le code" );
        return;
    }
    if ( !hp_now( &now ) ) {
        hp_set_status( "Mets la montre \xC3\xA0 l'heure" );
        return;
    }
#ifndef NATIVE_64BIT
    hp_new_day();
    hp_job = new hp_job_t();
    hp_job->url = hp_settings.url;
    hp_job->key = hp_settings.key;
    hp_job->member = hp_settings.member;
    hp_job->progress = hp_progress;
    hp_job_done = false;
    hp_set_status( "Synchronisation..." );
    if ( xTaskCreatePinnedToCore( hp_worker, "home sync", 12288, hp_job, 1, NULL, 1 ) != pdPASS ) {
        delete hp_job;
        hp_job = NULL;
        hp_set_status( "M\xC3\xA9moire insuffisante" );
    }
#endif
}

static void hp_finish_sync( void ) {
    hp::SyncResult &r = hp_job->result;

    if ( !r.config_json.empty() && r.config_json != hp_config_json && hp::parseConfig( r.config_json.c_str(), hp_config ) ) {
        hp_config_json = r.config_json;
        hp_write_file( HP_CONFIG_CACHE, hp_config_json );
        hp_rebuild = true;
    }
    /* merged into the current marks: ticks made during the exchange are kept */
    if ( r.progress.date == hp_progress.date && !r.progress.done.empty() ) {
        for ( const auto &item : r.progress.done )
            hp::mergeMarks( hp_progress.done[ item.first ], item.second );
        hp_write_file( HP_PROGRESS_CACHE, hp::progressToJson( hp_progress ) );
        hp_shown_minute = -1;
    }
    switch ( r.status ) {
        case hp::SYNC_OK:           hp_set_status( "A jour avec la maison" ); break;
        case hp::SYNC_PENDING:      hp_set_status( "Synchronisation..." ); hp_sync_at = lv_tick_get() + HP_SYNC_DELAY_MS; break;
        case hp::SYNC_BAD_KEY:      hp_set_status( "Code d'appairage refus\xC3\xA9" ); break;
        case hp::SYNC_NO_CONFIG:    hp_set_status( "Ouvre d'abord Home sur un t\xC3\xA9l\xC3\xA9phone" ); break;
        case hp::SYNC_BAD_ANSWER:   hp_set_status( "R\xC3\xA9ponse illisible" ); break;
        default:                    hp_set_status( "Maison injoignable" ); break;
    }
    delete hp_job;
    hp_job = NULL;
}

/* ---- screens ------------------------------------------------------------ */

static lv_coord_t hp_width( void ) {
    return( lv_disp_get_hor_res( NULL ) - 2 * HP_MARGIN );
}

static const hp::Member *hp_member( void ) {
    if ( !hp_config.valid() )
        return( NULL );
    for ( const hp::Member &m : hp_config.members )
        if ( m.id == hp_settings.member )
            return( &m );
    return( &hp_config.members[ 0 ] );
}

static lv_obj_t *hp_label( lv_obj_t *parent, const char *text, lv_style_t *style, lv_coord_t width ) {
    lv_obj_t *label = lv_label_create( parent, NULL );
    lv_label_set_long_mode( label, LV_LABEL_LONG_BREAK );
    lv_obj_set_width( label, width );
    lv_obj_add_style( label, LV_LABEL_PART_MAIN, style );
    lv_label_set_text( label, text );
    return( label );
}

static lv_obj_t *hp_button( const char *text, lv_event_cb_t cb ) {
    lv_obj_t *btn = lv_btn_create( hp_page, NULL );
    lv_obj_set_width( btn, hp_width() );
    lv_btn_set_fit2( btn, LV_FIT_NONE, LV_FIT_TIGHT );
    lv_obj_add_style( btn, LV_BTN_PART_MAIN, ws_get_button_style() );
    lv_obj_set_style_local_pad_top( btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 9 );
    lv_obj_set_style_local_pad_bottom( btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 9 );
    lv_obj_set_event_cb( btn, cb );
    lv_obj_t *label = hp_label( btn, text, &hp_style16, hp_width() - 12 );
    lv_label_set_align( label, LV_LABEL_ALIGN_CENTER );
    lv_tileview_add_element( hp_tile, btn );
    return( btn );
}

static void hp_update( void );

static void hp_set_hidden( lv_obj_t *obj, bool hidden ) {
    if ( lv_obj_get_hidden( obj ) != hidden )
        lv_obj_set_hidden( obj, hidden );
}

/* gauge and countdown of the current step, refreshed every second */
static void hp_update_timing( const hp::Routine &routine, const hp::Evaluation &e, bool clock ) {
    if ( !hp_gauge )
        return;
    bool shown = clock && e.state != hp::STATE_EMPTY;
    hp_set_hidden( hp_gauge, !shown );
    hp_set_hidden( hp_gauge_lbl, !shown );
    if ( shown ) {
        /* the middle is "on time": the bar grows to the right when ahead, to the left when late */
        lv_coord_t half = ( hp_width() - HP_GAUGE_H ) / 2;
        lv_coord_t offset = half * hp::gaugePercent( e.slack ) / 100;
        lv_color_t tone = lv_color_hex( e.slack >= 300 ? 0x2E7D32 : e.slack > -60 ? 0xE0A100 : 0xD9532C );
        lv_coord_t middle = hp_width() / 2;
        lv_obj_set_pos( hp_gauge_fill, offset < 0 ? middle + offset : middle, 5 );
        lv_obj_set_size( hp_gauge_fill, offset < 0 ? -offset : offset ? offset : 1, HP_GAUGE_H - 10 );
        lv_obj_set_pos( hp_gauge_dot, middle + offset - HP_GAUGE_H / 2, 0 );
        lv_obj_set_style_local_bg_color( hp_gauge_fill, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, tone );
        lv_obj_set_style_local_bg_color( hp_gauge_dot, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, tone );
        std::string text = hp::gaugeText( e.slack );
        if ( e.losing )
            text += e.slack >= 0 ? "\nl'avance fond" : "\n\xC3\xA7""a grandit";
        lv_label_set_text( hp_gauge_lbl, text.c_str() );
    }
    bool timer = clock && e.next >= 0 && e.state != hp::STATE_DONE;
    hp_set_hidden( hp_timer_lbl, !timer );
    hp_set_hidden( hp_go_btn, !timer || e.started );
    if ( timer ) {
        int seconds = e.step_left < 0 ? -e.step_left : e.step_left;
        char text[ 96 ];
        snprintf( text, sizeof( text ), "%s\n%s%d:%02d", routine.steps[ e.next ].label.c_str(),
                  e.step_left < 0 ? "+" : "", seconds / 60, seconds % 60 );
        lv_label_set_text( hp_timer_lbl, text );
        lv_obj_set_style_local_text_color( hp_timer_lbl, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT,
                                           e.step_left < 0 ? lv_color_hex( 0xFF7043 ) : LV_COLOR_WHITE );
    }
}

/* every second: only the moving parts, the whole screen when the situation changes */
static void hp_tick( void ) {
    const hp::Member *member = hp_member();
    struct tm now;
    if ( hp_setup_screen || !member || hp_shown_routine < 0 || !hp_gauge || !hp_now( &now ) )
        return;
    const hp::Routine &routine = hp_config.routines[ hp_shown_routine ];
    hp::Evaluation e = hp::evaluate( routine, hp_progress.done[ member->id + "/" + routine.id ],
                                     now.tm_hour * 60 + now.tm_min, hp_now_ms() );
    if ( e.next != hp_shown_next || e.state != hp_shown_state )
        hp_update();
    else
        hp_update_timing( routine, e, true );
}

/* plain coloured rectangle, not clickable so the list still scrolls */
static lv_obj_t *hp_box( lv_obj_t *parent, lv_coord_t w, lv_coord_t h, uint32_t color, lv_coord_t radius ) {
    lv_obj_t *box = lv_obj_create( parent, NULL );
    lv_obj_set_size( box, w, h );
    lv_obj_set_click( box, false );
    lv_obj_set_style_local_bg_color( box, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex( color ) );
    lv_obj_set_style_local_bg_opa( box, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER );
    lv_obj_set_style_local_border_width( box, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0 );
    lv_obj_set_style_local_radius( box, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, radius );
    return( box );
}

/* refresh the texts and colours without rebuilding, so the list keeps its position */
static void hp_update( void ) {
    const hp::Member *member = hp_member();
    struct tm now;
    if ( hp_setup_screen || !member || hp_shown_routine < 0 || !hp_head_text )
        return;

    const hp::Routine &routine = hp_config.routines[ hp_shown_routine ];
    const hp::Marks &marks = hp_progress.done[ member->id + "/" + routine.id ];
    bool clock = hp_now( &now );
    hp::Evaluation e = hp::evaluate( routine, marks, clock ? now.tm_hour * 60 + now.tm_min : 0, clock ? hp_now_ms() : 0 );
    hp_shown_next = e.next;
    hp_shown_state = e.state;
    hp_update_timing( routine, e, clock );
    char text[ 96 ];
    const lv_img_dsc_t *mascot = &hp_rabbit_content_64;

    if ( e.state == hp::STATE_DONE ) {
        snprintf( text, sizeof( text ), "Bravo !\nTout est pr\xC3\xAAt\n%d/%d", e.done, e.total );
        mascot = &hp_fox_bravo_64;
    }
    else if ( !clock ) {
        snprintf( text, sizeof( text ), "Heure ?\n%d/%d", e.done, e.total );
    }
    else if ( e.state == hp::STATE_PASSED ) {
        snprintf( text, sizeof( text ), "L'heure est\npass\xC3\xA9""e\n%d/%d", e.done, e.total );
        mascot = &hp_fox_comfort_64;
    }
    else {
        const char *mood = e.state == hp::STATE_LATE ? "On se d\xC3\xA9p\xC3\xAA""che" : e.state == hp::STATE_TIGHT ? "C'est juste" : "\xC3\x87""a roule";
        if ( e.left >= 60 )
            snprintf( text, sizeof( text ), "%d h %02d\n%s\n%d/%d", e.left / 60, e.left % 60, mood, e.done, e.total );
        else
            snprintf( text, sizeof( text ), "%d min\n%s\n%d/%d", e.left, mood, e.done, e.total );
        if ( e.state == hp::STATE_LATE )
            mascot = &hp_fox_comfort_64;
        else if ( e.state == hp::STATE_TIGHT )
            mascot = &hp_rabbit_think_64;
        else if ( e.done )
            mascot = &hp_fox_content_64;
    }
    lv_label_set_text( hp_head_text, text );
    lv_img_set_src( hp_head_img, mascot );

    for ( size_t i = 0; i < routine.steps.size() && i < HP_MAX_STEPS; i++ ) {
        if ( !hp_step_btn[ i ] )
            continue;
        bool done = hp::isDone( marks, routine.steps[ i ].id );
        /* the font has no symbols: green shows a ticked step, an arrow the current one */
        std::string label = ( (int)i == e.next ? std::string( "\xC2\xBB " ) : std::string() ) + routine.steps[ i ].label;
        lv_label_set_text( lv_obj_get_child( hp_step_btn[ i ], NULL ), label.c_str() );
        if ( done ) {
            lv_obj_set_style_local_bg_color( hp_step_btn[ i ], LV_BTN_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex( 0x2E7D32 ) );
            lv_obj_set_style_local_text_color( hp_step_btn[ i ], LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE );
        }
        else {
            lv_obj_remove_style_local_prop( hp_step_btn[ i ], LV_BTN_PART_MAIN, LV_STYLE_BG_COLOR | ( LV_STATE_DEFAULT << LV_STYLE_STATE_POS ) );
            lv_obj_remove_style_local_prop( hp_step_btn[ i ], LV_BTN_PART_MAIN, LV_STYLE_TEXT_COLOR | ( LV_STATE_DEFAULT << LV_STYLE_STATE_POS ) );
        }
    }
}

static void hp_focus( hp::Marks &marks, const std::string &step ) {
    const std::string key = hp::goKey( step );
    marks[ key ] = hp::nextMark( marks[ key ], true, hp_now_ms() );
}

static void hp_marks_changed( void ) {
    hp_write_file( HP_PROGRESS_CACHE, hp::progressToJson( hp_progress ) );
    hp_update();
    hp_sync_at = lv_tick_get() + HP_SYNC_DELAY_MS;
}

static void hp_step_event_cb( lv_obj_t *obj, lv_event_t event ) {
    const hp::Member *member = hp_member();
    struct tm now;
    /* short press: tick or untick; long press: "I am doing this one" */
    if ( ( event != LV_EVENT_SHORT_CLICKED && event != LV_EVENT_LONG_PRESSED ) || !member || hp_shown_routine < 0 )
        return;
    if ( !hp_now( &now ) ) {
        hp_set_status( "Mets la montre \xC3\xA0 l'heure" );
        return;
    }
    hp_new_day();
    if ( hp_rebuild )
        return;                             /* the screen was yesterday's */
    const hp::Routine &routine = hp_config.routines[ hp_shown_routine ];
    for ( size_t i = 0; i < routine.steps.size() && i < HP_MAX_STEPS; i++ ) {
        if ( obj != hp_step_btn[ i ] )
            continue;
        hp::Marks &marks = hp_progress.done[ member->id + "/" + routine.id ];
        const std::string &step = routine.steps[ i ].id;
        if ( event == LV_EVENT_SHORT_CLICKED )
            marks[ step ] = hp::nextMark( marks[ step ], !hp::isDone( marks, step ), hp_now_ms() );
        else if ( !hp::isDone( marks, step ) )
            hp_focus( marks, step );
        hp_marks_changed();
    }
}

static void hp_go_event_cb( lv_obj_t *obj, lv_event_t event ) {
    const hp::Member *member = hp_member();
    struct tm now;
    if ( event != LV_EVENT_CLICKED || !member || hp_shown_routine < 0 || !hp_now( &now ) )
        return;
    hp_new_day();
    if ( hp_rebuild || hp_shown_next < 0 )
        return;
    const hp::Routine &routine = hp_config.routines[ hp_shown_routine ];
    if ( hp_shown_next < (int)routine.steps.size() ) {
        hp_focus( hp_progress.done[ member->id + "/" + routine.id ], routine.steps[ hp_shown_next ].id );
        hp_marks_changed();
    }
}

static void hp_member_event_cb( lv_obj_t *obj, lv_event_t event ) {
    const hp::Member *member = hp_member();
    if ( event != LV_EVENT_CLICKED || !member )
        return;
    size_t next = ( member - &hp_config.members[ 0 ] + 1 ) % hp_config.members.size();
    snprintf( hp_settings.member, sizeof( hp_settings.member ), "%s", hp_config.members[ next ].id.c_str() );
    hp_settings.save();
    hp_rebuild = true;
    hp_sync_at = lv_tick_get() + HP_SYNC_DELAY_MS;
}

static void hp_routine_event_cb( lv_obj_t *obj, lv_event_t event ) {
    if ( event != LV_EVENT_CLICKED || hp_shown_routine < 0 )
        return;
    hp_manual_routine = ( hp_shown_routine + 1 ) % hp_config.routines.size();
    hp_rebuild = true;
}

static void hp_textarea_event_cb( lv_obj_t *obj, lv_event_t event ) {
    if ( event == LV_EVENT_CLICKED )
        keyboard_set_textarea( obj );
}

static lv_obj_t *hp_textarea( const char *title, const char *value, size_t max ) {
    hp_label( hp_page, title, &hp_style16, hp_width() );
    lv_obj_t *area = lv_textarea_create( hp_page, NULL );
    lv_textarea_set_text( area, value );
    lv_textarea_set_max_length( area, max );
    lv_textarea_set_one_line( area, true );
    lv_textarea_set_cursor_hidden( area, true );
    lv_obj_set_width( area, hp_width() );
    lv_obj_set_event_cb( area, hp_textarea_event_cb );
    lv_tileview_add_element( hp_tile, area );
    return( area );
}

static void hp_build( void ) {
    keyboard_hide();
    lv_page_clean( hp_page );
    hp_head_img = hp_head_text = hp_status_lbl = hp_url_area = hp_key_area = NULL;
    hp_member_btn = hp_routine_btn = NULL;
    hp_gauge = hp_gauge_lbl = hp_gauge_fill = hp_gauge_dot = hp_timer_lbl = hp_go_btn = NULL;
    memset( hp_step_btn, 0, sizeof( hp_step_btn ) );
    hp_shown_routine = -1;
    lv_obj_set_hidden( hp_refresh_btn, hp_setup_screen );
    lv_obj_set_hidden( hp_setup_btn, hp_setup_screen );

    if ( hp_setup_screen ) {
        hp_url_area = hp_textarea( "Adresse de la centrale", *hp_settings.url ? hp_settings.url : "http://", HP_URL_LEN - 1 );
        hp_key_area = hp_textarea( "Code d'appairage", hp_settings.key, HP_KEY_LEN - 1 );
        hp_label( hp_page, "La fl\xC3\xA8""che enregistre.", &hp_style16, hp_width() );
    }
    else {
        const hp::Member *member = hp_member();
        if ( member ) {
            struct tm now;
            bool clock = hp_now( &now );
            hp_shown_routine = hp_manual_routine >= 0 && hp_manual_routine < (int)hp_config.routines.size() ? hp_manual_routine
                : (int)hp::suggestRoutine( hp_config, clock ? now.tm_wday : 1, clock ? now.tm_hour * 60 + now.tm_min : 0 );
            const hp::Routine &routine = hp_config.routines[ hp_shown_routine ];

            hp_routine_btn = hp_button( routine.name.c_str(), hp_routine_event_cb );

            lv_obj_t *head = lv_cont_create( hp_page, NULL );
            lv_obj_set_width( head, hp_width() );
            lv_cont_set_fit2( head, LV_FIT_NONE, LV_FIT_TIGHT );
            lv_cont_set_layout( head, LV_LAYOUT_ROW_MID );
            lv_obj_set_style_local_bg_opa( head, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP );
            lv_obj_set_style_local_border_width( head, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 0 );
            lv_obj_set_click( head, false );
            hp_head_img = lv_img_create( head, NULL );
            lv_img_set_src( hp_head_img, &hp_rabbit_content_64 );
            hp_head_text = hp_label( head, "", &hp_style22, hp_width() - 64 - 24 );
            lv_tileview_add_element( hp_tile, head );

            hp_gauge_lbl = hp_label( hp_page, "", &hp_style16, hp_width() );
            lv_label_set_align( hp_gauge_lbl, LV_LABEL_ALIGN_CENTER );
            hp_gauge = hp_box( hp_page, hp_width(), HP_GAUGE_H, 0x37474F, HP_GAUGE_H / 2 );
            hp_gauge_fill = hp_box( hp_gauge, 1, HP_GAUGE_H - 10, 0x2E7D32, 0 );
            lv_obj_t *middle = hp_box( hp_gauge, 4, HP_GAUGE_H, 0xFFFFFF, 2 );
            lv_obj_set_pos( middle, hp_width() / 2 - 2, 0 );
            hp_gauge_dot = hp_box( hp_gauge, HP_GAUGE_H, HP_GAUGE_H, 0x2E7D32, HP_GAUGE_H / 2 );
            lv_obj_set_style_local_border_width( hp_gauge_dot, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 3 );
            lv_obj_set_style_local_border_color( hp_gauge_dot, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE );
            lv_tileview_add_element( hp_tile, hp_gauge );

            hp_timer_lbl = hp_label( hp_page, "", &hp_style22, hp_width() );
            lv_label_set_align( hp_timer_lbl, LV_LABEL_ALIGN_CENTER );
            hp_go_btn = hp_button( "C'est parti", hp_go_event_cb );

            for ( size_t i = 0; i < routine.steps.size() && i < HP_MAX_STEPS; i++ )
                hp_step_btn[ i ] = hp_button( routine.steps[ i ].label.c_str(), hp_step_event_cb );

            if ( hp_config.members.size() > 1 )
                hp_member_btn = hp_button( ( "Changer : " + member->name ).c_str(), hp_member_event_cb );
        }
        else {
            hp_label( hp_page, "Home\n\nR\xC3\xA8gle l'adresse de la maison avec la roue dent\xC3\xA9""e, puis synchronise.", &hp_style16, hp_width() );
        }
        hp_status_lbl = hp_label( hp_page, hp_status, &hp_style16, hp_width() );
        hp_update();
    }
    lv_obj_set_y( lv_page_get_scrollable( hp_page ), 0 );
}

/* ---- events ------------------------------------------------------------- */

static void hp_save_setup( void ) {
    if ( !hp_url_area || !hp_key_area )
        return;
    std::string url = lv_textarea_get_text( hp_url_area );
    while ( !url.empty() && ( url.back() == '/' || url.back() == ' ' ) )
        url.pop_back();
    if ( url == "http:" )
        url.clear();
    if ( !url.empty() && url.find( "://" ) == std::string::npos )
        url = "http://" + url;
    snprintf( hp_settings.url, sizeof( hp_settings.url ), "%s", url.c_str() );

    std::string key;
    for ( const char *c = lv_textarea_get_text( hp_key_area ); *c; c++ )
        if ( *c != ' ' )
            key += ( *c >= 'a' && *c <= 'z' ) ? *c - 32 : *c;
    snprintf( hp_settings.key, sizeof( hp_settings.key ), "%s", key.c_str() );
    hp_settings.save();
}

static void hp_exit_event_cb( lv_obj_t *obj, lv_event_t event ) {
    if ( event != LV_EVENT_CLICKED )
        return;
    keyboard_hide();
    if ( hp_setup_screen ) {
        hp_save_setup();
        hp_setup_screen = false;
        hp_rebuild = true;
        hp_sync_at = lv_tick_get() + 500;
    }
    else {
        mainbar_jump_back();
    }
}

static void hp_refresh_event_cb( lv_obj_t *obj, lv_event_t event ) {
    if ( event != LV_EVENT_CLICKED )
        return;
    hp_manual_routine = -1;
    hp_rebuild = true;
    hp_sync_at = lv_tick_get() + 100;
}

static void hp_setup_event_cb( lv_obj_t *obj, lv_event_t event ) {
    if ( event != LV_EVENT_CLICKED )
        return;
    hp_setup_screen = true;
    hp_rebuild = true;
}

static void hp_lv_task( lv_task_t *task ) {
    if ( hp_job && hp_job_done )
        hp_finish_sync();

    struct tm now;
    if ( !hp_setup_screen && hp_now( &now ) && now.tm_min != hp_shown_minute ) {
        hp_shown_minute = now.tm_min;
        hp_new_day();
        if ( hp_config.valid() && hp_manual_routine < 0
             && hp_shown_routine != (int)hp::suggestRoutine( hp_config, now.tm_wday, now.tm_hour * 60 + now.tm_min ) )
            hp_rebuild = true;
        hp_update();
    }
    if ( hp_rebuild ) {
        hp_rebuild = false;
        hp_build();
    }
    time_t second = time( NULL );
    if ( hp_active && second != hp_shown_second ) {
        hp_shown_second = second;
        hp_tick();
    }
    if ( hp_sync_at && (int32_t)( lv_tick_get() - hp_sync_at ) >= 0 && !hp_job ) {
        hp_sync_at = 0;
        hp_start_sync();
    }
}

static void hp_activate_cb( void ) {
    hp_active = true;
    lv_task_set_prio( hp_task, LV_TASK_PRIO_MID );
    hp_manual_routine = -1;
    hp_shown_minute = -1;
    hp_new_day();
    hp_rebuild = true;
    hp_sync_at = lv_tick_get() + 500;
}

static void hp_hibernate_cb( void ) {
    hp_active = false;
    keyboard_hide();
    hp_setup_screen = false;
    /* a running exchange or pending ticks still finish in the background */
}

void hp_ui_setup( uint32_t tile_num ) {
    hp_settings.load();
    if ( hp_read_file( HP_CONFIG_CACHE, hp_config_json ) && !hp::parseConfig( hp_config_json.c_str(), hp_config ) )
        hp_config_json.clear();
    std::string day;
    if ( hp_read_file( HP_PROGRESS_CACHE, day ) )
        hp::progressFromJson( day.c_str(), hp_progress );

    mainbar_add_tile_activate_cb( tile_num, hp_activate_cb );
    mainbar_add_tile_hibernate_cb( tile_num, hp_hibernate_cb );
    hp_tile = mainbar_get_tile_obj( tile_num );

    lv_style_init( &hp_style16 );
    lv_style_set_text_font( &hp_style16, LV_STATE_DEFAULT, &hp_16px );
    lv_style_init( &hp_style22 );
    lv_style_set_text_font( &hp_style22, LV_STATE_DEFAULT, &hp_22px );

    hp_page = lv_page_create( hp_tile, NULL );
    lv_obj_set_size( hp_page, lv_disp_get_hor_res( NULL ), lv_disp_get_ver_res( NULL ) - THEME_ICON_SIZE - 2 * THEME_ICON_PADDING );
    lv_obj_align( hp_page, hp_tile, LV_ALIGN_IN_TOP_MID, 0, 0 );
    lv_obj_add_style( hp_page, LV_OBJ_PART_MAIN, APP_STYLE );
    lv_page_set_scrlbar_mode( hp_page, LV_SCRLBAR_MODE_DRAG );
    lv_page_set_scrl_layout( hp_page, LV_LAYOUT_COLUMN_MID );
    lv_obj_set_style_local_pad_left( hp_page, LV_PAGE_PART_SCROLLABLE, LV_STATE_DEFAULT, 0 );
    lv_obj_set_style_local_pad_right( hp_page, LV_PAGE_PART_SCROLLABLE, LV_STATE_DEFAULT, 0 );
    lv_obj_set_style_local_pad_inner( hp_page, LV_PAGE_PART_SCROLLABLE, LV_STATE_DEFAULT, 6 );
    lv_tileview_add_element( hp_tile, hp_page );

    hp_exit_btn = wf_add_exit_button( hp_tile, hp_exit_event_cb );
    lv_obj_align( hp_exit_btn, hp_tile, LV_ALIGN_IN_BOTTOM_LEFT, THEME_ICON_PADDING, -THEME_ICON_PADDING );
    hp_refresh_btn = wf_add_refresh_button( hp_tile, hp_refresh_event_cb );
    lv_obj_align( hp_refresh_btn, hp_tile, LV_ALIGN_IN_BOTTOM_MID, 0, -THEME_ICON_PADDING );
    hp_setup_btn = wf_add_setup_button( hp_tile, hp_setup_event_cb );
    lv_obj_align( hp_setup_btn, hp_tile, LV_ALIGN_IN_BOTTOM_RIGHT, -THEME_ICON_PADDING, -THEME_ICON_PADDING );
    lv_tileview_add_element( hp_tile, hp_exit_btn );
    lv_tileview_add_element( hp_tile, hp_refresh_btn );
    lv_tileview_add_element( hp_tile, hp_setup_btn );

    /* also ticks in the background, for the pending synchronization */
    hp_task = lv_task_create( hp_lv_task, 200, LV_TASK_PRIO_LOW, NULL );
    hp_build();
}
