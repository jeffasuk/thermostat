/* Licensed under GNU General Public License v3.0
  See https://github.com/jeffasuk/thermostat
  jeff at jamcupboard.co.uk
*/

#ifndef _GLOBALS_H
#define _GLOBALS_H

#include <Arduino.h>    // for the various int typedefs

// should be in Arduino.h but doesn't seem to be, or not to work anyway. It's OK for ints but we need floats too
#define abs(x) ((x)>0?(x):-(x))

extern char magic_tag[4];

// to switch off serial reporting
//#define QUIET

#ifdef QUIET
#define DOPRINT(x)
#define DOPRINTLN(x)
#else
#define DOPRINT(x)  Serial.print( (x) )
#define DOPRINTLN(x)  Serial.println( (x) )
#endif

#define LED_PIN 2
#define RELAY_PIN_POWER 4  // Relay control. Use this one for single-relay.
#define RELAY_PIN_MAIN  5  // Relay control. Use this one for main power relay which switches everything off.
                           //       Irrelevant in single-relay.
#define SETUP_PIN 14

// modes
#define HEATING     0
#define COOLING     1

// sensors
#define MAX_TEMPERATURE_SENSORS 8

// inter-module i/f
typedef struct {
    int             ok;
    unsigned char   addr[8];
    float           temperature_f, temperature_c;
} TEMPERATURE_DATA;

typedef struct {
    int     nb_temperature_sensors;
    TEMPERATURE_DATA temperature[MAX_TEMPERATURE_SENSORS];
} SENSOR_DATA;

#define IMPOSSIBLE_TEMPERATURE  (-999999)

extern float current_temperature;
extern float switch_temperature;
extern float switch_offset_above;
extern float switch_offset_below;
extern SENSOR_DATA sensor_data;
extern int8_t power_state;
extern int8_t main_state;
extern uint8_t in_setup_mode;
extern char *new_etag;

// settable
extern char *p_etag;
extern char *p_ssid;
extern char *p_passrot;
extern char *p_report_hostname;
extern char *p_report_path;
extern char *p_cfg_path;   // not currently used
extern char *p_identifier;

struct PERSISTENT_DATA {    // this structure can be stored in EEPROM
    uint16_t port;
    uint8_t onewire_pin;
    int8_t  rot;
    uint32_t max_time_between_reports;
    uint32_t fan_overrun_sec;
    float   desired_temperature;
    float   precision;
    uint8_t mode;
};
extern struct PERSISTENT_DATA persistent_data;

// This defines the datatypes, names and where to store them in runtime memory
typedef enum { PERS_INT8, PERS_INT16, PERS_INT32, PERS_UINT8, PERS_UINT16, PERS_UINT32, PERS_FLOAT, PERS_STR } PERSISTENT_DATA_TYPE;
struct PERSISTENT_INFO_STR {
    PERSISTENT_DATA_TYPE    type;
    char    *name;
    void    *value;
};
typedef struct PERSISTENT_INFO_STR PERSISTENT_INFO;
extern PERSISTENT_INFO persistents[];

// This defines the names and runtime storage locations for string-type data to be stored in EEPROM
struct PERSISTENT_STRING_INFO_STR {
    char    *name;
    char    **value;    // where to put a pointer to malloced area for the data itself
};
typedef struct PERSISTENT_STRING_INFO_STR PERSISTENT_STRING_INFO;
extern PERSISTENT_STRING_INFO persistent_strings[];

struct NAME_MAPPING_STR {
    char    *html_field_name;
    char    *persistent_item_name;
    char    *display_name;  // unused as yet. Should be used in generating the HTML form.
};
typedef struct NAME_MAPPING_STR NAME_MAPPING;
extern NAME_MAPPING name_mapping[];

#endif  // _GLOBALS_H
