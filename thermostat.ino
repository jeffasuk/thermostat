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

/* NB The terms "up" and "down" refer to the direction in which the power operates, so "up" means
   increasing temperature when heating, but decreasing temperature when cooling.
   Ditto for "max" and "min".
 */

// for detecting change in desired temperature and other settings
static float previous_desired_temperature = IMPOSSIBLE_TEMPERATURE;
static uint8_t previous_mode = 99;  // matches neither of the valid mode values

static int8_t temperature_changing = 0;    // -1 = going down,  0 = not changing,  +1 = going up

enum RELAY_STATE {POWER_OFF, POWER_ON};
const char *powerStateName[] = {"OFF", "ON"}; // for debug and reporting

// Some odd effects can happen if the offsets are changed while we're deciding whether to switch
// on or off. In such circumstances, use these pending variables so the change can be applied when safe.
static float pending_switch_offset_above = IMPOSSIBLE_TEMPERATURE;
static float pending_switch_offset_below = IMPOSSIBLE_TEMPERATURE;

// A pair of offset values indicates a range of below-above the desired temperature. They thus divide the full
// temperature range into 4 regions:
//  High        = above desired + switch_offset_above
//  Mid High    = below desired + switch_offset_above but above desired
//  Mid Low     = below desired but above desired + switch_offset_below
//  Low         = below desired + switch_offset_below
// (switch_offset_below should be negative)
typedef enum REGION {REGION_HIGH, REGION_MID_HIGH, REGION_MID_LOW, REGION_LOW};

static uint32_t     millis_now;
static uint32_t     millis_at_last_report = 0;
static float        previous_temperature = IMPOSSIBLE_TEMPERATURE;     // for detecting direction of change

// for comparing how long power is on vs. how long off
static uint32_t time_when_switched_on = 0;
static uint32_t time_when_switched_off = 0;
static uint32_t length_of_last_on_period = 0;
static uint32_t length_of_last_off_period = 0;

// rotating history of discrepancies from switch temperature
#define HISTORY_CYCLES  5   // number of on/off cycles to keep history of
#define HISTORY_LENGTH  (HISTORY_CYCLES*2)  // must be even number, to catch equal number of peaks and troughs
static float past_peaks_and_troughs[HISTORY_LENGTH] = {0};
static uint8_t history_index = HISTORY_LENGTH-1;
static float min_temperature = IMPOSSIBLE_TEMPERATURE;
static float max_temperature = IMPOSSIBLE_TEMPERATURE;

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
static void revertToStartupAlgorithm()
{
    // Clear out history and such to revert to start-up state.
    history_index = HISTORY_LENGTH-1;
    for (int i = 0; i < HISTORY_LENGTH; ++i)
    {
        past_peaks_and_troughs[i] = 0.0;
    }
    switch_offset_above = switch_offset_below = 0;
}

// Record peaks and troughs w.r.t. the current switch temperature.
// Will be used later for tuning.
static void recordPeaksAndTroughs(float switch_temperature, int8_t new_relay_state)
{
    float diff;
    if ( (new_relay_state == POWER_OFF && persistent_data.mode == HEATING)
      || (new_relay_state == POWER_ON  && persistent_data.mode == COOLING)
        )
    {
        // just switched heating off or cooling on, so we're heading for a temperature peak
        diff = min_temperature - switch_temperature;
        // start recording max temperature in order to capture level of next temperature peak
        max_temperature = current_temperature;
    }
    else  // must have just switched heating on or cooling off, so we're heading for a temperature trough
    {
        diff = max_temperature - switch_temperature;
        // start recording min temperature in order to capture level of next temperature trough
        min_temperature = current_temperature;
    }
    // record the temperature discrepancy at the preceding temperature peak/trough...
    history_index = (history_index + 1) % HISTORY_LENGTH;
    past_peaks_and_troughs[history_index] = diff;
}

static void assessPerformance()
{
 static int nb_cycles = 0;   // don't assess until we've gone round at least once
    // we're about to switch on, so assess performance
    float average_discrepancy = 0;
    float min_val = past_peaks_and_troughs[0];
    float max_val = past_peaks_and_troughs[0];

    if (++nb_cycles < HISTORY_CYCLES)
    {
        return;
    }
    nb_cycles = HISTORY_CYCLES; // to avoid overflow, not that the device is likely to run that long withour a reset
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
    DOPRINT(" Ignoring extreme values ");
    DOPRINT(min_val);
    DOPRINT(" and ");
    DOPRINTLN(max_val);
    // adjust switch offsets
    float relative_switch_temperature = (switch_offset_above + switch_offset_below) / 2;
    // discrepancy_from_desired is what we need the offset to be
    float discrepancy_from_desired = relative_switch_temperature - average_discrepancy;
    if (discrepancy_from_desired != 0)
    {
        if (discrepancy_from_desired > 0)
        {
            // changing up
            if (temperature_changing > 0)
            {
                pending_switch_offset_above = discrepancy_from_desired;
            }
            else
            {
                switch_offset_above = discrepancy_from_desired;
            }
        }
        else if (discrepancy_from_desired < 0)
        {
            // changing down
            // if temp falling, make pending so as not to trigger an immediate switch-off by immediately
            // applying the new value
            if (temperature_changing < 0)
            {
                pending_switch_offset_below = discrepancy_from_desired;
            }
            else
            {
                switch_offset_below = discrepancy_from_desired;
            }
        }
    }
}

static int8_t assessRelayState(int8_t pre_relay_state)
{
static float local_previous_temperature = IMPOSSIBLE_TEMPERATURE;    // NB: separate from previous_temperature used in reporting (in loop)
    uint8_t switched = 0;
    int8_t heating_is_more_powerful = 0;    // -1 == No,  0 == undecided,  +1 = Yes
    int8_t new_relay_state = pre_relay_state;
    float norm_temp;
    int norm_changing;
    float switch_on_temperature;
    float switch_off_temperature;
    float switch_mid_temperature;
    REGION region;

    setIfImpossible(&max_temperature, current_temperature);
    setIfImpossible(&min_temperature, current_temperature);
    setIfImpossible(&local_previous_temperature, current_temperature);
    temperature_changing = (local_previous_temperature == current_temperature) ? 0
                    : ( (local_previous_temperature < current_temperature) ? 1 : -1);

    if (persistent_data.mode == HEATING)
    {
        switch_on_temperature = persistent_data.desired_temperature + switch_offset_above;
        switch_off_temperature = persistent_data.desired_temperature + switch_offset_below;
        norm_changing = temperature_changing;
    }
    else
    {
        // reverse meaning of offsets and temperature direction when cooling
        switch_on_temperature = persistent_data.desired_temperature + switch_offset_below;
        switch_off_temperature = persistent_data.desired_temperature + switch_offset_above;
        norm_changing = -temperature_changing;
    }

    switch_mid_temperature = (switch_on_temperature + switch_off_temperature) / 2.0;

    norm_temp = normalizeTemperature(current_temperature);

    local_previous_temperature = current_temperature;

    if (length_of_last_on_period != 0 && length_of_last_off_period != 0)
    {
        if (length_of_last_on_period < length_of_last_off_period)
        {
            heating_is_more_powerful = 1;   // yes
        }
        else if (length_of_last_on_period > length_of_last_off_period)
        {
            heating_is_more_powerful = -1;   // no
        }
    }
    // else leave at zero for undecided

    // indicate which region we're in to simplify code below.
    region =  (norm_temp > normalizeTemperature(switch_on_temperature))      ? REGION_HIGH
            : (norm_temp > normalizeTemperature(switch_mid_temperature))    ? REGION_MID_HIGH
            : (norm_temp >= normalizeTemperature(switch_off_temperature))    ? REGION_MID_LOW
            : REGION_LOW;

    if (pre_relay_state == POWER_ON)
    {   
        uint8_t do_switch = 0;
        if (region == REGION_HIGH)
        {
            // above the upper temperature; always turn off
            DOPRINTLN("High: switch OFF");
            do_switch = 1;
        }
        else
        {
            switch(heating_is_more_powerful)
            {
                case 1:
                    {
                        // heating is more powerful (i.e. stays on for less time than cooling)
                        // so heating happens in Low, and in Mid-low if falling. Anywhere else, switch off.
                        // That is in High, Mid-high, and Mid-low if rising
                        // High has already been dealt with above
                        if ( region == REGION_MID_HIGH
                            || (region == REGION_MID_LOW && norm_changing > 0) )
                        {
                            if ( region == REGION_MID_HIGH)
                            {
                                DOPRINTLN("Mid-high and heating more powerful: switch OFF");
                            }
                            else
                            {
                                DOPRINTLN("Mid-low and heating more powerful and temp rising: switch OFF");
                            }
                            do_switch = 1;
                        }
                    }
                    break;
                case -1:
                    {
                        // heating is less powerful (i.e. stays on less time than cooling)
                        // so heating happens in Low, in Mid-low and in Mid-high if falling. Anywhere else, switch off.
                        // That is in High, and Mid-high if rising
                        // High has already been dealt with above
                        if ( region == REGION_MID_HIGH && norm_changing > 0)
                        {
                            DOPRINTLN("Mid-high and heating less powerful and temp rising: switch OFF");
                            do_switch = 1;
                        }
                    }
                    break;
                case 0:
                    {
                        // heating and cooling times are balanced. (Unlikely to be exact; maybe make the logic a bit fuzzy)
                        // In either mid-range, switch off if rising
                        if ( (region == REGION_MID_HIGH || region == REGION_MID_LOW)
                                && norm_changing > 0)
                        {
                            DOPRINTLN("Balanced: switch OFF");
                            do_switch = 1;
                        }
                    }
            }
        }
        if (do_switch)
        {
            new_relay_state = POWER_OFF;
            switched = 1;
            time_when_switched_off = millis_now;
            if (time_when_switched_on != 0)
            {
                length_of_last_on_period = millis_now - time_when_switched_on;
            }
        }
    }
    else // pre_relay_state == POWER_OFF
    {   
        uint8_t do_switch = 0;
        if (region == REGION_LOW)
        {
            // below the lower temperature; always turn on
            DOPRINTLN("Low: switch ON");
            do_switch = 1;
        }
        else
        {
            switch(heating_is_more_powerful)
            {
                case 1:
                    {
                        // heating is more powerful (i.e. stays on longer than cooling)
                        // so heating happens in Low, and in Mid-low if falling.
                        // Low has already been dealt with above
                        if ( region == REGION_MID_LOW && norm_changing < 0)
                        {
                            DOPRINTLN("Mid-low and heating more powerful and temp falling: switch ON");
                            do_switch = 1;
                        }
                    }
                    break;
                case -1:
                    {
                        // heating is less powerful (i.e. stays on less time than cooling)
                        // so heating happens in Low, in Mid-low and in Mid-high if falling.
                        // Low has already been dealt with above
                        if ( region == REGION_MID_LOW
                             || (region == REGION_MID_HIGH && norm_changing < 0) )
                        {
                            if ( region == REGION_MID_LOW)
                            {
                                DOPRINTLN("Mid-low and heating less powerful: switch ON");
                            }
                            else
                            {
                                DOPRINTLN("Mid-low and heating less powerful and temp falling: switch ON");
                            }
                            do_switch = 1;
                        }
                    }
                    break;
                case 0:
                    {
                        // heating and cooling times are balanced. (Unlikely to be exact; maybe make the logic a bit fuzzy)
                        // In either mid-range, switch on if falling
                        if ( (region == REGION_MID_HIGH || region == REGION_MID_LOW)
                                && norm_changing < 0)
                        {
                            DOPRINTLN("Balanced: switch ON");
                            do_switch = 1;
                        }
                    }
            }
        }
        if (do_switch)
        {
            new_relay_state = POWER_ON;
            switched = 1;
            time_when_switched_on = millis_now;
            if (time_when_switched_off != 0)
            {
                length_of_last_off_period = millis_now - time_when_switched_off;
            }
        }
    }
    if (switched)
    {
        recordPeaksAndTroughs(switch_mid_temperature, new_relay_state);
    }

    max_temperature = max(max_temperature, current_temperature);
    min_temperature = min(min_temperature, current_temperature);

    if (switched && new_relay_state == POWER_ON)
    {
        assessPerformance();
    }

    if (switched && new_relay_state == POWER_OFF && pending_switch_offset_below != IMPOSSIBLE_TEMPERATURE)
    {
        switch_offset_below = pending_switch_offset_below;
        pending_switch_offset_below = IMPOSSIBLE_TEMPERATURE;
        DOPRINT("Apply pending switch-offset-below: ");
        DOPRINTLN(switch_offset_below);
    }

    if (switched && new_relay_state == POWER_ON && pending_switch_offset_above != IMPOSSIBLE_TEMPERATURE)
    {
        switch_offset_above = pending_switch_offset_above;
        pending_switch_offset_above = IMPOSSIBLE_TEMPERATURE;
        DOPRINT("Apply pending switch-offset-above: ");
        DOPRINTLN(switch_offset_above);
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

    uint32_t millis_at_loop_start = millis();

    setLEDflashing(100, 400);
    readSensors(&sensor_data);
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
        int do_check = 0;
        char report_text[100] = "";
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
        DOPRINT  ("   switching range ");
        DOPRINT  (switch_offset_below);
        DOPRINT  (" .. ");
        DOPRINT  (switch_offset_above);
        DOPRINT  ("  for ");
        DOPRINTLN(persistent_data.mode == HEATING ? "heating" : "cooling");
#endif
        if (previous_temperature == IMPOSSIBLE_TEMPERATURE  // start-up state
            || persistent_data.desired_temperature != previous_desired_temperature // new desired temperature
            || persistent_data.mode != previous_mode // new mode
          )
        {
            // first time after reset or significant change in settings, so report
            // current temperature and use it to decide action
            if (previous_temperature == IMPOSSIBLE_TEMPERATURE)
            {
                strcat(report_text, "First time after reset. ");
                DOPRINTLN("report because first time through");
            }
            else
            {
                strcat(report_text, "First time after change in settings");
                DOPRINTLN("report because of change in settings");
            }
            previous_desired_temperature = persistent_data.desired_temperature;
            previous_mode = persistent_data.mode;
            previous_temperature = current_temperature;
            temperature_to_report = current_temperature;
            revertToStartupAlgorithm();
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
                    strcat(report_text, "Started getting warmer");
                }
            }
            else // previous_temperature must be > current_temperature as we already tested for same (or very small diff)
            {
                // getting cooler
                if (temperature_changing >= 0) // was getting warmer, or in initial zero state
                {
                    temperature_changing = -1;
                    change_name = "cooler";
                    strcat(report_text, "Started getting cooler");
                }
            }
            if (change_name != NULL)
            {
                // a reportable change has occurred
                DOPRINT("report because now getting ");
                DOPRINTLN(change_name);
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
                //TODO Consider whether this should be conditional (probably not, as there's a check on change amount above).
                // sufficiently far from desired to make a change
                relay_state = assessRelayState(pre_relay_state);
            }
            if (relay_state != pre_relay_state)
            {
                if (relay_state)
                {
                    strcat(report_text, "Turning on");
                    DOPRINTLN("report because turning on");
                    DOPRINTLN("turn on");
                    relay_state = POWER_ON;
                    digitalWrite(RELAY_PIN, 1);
                }
                else
                {
                    strcat(report_text, "Turning off");
                    DOPRINTLN("report because turning off");
                    DOPRINTLN("turn off");
                    relay_state = POWER_OFF;
                    digitalWrite(RELAY_PIN, 0);
                }
            }
        }

        if (!report_text[0]
                && (millis_now - millis_at_last_report) > (persistent_data.max_time_between_reports * 1000))
        {
            sprintf(report_text, "Time %u - %u > %u",
                        millis_now, millis_at_last_report, persistent_data.max_time_between_reports);
        }
        if (report_text[0])
        {
            DOPRINT  ("reporting because: ");
            DOPRINTLN(report_text);
            sendReport(temperature_to_report, relay_state, switch_offset_below, switch_offset_above,
                        report_text, &sensor_data);
            millis_at_last_report = millis_now;
        }

        setLEDflashing(0, 0);
    }
    // min 1-sec spacing between actions, allowing for how much time was spent actually doing stuff
    // always have a non-zero delay call to let other operations in (probably unnecessary, but no harm).
    delay(max(1, int(1000 - (millis() - millis_at_loop_start))));
}
