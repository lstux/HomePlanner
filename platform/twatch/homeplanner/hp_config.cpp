#include "config.h"
#include <stdio.h>

#include "hp_config.h"

hp_settings_t::hp_settings_t() : BaseJsonConfig( HP_JSON_CONFIG_FILE ) {}

bool hp_settings_t::onSave( JsonDocument& doc ) {
    doc["url"] = url;
    doc["key"] = key;
    doc["member"] = member;
    return true;
}

bool hp_settings_t::onLoad( JsonDocument& doc ) {
    snprintf( url, sizeof( url ), "%s", (const char*)( doc["url"] | "" ) );
    snprintf( key, sizeof( key ), "%s", (const char*)( doc["key"] | "" ) );
    snprintf( member, sizeof( member ), "%s", (const char*)( doc["member"] | "" ) );
    return true;
}

bool hp_settings_t::onDefault( void ) {
    url[ 0 ] = key[ 0 ] = member[ 0 ] = '\0';
    return true;
}
