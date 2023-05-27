/* Licensed under GNU General Public License v3.0
  See https://github.com/jeffasuk/thermostat
  jeff at jamcupboard.co.uk
*/
#include <Arduino.h>
#include "OneWire.h"
#include <DallasTemperature.h>
#include "globals.h"
#include "utils.h"
#include "sensors.h"

// enable debug printing in this module
//#define MYDOPRINT   DOPRINT
//#define MYDOPRINTLN DOPRINTLN

// disable debug printing in this module
#define MYDOPRINT(x)
#define MYDOPRINTLN(x)

static void showaddr(unsigned char  addr[8])
{
#ifndef QUIET
    char buf[17];
    MYDOPRINT(formatAddr(buf, addr));
#endif
}

typedef unsigned char   DEVICEADDR[8];
OneWire  ds(persistent_data.onewire_pin);
DallasTemperature sensors(&ds);
static uint8_t done_begin = 0;

void readSensors(SENSOR_DATA *result)
{
    int i;
    int ds_start;
    TEMPERATURE_DATA temp_data;
    static DEVICEADDR foundaddrs[MAX_TEMPERATURE_SENSORS];
    static int nb_foundaddrs = 0;
    int new_foundaddrs;

    // Set all readings to "not ok", so caller knows if each reading is valid
    for (i = 0; i < MAX_TEMPERATURE_SENSORS; ++i)
    {
        result->temperature[i].ok = ONEWIRE_NO_RESULT;
    }
    ds_start = 1;
    MYDOPRINTLN("");
    MYDOPRINT("Read temperature sensors on pin ");
    MYDOPRINTLN(persistent_data.onewire_pin);
    if (!done_begin)
    {
        sensors.begin();
        sensors.setResolution(12);
        done_begin = 1;
    }
    new_foundaddrs = sensors.getDeviceCount();
    if (new_foundaddrs != nb_foundaddrs)
    {
        // changed number of sensors, so go back to the beginning
        // NB this means that if you're changing a sensor, wait at least one cycle after
        //  unplugging the old one before plugging the new one in.
        memset(foundaddrs, sizeof foundaddrs, 0);
        nb_foundaddrs = new_foundaddrs;
        result->nb_temperature_sensors = nb_foundaddrs;
        // Search the wire for addresses
        for (int i=0; i < nb_foundaddrs; i++)
        {
            if (sensors.getAddress(foundaddrs[i], i))
            {
                MYDOPRINT("Found device ");
                MYDOPRINT(i);
                MYDOPRINT(" with address: ");
                showaddr(foundaddrs[i]);
                MYDOPRINTLN("");
            }
            else
            {
                MYDOPRINTLN("Bad address");
            }
        }
    }

    MYDOPRINT(millis());
    MYDOPRINTLN(" getDeviceCount");
    result->nb_temperature_sensors = sensors.getDeviceCount();
    MYDOPRINT(millis());
    MYDOPRINTLN(result->nb_temperature_sensors);
    MYDOPRINT(millis());
    MYDOPRINTLN(" requestTemperatures");
    sensors.requestTemperatures();
    for (int i=0; i < result->nb_temperature_sensors; i++)
    {
        TEMPERATURE_DATA *res = &(result->temperature[i]);
        MYDOPRINT(millis());
        MYDOPRINT("  temp ");
        MYDOPRINT(i);
        MYDOPRINT(" ");
        showaddr(foundaddrs[i]);
        MYDOPRINTLN("");
        float temp_c = sensors.getTempC(foundaddrs[i]);
        MYDOPRINT(millis());
        MYDOPRINT(" ");
        MYDOPRINTLN(temp_c);
        if (temp_c == -127.00)
        {
            MYDOPRINTLN("Failed to read sensor");
            continue;
        }
        // else the reading was OK
        res->ok = ONEWIRE_OK;
        res->temperature_c = temp_c;
        res->temperature_f = DallasTemperature::toFahrenheit(temp_c);
        memcpy(res->addr, foundaddrs[i], sizeof foundaddrs[i]);
    }
    MYDOPRINT(millis());
    MYDOPRINT(" end get temperatures from ");
    MYDOPRINT(result->nb_temperature_sensors);
    MYDOPRINTLN(" sensors");
}
