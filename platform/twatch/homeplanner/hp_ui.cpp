// Home on the watch: a "now" screen (what to do, ahead or late, one button to
// validate), then the list of today's steps below; synchronization.
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
    #include <ESPmDNS.h>
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
#include "hardware/display.h"
#include "hardware/powermgm.h"

LV_FONT_DECLARE( hp_16px );
LV_FONT_DECLARE( hp_22px );
LV_FONT_DECLARE( hp_48px );
LV_IMG_DECLARE( hp_fox_bravo_96 );
LV_IMG_DECLARE( hp_rabbit_bravo_96 );
LV_IMG_DECLARE( hp_fox_content_64 );
LV_IMG_DECLARE( hp_fox_comfort_64 );
LV_IMG_DECLARE( hp_rabbit_content_64 );
LV_IMG_DECLARE( hp_rabbit_think_64 );

#define HP_CONFIG_CACHE     "/hp_family.json"
#define HP_PROGRESS_CACHE   "/hp_day.json"
#define HP_MAX_STEPS        24
#define HP_MARGIN           6
#define HP_GAUGE_H          14
#define HP_NOW_H            186             /* the "now" screen: all that shows above the bottom icons */
#define HP_PRESS_GUARD_MS   1500            /* a second tap must not validate the next step */
#define HP_WAKE_SYNC_MS     60000           /* at most one synchronization per minute of wake-ups */
#define HP_GREEN            0x2E7D32
#define HP_BLUE             0x1565C0
#define HP_GREY             0x37474F
#define HP_VALID_TIME       1767225600      /* 2026-01-01: older means the clock was never set */
#define HP_SYNC_DELAY_MS    3000            /* grouped ticks before sending */
#define HP_WIFI_WAIT_MS     12000
#define HP_MAX_ANSWER       ( 120 * 1024 )

static hp_settings_t hp_settings;
static hp::Config hp_config;
static hp::Progress hp_progress;
static std::string hp_config_json;

static lv_obj_t *hp_tile, *hp_page, *hp_exit_btn, *hp_setup_btn, *hp_clock_lbl;
static lv_obj_t *hp_head_img, *hp_step_lbl, *hp_bravo_fox, *hp_bravo_rabbit, *hp_bravo_lbl, *hp_status_lbl, *hp_left_lbl;
static lv_obj_t *hp_gauge, *hp_gauge_lbl, *hp_gauge_fill, *hp_gauge_dot, *hp_timer_lbl, *hp_done_btn;
static int hp_shown_next = -1, hp_shown_state = -1, hp_shown_target = -1;
static time_t hp_shown_second = 0;
static bool hp_active = false;
static bool hp_woke = false;                /* the screen came back on while Home was shown */
static bool hp_holding = false, hp_hold_saved = false;
static uint32_t hp_pressed_at = 0, hp_synced_at = 0;
static lv_obj_t *hp_url_area, *hp_key_area;
static lv_obj_t *hp_member_btn, *hp_routine_btn, *hp_sync_btn;
static lv_obj_t *hp_step_btn[ HP_MAX_STEPS ];
static lv_style_t hp_style16, hp_style22, hp_style48;
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
        response.clear();
        std::string base = url_;
        const std::string name = hp::mdnsName( base );
        if ( !name.empty() ) {
            /* the watch resolves .local names itself; begin() is harmless if mDNS already runs */
            MDNS.begin( "home-watch" );
            IPAddress ip = MDNS.queryHost( name.c_str(), 3000 );
            if ( (uint32_t)ip == 0 )
                return( -1 );
            base = hp::withHost( base, ip.toString().c_str() );
        }
        String url = String( base.c_str() ) + path;
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
    hp_synced_at = lv_tick_get();
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

static void hp_color_text( lv_obj_t *label, lv_color_t color ) {
    lv_obj_set_style_local_text_color( label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, color );
}

static void hp_color_button( lv_obj_t *btn, uint32_t color ) {
    lv_obj_set_style_local_bg_color( btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex( color ) );
    lv_obj_set_style_local_bg_color( btn, LV_BTN_PART_MAIN, LV_STATE_PRESSED, lv_color_hex( 0x607D8B ) );
}

static lv_obj_t *hp_button( lv_obj_t *parent, const char *text, lv_style_t *style, lv_event_cb_t cb ) {
    lv_obj_t *btn = lv_btn_create( parent, NULL );
    lv_obj_set_width( btn, hp_width() );
    lv_btn_set_fit2( btn, LV_FIT_NONE, LV_FIT_TIGHT );
    lv_obj_add_style( btn, LV_BTN_PART_MAIN, ws_get_button_style() );
    lv_obj_set_style_local_radius( btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 10 );
    lv_obj_set_style_local_border_width( btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 0 );
    lv_obj_set_style_local_pad_top( btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 9 );
    lv_obj_set_style_local_pad_bottom( btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 9 );
    hp_color_button( btn, HP_GREY );
    lv_obj_set_event_cb( btn, cb );
    lv_obj_t *label = hp_label( btn, text, style, hp_width() - 12 );
    lv_label_set_align( label, LV_LABEL_ALIGN_CENTER );
    hp_color_text( label, LV_COLOR_WHITE );
    lv_tileview_add_element( hp_tile, btn );
    return( btn );
}

static void hp_update( void );

static void hp_set_hidden( lv_obj_t *obj, bool hidden ) {
    if ( lv_obj_get_hidden( obj ) != hidden )
        lv_obj_set_hidden( obj, hidden );
}

/* an unchanged text is left alone: a scrolling label would start over */
static void hp_set_text( lv_obj_t *label, const char *text ) {
    if ( strcmp( lv_label_get_text( label ), text ) )
        lv_label_set_text( label, text );
}

/* "4:32", "1h05", "+0:20" when over, "+12'" when far over: always fits next to the mascot */
static void hp_countdown( char *text, size_t size, int seconds ) {
    if ( seconds <= -600 )
        snprintf( text, size, "+%d'", -seconds / 60 );
    else if ( seconds < 0 )
        snprintf( text, size, "+%d:%02d", -seconds / 60, -seconds % 60 );
    else if ( seconds >= 3600 )
        snprintf( text, size, "%dh%02d", seconds / 3600, seconds / 60 % 60 );
    else
        snprintf( text, size, "%d:%02d", seconds / 60, seconds % 60 );
}

/* the "now" screen, refreshed every second */
static void hp_update_now( const hp::Routine &routine, const hp::Evaluation &e, bool clock ) {
    if ( !hp_gauge )
        return;
    const bool bravo = clock && e.state == hp::STATE_DONE && !( e.free && e.next >= 0 );
    const bool step = clock && !bravo && e.state != hp::STATE_EMPTY && e.next >= 0;
    const bool gauge = clock && e.state != hp::STATE_EMPTY;

    /* ahead or late: the most important line */
    lv_color_t tone = lv_color_hex( e.slack >= 300 ? 0x43A047 : e.slack > -60 ? 0xE0A100 : 0xE8603C );
    std::string head = gauge ? hp::gaugeText( e.slack ) : std::string( clock ? "" : "Heure ?" );
    if ( gauge && e.state == hp::STATE_PASSED )
        head = "C'est l'heure !";
    else if ( gauge && e.losing && time( NULL ) / 3 % 2 )
        head = e.slack >= 0 ? "L'avance fond" : "Vite !";
    hp_set_text( hp_gauge_lbl, head.c_str() );
    hp_color_text( hp_gauge_lbl, gauge ? tone : lv_color_hex( 0xE0A100 ) );

    hp_set_hidden( hp_gauge, !gauge );
    if ( gauge ) {
        /* the middle is "on time": the bar grows to the right when ahead, to the left when late */
        lv_coord_t half = ( hp_width() - HP_GAUGE_H ) / 2;
        lv_coord_t offset = half * hp::gaugePercent( e.slack ) / 100;
        lv_coord_t middle = hp_width() / 2;
        lv_obj_set_pos( hp_gauge_fill, offset < 0 ? middle + offset : middle, 3 );
        lv_obj_set_size( hp_gauge_fill, offset < 0 ? -offset : offset ? offset : 1, HP_GAUGE_H - 6 );
        lv_obj_set_pos( hp_gauge_dot, middle + offset - HP_GAUGE_H / 2, 0 );
        lv_obj_set_style_local_bg_color( hp_gauge_fill, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, tone );
        lv_obj_set_style_local_bg_color( hp_gauge_dot, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, tone );
    }

    /* what to do */
    std::string what;
    if ( bravo )
        what = "";
    else if ( !clock )
        what = "R\xC3\xA8gle la montre";
    else if ( step )
        what = routine.steps[ e.next ].label + ( e.waiting ? " dans" : "" );
    else
        what = "Rien \xC3\xA0 faire";
    hp_set_text( hp_step_lbl, what.c_str() );

    const lv_img_dsc_t *mascot = &hp_rabbit_content_64;
    if ( e.state == hp::STATE_LATE || e.state == hp::STATE_PASSED )
        mascot = &hp_fox_comfort_64;
    else if ( e.state == hp::STATE_TIGHT )
        mascot = &hp_rabbit_think_64;
    else if ( e.done )
        mascot = &hp_fox_content_64;
    if ( lv_img_get_src( hp_head_img ) != mascot )
        lv_img_set_src( hp_head_img, mascot );
    hp_set_hidden( hp_head_img, bravo );
    hp_set_hidden( hp_bravo_fox, !bravo );
    hp_set_hidden( hp_bravo_rabbit, !bravo );

    hp_set_hidden( hp_timer_lbl, !step );
    hp_set_hidden( hp_done_btn, !step );
    if ( step ) {
        char text[ 24 ];
        hp_countdown( text, sizeof( text ), e.step_left );
        hp_set_text( hp_timer_lbl, text );
        const bool running = e.started || e.free || e.waiting;
        hp_color_text( hp_timer_lbl, e.step_left < 0 ? lv_color_hex( 0xE8603C )
                       : running ? lv_obj_get_style_text_color( hp_tile, LV_OBJ_PART_MAIN ) : lv_color_hex( 0x90A4AE ) );
        const bool go = !e.started && !e.free;
        hp_set_text( lv_obj_get_child( hp_done_btn, NULL ), go ? "C'est parti" : "Fait \xE2\x9C\x93" );
        hp_color_button( hp_done_btn, go ? HP_BLUE : HP_GREEN );
    }
    hp_set_hidden( hp_bravo_lbl, !bravo );

    /* time left before leaving (or before the meal...) */
    if ( clock && !bravo && e.state != hp::STATE_EMPTY && e.left > 0 ) {
        char text[ 96 ];
        const char *aim = e.target >= 0 ? routine.steps[ e.target ].label.c_str()
                          : routine.deadline_label.empty() ? "Fin" : routine.deadline_label.c_str();
        if ( e.left >= 60 )
            snprintf( text, sizeof( text ), "%s dans %d h %02d\n%d/%d fait", aim, e.left / 60, e.left % 60, e.done, e.total );
        else
            snprintf( text, sizeof( text ), "%s dans %d min\n%d/%d fait", aim, e.left, e.done, e.total );
        hp_set_text( hp_left_lbl, text );
    }
    else {
        char text[ 32 ];
        snprintf( text, sizeof( text ), "%d/%d fait", e.done, e.total );
        hp_set_text( hp_left_lbl, text );
    }
}

static void hp_update_clock( void ) {
    struct tm now;
    char text[ 8 ] = "--:--";
    if ( hp_now( &now ) )
        strftime( text, sizeof( text ), "%H:%M", &now );
    hp_set_text( hp_clock_lbl, text );
    lv_obj_align( hp_clock_lbl, hp_tile, LV_ALIGN_IN_BOTTOM_MID, 0, -THEME_ICON_PADDING - 8 );
}

/* every second: only the moving parts, the whole screen when the situation changes */
static void hp_tick( void ) {
    const hp::Member *member = hp_member();
    struct tm now;
    hp_update_clock();
    if ( hp_setup_screen || !member || hp_shown_routine < 0 || !hp_gauge || !hp_now( &now ) )
        return;
    const hp::Routine &routine = hp_config.routines[ hp_shown_routine ];
    hp::Evaluation e = hp::evaluate( routine, hp_progress.done[ member->id + "/" + routine.id ],
                                     now.tm_hour * 60 + now.tm_min, hp_now_ms() );
    if ( e.next != hp_shown_next || e.state != hp_shown_state || e.target != hp_shown_target )
        hp_update();
    else
        hp_update_now( routine, e, true );
}

/* plain coloured rectangle, not clickable so the page still scrolls */
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
    if ( hp_setup_screen || !member || hp_shown_routine < 0 || !hp_gauge )
        return;

    const hp::Routine &routine = hp_config.routines[ hp_shown_routine ];
    const hp::Marks &marks = hp_progress.done[ member->id + "/" + routine.id ];
    bool clock = hp_now( &now );
    hp::Evaluation e = hp::evaluate( routine, marks, clock ? now.tm_hour * 60 + now.tm_min : 0, clock ? hp_now_ms() : 0 );
    hp_shown_next = e.next;
    hp_shown_state = e.state;
    hp_shown_target = e.target;
    hp_update_now( routine, e, clock );

    for ( size_t i = 0; i < routine.steps.size() && i < HP_MAX_STEPS; i++ ) {
        if ( !hp_step_btn[ i ] )
            continue;
        const bool done = hp::isDone( marks, routine.steps[ i ].id );
        const bool current = !done && (int)i == e.next;
        std::string label = std::string( done ? "\xE2\x9C\x93 " : current ? "\xC2\xBB " : "" ) + routine.steps[ i ].label;
        lv_label_set_text( lv_obj_get_child( hp_step_btn[ i ], NULL ), label.c_str() );
        hp_color_button( hp_step_btn[ i ], done ? HP_GREEN : current ? HP_BLUE : HP_GREY );
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

/* the big button: starts the countdown, then validates the current step */
static void hp_done_event_cb( lv_obj_t *obj, lv_event_t event ) {
    const hp::Member *member = hp_member();
    struct tm now;
    if ( event != LV_EVENT_CLICKED || !member || hp_shown_routine < 0 || !hp_now( &now ) )
        return;
    if ( hp_pressed_at && lv_tick_elaps( hp_pressed_at ) < HP_PRESS_GUARD_MS )
        return;
    hp_new_day();
    if ( hp_rebuild || hp_shown_next < 0 )
        return;
    const hp::Routine &routine = hp_config.routines[ hp_shown_routine ];
    if ( hp_shown_next >= (int)routine.steps.size() )
        return;
    hp::Marks &marks = hp_progress.done[ member->id + "/" + routine.id ];
    const std::string &step = routine.steps[ hp_shown_next ].id;
    const hp::Evaluation e = hp::evaluate( routine, marks, now.tm_hour * 60 + now.tm_min, hp_now_ms() );
    if ( !e.started && !e.free )
        hp_focus( marks, step );
    else
        marks[ step ] = hp::nextMark( marks[ step ], true, hp_now_ms() );
    hp_pressed_at = lv_tick_get();
    hp_marks_changed();
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

static void hp_refresh_event_cb( lv_obj_t *obj, lv_event_t event ) {
    if ( event != LV_EVENT_CLICKED )
        return;
    hp_manual_routine = -1;
    hp_rebuild = true;
    hp_sync_at = lv_tick_get() + 100;
}

/* a label of the "now" screen, centred at a fixed place */
static lv_obj_t *hp_line( lv_obj_t *parent, lv_style_t *style, lv_coord_t x, lv_coord_t y, lv_coord_t width ) {
    lv_obj_t *label = lv_label_create( parent, NULL );
    lv_obj_add_style( label, LV_LABEL_PART_MAIN, style );
    lv_label_set_long_mode( label, LV_LABEL_LONG_SROLL_CIRC );
    lv_label_set_align( label, LV_LABEL_ALIGN_CENTER );
    lv_obj_set_width( label, width );
    lv_label_set_text( label, "" );
    lv_obj_set_pos( label, x, y );
    return( label );
}

static lv_obj_t *hp_image( lv_obj_t *parent, const lv_img_dsc_t *src, lv_coord_t x, lv_coord_t y ) {
    lv_obj_t *img = lv_img_create( parent, NULL );
    lv_img_set_src( img, src );
    lv_obj_set_pos( img, x, y );
    return( img );
}

static void hp_build_now( void ) {
    const lv_coord_t full = lv_disp_get_hor_res( NULL );
    lv_obj_t *screen = hp_box( hp_page, full, HP_NOW_H, 0, 0 );
    lv_obj_set_style_local_bg_opa( screen, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP );
    lv_tileview_add_element( hp_tile, screen );
    lv_page_glue_obj( screen, true );

    hp_gauge_lbl = hp_line( screen, &hp_style22, HP_MARGIN, 1, hp_width() );
    hp_gauge = hp_box( screen, hp_width(), HP_GAUGE_H, HP_GREY, HP_GAUGE_H / 2 );
    lv_obj_set_pos( hp_gauge, HP_MARGIN, 30 );
    hp_gauge_fill = hp_box( hp_gauge, 1, HP_GAUGE_H - 6, HP_GREEN, 0 );
    lv_obj_t *middle = hp_box( hp_gauge, 4, HP_GAUGE_H, 0xB0BEC5, 2 );
    lv_obj_set_pos( middle, hp_width() / 2 - 2, 0 );
    hp_gauge_dot = hp_box( hp_gauge, HP_GAUGE_H, HP_GAUGE_H, HP_GREEN, HP_GAUGE_H / 2 );
    lv_obj_set_style_local_border_width( hp_gauge_dot, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 2 );
    lv_obj_set_style_local_border_color( hp_gauge_dot, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE );

    hp_step_lbl = hp_line( screen, &hp_style22, HP_MARGIN, 49, hp_width() );
    hp_head_img = hp_image( screen, &hp_rabbit_content_64, HP_MARGIN, 71 );
    hp_timer_lbl = hp_line( screen, &hp_style48, HP_MARGIN + 66, 77, full - 2 * HP_MARGIN - 66 );
    hp_done_btn = hp_button( screen, "", &hp_style22, hp_done_event_cb );
    lv_obj_set_style_local_pad_top( hp_done_btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 10 );
    lv_obj_set_style_local_pad_bottom( hp_done_btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 10 );
    lv_obj_set_pos( hp_done_btn, HP_MARGIN, 136 );

    /* everything done: the two mascots */
    hp_bravo_fox = hp_image( screen, &hp_fox_bravo_96, full / 2 - 102, 52 );
    hp_bravo_rabbit = hp_image( screen, &hp_rabbit_bravo_96, full / 2 + 6, 52 );
    hp_bravo_lbl = hp_line( screen, &hp_style22, HP_MARGIN, 154, hp_width() );
    lv_label_set_text( hp_bravo_lbl, "Bravo, c'est pr\xC3\xAAt !" );
}

static void hp_build( void ) {
    keyboard_hide();
    lv_page_clean( hp_page );
    hp_head_img = hp_step_lbl = hp_status_lbl = hp_url_area = hp_key_area = NULL;
    hp_member_btn = hp_routine_btn = hp_sync_btn = hp_left_lbl = NULL;
    hp_gauge = hp_gauge_lbl = hp_gauge_fill = hp_gauge_dot = hp_timer_lbl = hp_done_btn = NULL;
    hp_bravo_fox = hp_bravo_rabbit = hp_bravo_lbl = NULL;
    memset( hp_step_btn, 0, sizeof( hp_step_btn ) );
    hp_shown_routine = -1;
    lv_obj_set_hidden( hp_setup_btn, hp_setup_screen );
    lv_obj_set_hidden( hp_clock_lbl, hp_setup_screen );

    /* theme colour (white on the dark theme) for every text, the default one is hard to read */
    const lv_color_t text = lv_obj_get_style_text_color( hp_tile, LV_OBJ_PART_MAIN );
    lv_style_set_text_color( &hp_style16, LV_STATE_DEFAULT, text );
    lv_style_set_text_color( &hp_style22, LV_STATE_DEFAULT, text );
    lv_style_set_text_color( &hp_style48, LV_STATE_DEFAULT, text );

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

            hp_build_now();
            hp_left_lbl = hp_label( hp_page, "", &hp_style16, hp_width() );
            lv_label_set_align( hp_left_lbl, LV_LABEL_ALIGN_CENTER );

            for ( size_t i = 0; i < routine.steps.size() && i < HP_MAX_STEPS; i++ )
                hp_step_btn[ i ] = hp_button( hp_page, routine.steps[ i ].label.c_str(), &hp_style22, hp_step_event_cb );

            hp_routine_btn = hp_button( hp_page, ( "Routine : " + routine.name ).c_str(), &hp_style16, hp_routine_event_cb );
            if ( hp_config.members.size() > 1 )
                hp_member_btn = hp_button( hp_page, ( "Changer : " + member->name ).c_str(), &hp_style16, hp_member_event_cb );
        }
        else {
            hp_label( hp_page, "Home\n\nR\xC3\xA8gle l'adresse de la maison avec la roue dent\xC3\xA9""e, puis synchronise.", &hp_style16, hp_width() );
        }
        hp_sync_btn = hp_button( hp_page, "Synchroniser", &hp_style16, hp_refresh_event_cb );
        hp_status_lbl = hp_label( hp_page, hp_status, &hp_style16, hp_width() );
        hp_update();
    }
    hp_update_clock();
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
    if ( hp_woke ) {
        /* back from a dark screen: show the present moment, and fetch the ticks made elsewhere */
        hp_woke = false;
        hp_manual_routine = -1;
        hp_shown_minute = -1;
        hp_rebuild = !hp_setup_screen;
        if ( !hp_synced_at || lv_tick_elaps( hp_synced_at ) >= HP_WAKE_SYNC_MS )
            hp_sync_at = lv_tick_get() + 500;
    }
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

/*
 * While Home is open the watch stays on it when the screen goes off, like the map
 * app does; the setting of the user comes back when Home is left.
 */
static void hp_hold_screen( bool hold ) {
    if ( hold == hp_holding )
        return;
    if ( hold ) {
        hp_hold_saved = display_get_block_return_maintile();
        display_set_block_return_maintile( true );
    }
    else {
        display_set_block_return_maintile( hp_hold_saved );
    }
    hp_holding = hold;
}

static bool hp_powermgm_event_cb( EventBits_t event, void *arg ) {
    if ( event == POWERMGM_WAKEUP && hp_active )
        hp_woke = true;
    return( true );
}

static void hp_activate_cb( void ) {
    hp_active = true;
    hp_hold_screen( true );
    lv_task_set_prio( hp_task, LV_TASK_PRIO_MID );
    hp_manual_routine = -1;
    hp_shown_minute = -1;
    hp_new_day();
    hp_rebuild = true;
    hp_sync_at = lv_tick_get() + 500;
}

static void hp_hibernate_cb( void ) {
    hp_active = false;
    hp_hold_screen( false );
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
    lv_style_init( &hp_style48 );
    lv_style_set_text_font( &hp_style48, LV_STATE_DEFAULT, &hp_48px );

    hp_page = lv_page_create( hp_tile, NULL );
    lv_obj_set_size( hp_page, lv_disp_get_hor_res( NULL ), lv_disp_get_ver_res( NULL ) - THEME_ICON_SIZE - 2 * THEME_ICON_PADDING );
    lv_obj_align( hp_page, hp_tile, LV_ALIGN_IN_TOP_MID, 0, 0 );
    lv_obj_add_style( hp_page, LV_OBJ_PART_MAIN, APP_STYLE );
    lv_page_set_scrlbar_mode( hp_page, LV_SCRLBAR_MODE_DRAG );
    lv_page_set_scrl_layout( hp_page, LV_LAYOUT_COLUMN_MID );
    lv_obj_set_style_local_pad_top( hp_page, LV_PAGE_PART_BG, LV_STATE_DEFAULT, 0 );
    lv_obj_set_style_local_pad_bottom( hp_page, LV_PAGE_PART_BG, LV_STATE_DEFAULT, 0 );
    lv_obj_set_style_local_pad_left( hp_page, LV_PAGE_PART_BG, LV_STATE_DEFAULT, 0 );
    lv_obj_set_style_local_pad_right( hp_page, LV_PAGE_PART_BG, LV_STATE_DEFAULT, 0 );
    lv_obj_set_style_local_border_width( hp_page, LV_PAGE_PART_BG, LV_STATE_DEFAULT, 0 );
    lv_obj_set_style_local_pad_top( hp_page, LV_PAGE_PART_SCROLLABLE, LV_STATE_DEFAULT, 0 );
    lv_obj_set_style_local_pad_left( hp_page, LV_PAGE_PART_SCROLLABLE, LV_STATE_DEFAULT, 0 );
    lv_obj_set_style_local_pad_right( hp_page, LV_PAGE_PART_SCROLLABLE, LV_STATE_DEFAULT, 0 );
    lv_obj_set_style_local_pad_inner( hp_page, LV_PAGE_PART_SCROLLABLE, LV_STATE_DEFAULT, 6 );
    lv_tileview_add_element( hp_tile, hp_page );

    hp_exit_btn = wf_add_exit_button( hp_tile, hp_exit_event_cb );
    lv_obj_align( hp_exit_btn, hp_tile, LV_ALIGN_IN_BOTTOM_LEFT, THEME_ICON_PADDING, -THEME_ICON_PADDING );
    hp_clock_lbl = lv_label_create( hp_tile, NULL );
    lv_obj_add_style( hp_clock_lbl, LV_LABEL_PART_MAIN, &hp_style22 );
    lv_label_set_text( hp_clock_lbl, "--:--" );
    hp_setup_btn = wf_add_setup_button( hp_tile, hp_setup_event_cb );
    lv_obj_align( hp_setup_btn, hp_tile, LV_ALIGN_IN_BOTTOM_RIGHT, -THEME_ICON_PADDING, -THEME_ICON_PADDING );
    lv_tileview_add_element( hp_tile, hp_exit_btn );
    lv_tileview_add_element( hp_tile, hp_setup_btn );

    powermgm_register_cb( POWERMGM_WAKEUP, hp_powermgm_event_cb, "home powermgm" );

    /* also ticks in the background, for the pending synchronization */
    hp_task = lv_task_create( hp_lv_task, 200, LV_TASK_PRIO_LOW, NULL );
    hp_build();
}
