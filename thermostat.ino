/* Licensed under GNU General Public License v3.0
  See https://github.com/jeffasuk/thermostat/blob/master/LICENSE
  jeff at jamcupboard.co.uk
*/

#include <ESP8266WiFi.h>
#include "globals.h"
#include "led.h"
#include "network.h"
#include "eepromutils.h"
#include "sensors.h"
#include "webserver.h"

// for detecting change in desired temperature
static float previous_desired_temperature = IMPOSSIBLE_TEMPERATURE;
static uint8_t temperature_changing = 0;    // flag

enum STATE {STATE_INIT, STATE_ON, STATE_OVERSHOOT, STATE_OFF, STATE_UNDERSHOOT};
const char *stateName[] = {"INIT", "ON", "OVERSHOOT", "OFF", "UNDERSHOOT"}; // for debug and reporting

/* NB The terms "up" and "down" refer to the direction in which the power operates, so "up" means
   increasing temperature when heating, but decreasing temperature when cooling.
   Ditto for "max" and "min".
 */

float normalizeTemperature(float temperature)
{
    // override for inverted operation (i.e. cooling instead of heating)
    DOPRINT  ("normalizeTemperature ");
    DOPRINT  (temperature);
    DOPRINT  (" to ");
    if (persistent_data.mode == HEATING)
    {
        DOPRINTLN("same");
        return temperature;
    }
    // cooling; invert temperature
    float ret = (temperature == IMPOSSIBLE_TEMPERATURE) ? IMPOSSIBLE_TEMPERATURE : -temperature;
    DOPRINTLN(ret);
    return ret;
}
float getDesiredTemperature()
{
    return normalizeTemperature(persistent_data.desired_temperature);
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
        WiFi.mode(WIFI_AP_STA); // AP for now, then STA to check WiFi config when it's been set
        DOPRINTLN("Done WIFI_AP_STA");
        DOPRINTLN("WiFi.SSID: myESP");
        WiFi.softAP("myESP");
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

static uint32_t     millis_at_last_report = 0;
static float        previous_temperature = IMPOSSIBLE_TEMPERATURE;     // for detecting direction change

// the loop routine runs over and over again forever:
void loop()
{
    SENSOR_DATA sensor_data = {0};
    uint32_t    millis_now;

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
        relay_state = 0;
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
        DOPRINT  ("   current temperature ");
        DOPRINT  (current_temperature);
        DOPRINT  ("   temperature changing? ");
        DOPRINT  (temperature_changing);
        DOPRINT  ("   abs(change) ");
        DOPRINT  (abs(previous_temperature - current_temperature));
        DOPRINT  ("   precision ");
        DOPRINTLN(persistent_data.precision);
        if (previous_temperature == IMPOSSIBLE_TEMPERATURE || persistent_data.desired_temperature != previous_desired_temperature)
        {
            // first time after reset or change in desired temperature, so report current temperature and use it to decide action
            send_report = 1;
            if (previous_temperature == IMPOSSIBLE_TEMPERATURE)
            {
                report_comment = "First time after reset";
                DOPRINTLN("report because first time through");
            }
            else
            {
                report_comment = "First time after change in desired temperature";
                previous_desired_temperature = persistent_data.desired_temperature;
                DOPRINTLN("report because of change in desired temperature");
            }
            previous_temperature = current_temperature;
            temperature_to_report = current_temperature;
            do_check = 1;
        }
        else if (abs(previous_temperature - current_temperature) > persistent_data.precision)
        {
            // large enough change, so use for assessing direction of travel, store the new temperature as new previous value,
            // and indicate that we need to check whether power should be switched
            if (previous_temperature < current_temperature)
            {
                // getter warmer
                if (temperature_changing <= 0) // was getting cooler, or in initial zero state
                {
                    temperature_changing = 1;
                    send_report = 1;
                    report_comment = "Started getting warmer";
                    DOPRINTLN("report because now getting warmer");
                    temperature_to_report = previous_temperature;   // report the more extreme, now that we're going in the opposite direction
                }
            }
            else if (previous_temperature > current_temperature)
            {
                // getter cooler
                if (temperature_changing >= 0) // was getting warmer, or in initial zero state
                {
                    temperature_changing = -1;
                    send_report = 1;
                    report_comment = "Started getting cooler";
                    DOPRINTLN("report because now getting cooler");
                    temperature_to_report = previous_temperature;   // report the more extreme, now that we're going in the opposite direction
                }
            }
            previous_temperature = current_temperature;
            do_check = 1;
        }

        if (do_check)
        {
            // check temperature and turn relay on/off as appropriate
            int pre_relay_state = relay_state;
            if (abs(persistent_data.desired_temperature - current_temperature) > persistent_data.precision)
            {
                // sufficiently far from desired to make a change
                if (normalizeTemperature(current_temperature) < getDesiredTemperature())
                {
                    relay_state = 1;    // on
                }
                else
                {
                    relay_state = 0;    // off
                }
            }
            if (relay_state != pre_relay_state)
            {
                if (relay_state)
                {
                    send_report = 1;
                    report_comment = "Turning on";
                    DOPRINTLN("report because turning on");
                    DOPRINTLN("turn on");
                    digitalWrite(RELAY_PIN, 1);
                }
                else
                {
                    send_report = 1;
                    report_comment = "Turning off";
                    DOPRINTLN("report because turning off");
                    DOPRINTLN("turn off");
                    digitalWrite(RELAY_PIN, 0);
                }
            }
        }

        millis_now = millis();
        if (send_report)
        {
            sendReport(temperature_to_report, relay_state, report_comment, &sensor_data);
            millis_at_last_report = millis_now;
        }
        else if ((millis_now - millis_at_last_report) > (persistent_data.max_time_between_reports * 1000))
        {
            char report_text[100];
            sprintf(report_text, "Time %u - %u > %u",
                        millis_now, millis_at_last_report, persistent_data.max_time_between_reports);
            DOPRINT  ("reporting because: ");
            DOPRINTLN(report_text);
            sendReport(temperature_to_report, relay_state, report_text, &sensor_data);
            millis_at_last_report = millis_now;
        }

        setLEDflashing(0, 0);
    }
    // 1-sec delay between actions
    delay(1000);
}
