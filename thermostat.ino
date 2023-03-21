/* Licensed under GNU General Public License v3.0
  See https://github.com/jeffasuk/thermostat
  jeff at jamcupboard.co.uk
*/

#include <ESP8266WiFi.h>
#include "globals.h"
#include "led.h"
#include "network.h"
#include "eepromutils.h"
#include "sensors.h"
#include "webserver.h"

// for detecting change in desired temperature and other settings
static float previous_desired_temperature = IMPOSSIBLE_TEMPERATURE;
static uint8_t previous_mode = 99;  // matches neither of the valid mode values
static int8_t temperature_changing = 0;    // flag

enum RELAY_STATE {POWER_OFF, POWER_ON};
const char *powerStateName[] = {"OFF", "ON"}; // for debug and reporting

static uint32_t    millis_now;
static uint32_t time_when_switched_on;
static float temp_when_switched_on;
static uint32_t time_when_switched_off;
static float temp_when_switched_off;
static uint32_t     millis_at_last_report = 0;
static float        previous_temperature = IMPOSSIBLE_TEMPERATURE;     // for detecting direction change
static float        previous_max_discrepancy_down = persistent_data.max_discrepancy_down;
static float        previous_max_discrepancy_up = persistent_data.max_discrepancy_up;

// rotating history of discrepancies from switch temperature
#define HISTORY_LENGTH  10  // must be even number, to catch equal number of peaks and troughs
static float past_peaks_and_troughs[HISTORY_LENGTH] = {0};
static uint8_t history_index = HISTORY_LENGTH-1;
static float min_temperature = IMPOSSIBLE_TEMPERATURE;
static float max_temperature = IMPOSSIBLE_TEMPERATURE;

/* NB The terms "up" and "down" refer to the direction in which the power operates, so "up" means
   increasing temperature when heating, but decreasing temperature when cooling.
   Ditto for "max" and "min".
 */

float normalizeTemperature(float temperature)
{
    // invert temperature for inverted operation (i.e. cooling instead of heating)
    if (persistent_data.mode == HEATING)
    {
        // no change
        return temperature;
    }
    // cooling; invert temperature
    return (temperature == IMPOSSIBLE_TEMPERATURE) ? IMPOSSIBLE_TEMPERATURE : -temperature;
}
float getDesiredTemperature()
{
    return normalizeTemperature(persistent_data.desired_temperature);
}

static void setIfImpossible(float *var, float default_value)
{
    // If the supplied variable is IMPOSSIBLE_TEMPERATURE, set it to the provided value
    if (*var == IMPOSSIBLE_TEMPERATURE)
    {
        *var = default_value;
    }
}

#ifdef NO_DONT_DO_THIS_RIGHT_NOW
static int assessRelayStateSimple(int pre_relay_state)
{
    // This just switches on when too cool and off when too warm. Not currently in use.
    // Maybe we should be able switch to this at runtime for testing purposes.
    if (normalizeTemperature(current_temperature) < getDesiredTemperature())
    {
        return POWER_ON;
    }
    return POWER_OFF;
}
#endif

static int8_t assessRelayState(int8_t pre_relay_state)
{
    uint8_t switched = 0;
    uint8_t revert_to_startup_algorithm = 0;
    int8_t new_relay_state = pre_relay_state;

    setIfImpossible(&switch_temperature, persistent_data.desired_temperature);
    setIfImpossible(&max_temperature, current_temperature);
    setIfImpossible(&min_temperature, current_temperature);
    if (normalizeTemperature(current_temperature) >= normalizeTemperature(switch_temperature))
    {
        if (pre_relay_state == POWER_ON)
        {
            // too warm for power on, so switch off
            new_relay_state = POWER_OFF;
            switched = 1;
        }
    }
    else
    {
        if (pre_relay_state == POWER_OFF)
        {
            // too cool for power off, so switch on
            new_relay_state = POWER_ON;
            switched = 1;
        }
    }
    if (switched)
    {
        float diff;
        if ( (new_relay_state == POWER_OFF && persistent_data.mode == HEATING)
          || (new_relay_state == POWER_ON  && persistent_data.mode == COOLING)
            )
        {
            // just switched heating off or cooling on, so we're heading for a temperature peak
            // record the temperature discrepancy at the preceding temperature trough...
            diff = min_temperature - switch_temperature;
            revert_to_startup_algorithm = abs(diff) > persistent_data.max_discrepancy_down;
            // ... and start recording max temperature in order to capture level of next temperature peak
            max_temperature = current_temperature;
        }
        else  // must have just switched heating on or cooling off, so we're heading for a temperature trough
        {
            // record the temperature discrepancy at the preceding temperature peak...
            diff = max_temperature - switch_temperature;
            revert_to_startup_algorithm = abs(diff) > persistent_data.max_discrepancy_up;
            // ... and start recording min temperature in order to capture level of next temperature trough
            min_temperature = current_temperature;
        }
        history_index = (history_index + 1) % HISTORY_LENGTH;
        past_peaks_and_troughs[history_index] = diff;
    }

    max_temperature = max(max_temperature, current_temperature);
    min_temperature = min(min_temperature, current_temperature);

    if (revert_to_startup_algorithm)
    {
        // latest discrepancy is outside the range we can comfortably handle without ringing
        // so reset to start-up state
        DOPRINTLN("Reverting to start-up switching");
        history_index = HISTORY_LENGTH-1;
        for (int i = 0; i < HISTORY_LENGTH; ++i)
        {
            past_peaks_and_troughs[i] = 0.0;
        }
        switch_temperature = persistent_data.desired_temperature;
    }

    if (switched)
    {
        // we've just switched, so assess performance
        float average_discrepancy = 0;
        float min_val = past_peaks_and_troughs[0];
        float max_val = past_peaks_and_troughs[0];
        for (int i = 0; i < HISTORY_LENGTH; ++i)
        {
            average_discrepancy += past_peaks_and_troughs[i];
            min_val = min(min_val, past_peaks_and_troughs[i]);
            max_val = max(max_val, past_peaks_and_troughs[i]);
        }
        // ignore the most extreme min and max values, to filter out extreme events.
        average_discrepancy -= min_val + max_val;
        average_discrepancy /= HISTORY_LENGTH - 2;
        DOPRINT("average discrepancy over ");
        DOPRINT(HISTORY_LENGTH);
        DOPRINT(" peaks/troughs: ");
        DOPRINT(average_discrepancy);
        DOPRINT(" Ignoring ");
        DOPRINT(min_val);
        DOPRINT(" and ");
        DOPRINTLN(max_val);
        // adjust switch point
        switch_temperature = persistent_data.desired_temperature - average_discrepancy;
        DOPRINT  ("New switch temperature: ");
        DOPRINTLN(switch_temperature);
    }
    return new_relay_state;
}


/* this is called on power-up */
void setup()
{                
    int do_setup_mode = 0;
#ifndef QUIET
    Serial.begin(115200);
#endif
    delay(100);
    DOPRINTLN("");
    DOPRINTLN(sizeof persistent_data);
    DOPRINTLN((uint32_t)&(persistent_data.port));
    DOPRINTLN((uint32_t)&(persistent_data.max_time_between_reports));
    DOPRINTLN((uint32_t)&(persistent_data.onewire_pin));
    DOPRINTLN((uint32_t)&(persistent_data.rot));
    DOPRINTLN((uint32_t)&(persistent_data.desired_temperature));

    pinMode(SETUP_PIN, INPUT_PULLUP);     
    pinMode(LED_PIN, OUTPUT);     
    pinMode(RELAY_PIN, OUTPUT);     
    DOPRINTLN("Sleep 1");
    delay(1000);
    if (eepromIsUninitialized())
    {
        do_setup_mode = 1;
        DOPRINT("No valid data in EEPROM.");
    }
    else
    {
        DOPRINTLN("Reading settings from EEPROM");
        readFromEeprom();
#ifndef QUIET
        showSettings();
#endif
    }
    if (!do_setup_mode && digitalRead(SETUP_PIN) == LOW)
    {
        do_setup_mode = 1;
        DOPRINT("Set-up signal found.");
    }
    // sleep a little in software while hardware wakes up
    DOPRINTLN("Sleep 2");
    delay(2000);
    if (do_setup_mode)
    {
        DOPRINTLN(" Entering set-up mode.");
        startAccessPoint();
        IPAddress myIP = WiFi.softAPIP();
        DOPRINT("AP IP address: ");
        DOPRINTLN(myIP);
        setLEDflashing(800, 200);
        in_setup_mode = 1;
    }
    else
    {
        WiFi.mode(WIFI_STA);    // Don't need AP now
        connectWiFi();
        DOPRINT("IP address: ");
        DOPRINTLN(WiFi.localIP());
    }
    startAsyncWebServer();  // need a webserver in all modes
}

// the loop routine runs over and over again forever:
void loop()
{
    setLED();   // allow operation of whatever flash/pulse mode has been set

    if (in_setup_mode)
    {
        delay(10);    // Nothing to do in loop() until setup has been done in the async webserver
        return;
    }

    // No longer in set-up mode.
    if (WiFi.getMode() == WIFI_AP_STA)
    {
        // switching to normal mode
        DOPRINTLN(" Leaving set-up mode.");
        WiFi.mode(WIFI_STA);    // Don't need AP now
        connectWiFi();
    }

    setLEDflashing(100, 400);
    //DOPRINTLN("read sensors");
    readSensors(&sensor_data);
    //DOPRINTLN("read sensors done");
    if (sensor_data.temperature[0].ok != ONEWIRE_OK)
    {
        DOPRINTLN("Failed to read controlling temperature sensor. Turning off for safety.");
        relay_state = POWER_OFF;
        setLEDflashing(500, 500);
        digitalWrite(RELAY_PIN, 0);
    }
    else
    {
        float temperature_to_report;
        int send_report = 0;
        int do_check = 0;
        const char *report_comment = NULL;
        temperature_to_report = current_temperature = sensor_data.temperature[0].temperature_c;
#ifndef QUIET
        DOPRINT  (powerStateName[relay_state]);
        DOPRINT  (" at ");
        DOPRINT  (current_temperature);
        DOPRINT  ("deg ");
        switch(temperature_changing)
        {
            case 0:
                {
                    DOPRINT  ("change less than ");
                    DOPRINT  (persistent_data.precision);
                }
                break;
            case 1:
            case -1:
                {
                    DOPRINT  ("getting ");
                    DOPRINT  ((temperature_changing < 0) ? "cooler by " : "warmer by ");
                    DOPRINT  (abs(previous_temperature - current_temperature));
                }
                break;
        }
        DOPRINT  (", target ");
        DOPRINT  (persistent_data.desired_temperature);
        DOPRINT  ("   switching at ");
        DOPRINT  (switch_temperature);
        DOPRINT  ("(-"); DOPRINT(persistent_data.max_discrepancy_down);
        DOPRINT  (","); DOPRINT(persistent_data.max_discrepancy_up);
        DOPRINT  (") for ");
        DOPRINTLN(persistent_data.mode == HEATING ? "heating" : "cooling");
#endif
        if (previous_temperature == IMPOSSIBLE_TEMPERATURE  // start-up state
            || persistent_data.desired_temperature != previous_desired_temperature // new desired temperature
            || persistent_data.mode != previous_mode // new mode
            || persistent_data.max_discrepancy_down != previous_max_discrepancy_down
            || persistent_data.max_discrepancy_up != previous_max_discrepancy_up
          )
        {
            // first time after reset or significant change in settings, so report
            // current temperature and use it to decide action
            send_report = 1;
            if (previous_temperature == IMPOSSIBLE_TEMPERATURE)
            {
                report_comment = "First time after reset";
                DOPRINTLN("report because first time through");
            }
            else
            {
                report_comment = "First time after change in settings";
                DOPRINTLN("report because of change in settings");
                if (switch_temperature != IMPOSSIBLE_TEMPERATURE)
                {
                    switch_temperature += persistent_data.desired_temperature - previous_desired_temperature;
                    DOPRINT  ("New switch temperature (settings change): ");
                    DOPRINTLN(switch_temperature);
                    DOPRINTLN("report because of change in settings");
                }
            }
            previous_desired_temperature = persistent_data.desired_temperature;
            previous_mode = persistent_data.mode;
            previous_temperature = current_temperature;
            previous_max_discrepancy_down = persistent_data.max_discrepancy_down;
            previous_max_discrepancy_up = persistent_data.max_discrepancy_up;
            temperature_to_report = current_temperature;
            do_check = 1;
        }
        else if (abs(previous_temperature - current_temperature) > persistent_data.precision)
        {
            // large enough change, so use for assessing direction of travel, store the new temperature as new previous value,
            // and indicate that we need to check whether power should be switched
            const char *change_name;
            change_name = NULL;
            if (previous_temperature < current_temperature)
            {
                // getting warmer
                if (temperature_changing <= 0) // was getting cooler, or in initial zero state
                {
                    temperature_changing = 1;
                    change_name = "warmer";
                    report_comment = "Started getting warmer";
                }
            }
            else // previous_temperature must be > current_temperature as we already tested for same (or very small diff)
            {
                // getting cooler
                if (temperature_changing >= 0) // was getting warmer, or in initial zero state
                {
                    temperature_changing = -1;
                    change_name = "cooler";
                    report_comment = "Started getting cooler";
                }
            }
            if (change_name != NULL)
            {
                // a reportable change has occurred
                DOPRINT("report because now getting ");
                DOPRINTLN(change_name);
                send_report = 1;
                temperature_to_report = previous_temperature;   // report the more extreme, now that we're going in the opposite direction
            }
            previous_temperature = current_temperature;
            do_check = 1;
        }

        millis_now = millis();

        if (do_check)
        {
            // check temperature and turn relay on/off as appropriate
            int8_t pre_relay_state = relay_state;
            if (abs(persistent_data.desired_temperature - current_temperature) > persistent_data.precision)
            {
                // sufficiently far from desired to make a change
                relay_state = assessRelayState(pre_relay_state);
            }
            if (relay_state != pre_relay_state)
            {
                if (relay_state)
                {
                    send_report = 1;
                    report_comment = "Turning on";
                    DOPRINTLN("report because turning on");
                    DOPRINTLN("turn on");
                    relay_state = POWER_ON;
                    time_when_switched_on = millis_now;
                    temp_when_switched_on = current_temperature;
                    digitalWrite(RELAY_PIN, 1);
                }
                else
                {
                    send_report = 1;
                    report_comment = "Turning off";
                    DOPRINTLN("report because turning off");
                    DOPRINTLN("turn off");
                    relay_state = POWER_OFF;
                    time_when_switched_off = millis_now;
                    temp_when_switched_off = current_temperature;
                    digitalWrite(RELAY_PIN, 0);
                }
            }
        }

        if (send_report)
        {
            sendReport(temperature_to_report, switch_temperature, relay_state, report_comment, &sensor_data);
            millis_at_last_report = millis_now;
        }
        else if ((millis_now - millis_at_last_report) > (persistent_data.max_time_between_reports * 1000))
        {
            char report_text[100];
            sprintf(report_text, "Time %u - %u > %u",
                        millis_now, millis_at_last_report, persistent_data.max_time_between_reports);
            DOPRINT  ("reporting because: ");
            DOPRINTLN(report_text);
            sendReport(temperature_to_report, switch_temperature, relay_state, report_text, &sensor_data);
            millis_at_last_report = millis_now;
        }

        setLEDflashing(0, 0);
    }
    // 1-sec delay between actions
    delay(1000);
}
