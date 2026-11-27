// Home on the watch: settings kept in /homeplanner.json.
#pragma once

#include "utils/basejsonconfig.h"

#define HP_JSON_CONFIG_FILE "/homeplanner.json"
#define HP_URL_LEN 96
#define HP_KEY_LEN 20
#define HP_MEMBER_LEN 24

class hp_settings_t : public BaseJsonConfig {
    public:
    hp_settings_t();
    char url[ HP_URL_LEN ] = "";        /* address of the central, e.g. http://192.168.1.50 */
    char key[ HP_KEY_LEN ] = "";        /* code shown by the central, replaced after the first sync by this watch's own token */
    char member[ HP_MEMBER_LEN ] = "";  /* member wearing the watch, chosen on the watch */

    protected:
    virtual bool onLoad( JsonDocument& document );
    virtual bool onSave( JsonDocument& document );
    virtual bool onDefault( void );
    virtual size_t getJsonBufferSize() { return 600; }
};
