/* Licensed under GNU General Public License v3.0
  See https://github.com/jeffasuk/thermostat
  jeff at jamcupboard.co.uk
*/
#include "globals.h"

char magic_tag[4] = "v11";    // To indicate that we've written to EEPROM, so it's OK to use the values.
            // MUST change this if the format/structure of persistent data has changed, which
            // will force unit into setup mode, with its own WiFi access point


float current_temperature = IMPOSSIBLE_TEMPERATURE;
float switch_temperature = IMPOSSIBLE_TEMPERATURE;
SENSOR_DATA sensor_data = {0};

// 0 means "off", which matches the start-up hardware state
int8_t relay_state = 0;

uint8_t in_setup_mode = 0;

char *new_etag = 0;

char *p_etag = 0;   // not currently used
char *p_ssid = 0;
char *p_passrot = 0;
char *p_report_hostname = 0;
char *p_report_path = 0;
char *p_cfg_path = 0;   // not currently used
char *p_identifier = 0;

struct PERSISTENT_DATA persistent_data = {
     0,     // port for server  Gets set in initial web set-up.
    13,     // onewire_pin      Gets set in initial web set-up.
     3,     // rotate for passwords     Gets set in initial web set-up.
    20,     // max_time_between_reports, seconds
    20.0,   // desired temperature
    0.2,    // precision, used for enhanced stability when looking at temperature changes, esp. for change of direction
    2.0, 2.0,   // max discrepancy values for scope of heuristic
    HEATING, // mode:  heating or cooling
};

// names of values that can be set from server and get saved to EEPROM
PERSISTENT_INFO persistents[] = {
    {PERS_UINT16, "port",                       &persistent_data.port},
    {PERS_UINT32, "max_time_between_reports",   &persistent_data.max_time_between_reports},
    {PERS_UINT8,  "onewire_pin",                &persistent_data.onewire_pin},
    {PERS_INT8,   "rot",                        &persistent_data.rot},
    {PERS_FLOAT,  "desired_temperature",        &persistent_data.desired_temperature},
    {PERS_FLOAT,  "precision",                  &persistent_data.precision},
    {PERS_FLOAT,  "max_discrepancy_down",       &persistent_data.max_discrepancy_down},
    {PERS_FLOAT,  "max_discrepancy_up",         &persistent_data.max_discrepancy_up},
    {PERS_UINT8,  "mode",                       &persistent_data.mode},
    {0}
};

// names of string-type values that can be set from server and get saved to EEPROM
PERSISTENT_STRING_INFO persistent_strings[] = {
    {"etag",        &p_etag},   // this one is unusual in being set from an HTTP header, not from response content
    {"ssid",        &p_ssid},
    {"rotpass",     &p_passrot},
    {"rpthost",     &p_report_hostname},
    {"rptpath",     &p_report_path},
    {"cfgpath",     &p_cfg_path},
    {"ident",       &p_identifier},
    {0}
};

// fields in the settings page, for storing in EEPROM
// This should probably be in the PERSISTENT_INFO and PERSISTENT_STRING_INFO tables
NAME_MAPPING name_mapping[] = {
    {"ssid",    "ssid",             "WiFi SSID",},
    {"pswd",    "rotpass",          "WiFi password",},
    {"lpswd",   "rotlpswd",         "Password for set-up access point",},
    {"ident",   "ident",            "Unit identifier",},
    {"host",    "rpthost",          "Server hostname",},
    {"port",    "port",             "Server port",},
    {"cpath",   "cfgpath",          "Settings path",},
    {"rpath",   "rptpath",          "Report path",},
    {0}
};
