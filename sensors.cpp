/* Licensed under GNU General Public License v3.0
  See https://github.com/jeffasuk/thermostat/blob/master/LICENSE
  jeff at jamcupboard.co.uk
*/
#include <Arduino.h>
#include "OneWire.h"
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
    char buf[17];
    MYDOPRINT(formatAddr(buf, addr));
}


int readDsTemp(OneWire *ds, int reset_search, TEMPERATURE_DATA *res)
{
    char i;
    char present = 0;
    char data[12];
    float temp_c;

    if (reset_search)
    {
        MYDOPRINTLN("Reset search for onewire devices");
        ds->reset_search();
    }

    res->ok = DS_NOTHING_FOUND;
    if (!ds->search(res->addr))
    {
        MYDOPRINTLN("End of onewire devices");
        return 0;
    }

    if (OneWire::crc8(res->addr, 7) != res->addr[7])
    {
        MYDOPRINTLN("Invalid CRC");
        res->ok = DS_INVALID_CRC;
        return 1;
    }

    if (res->addr[0] != 0x10 and res->addr[0] != 0x28 and res->addr[0] != 0x22)
    {
        MYDOPRINTLN("Unknown device");
        res->ok = DS_UNKNOWN_DEVICE;
        return 1;
    }

    MYDOPRINTLN("Found onewire device ");
    showaddr(res->addr);
    MYDOPRINTLN("");
    ds->reset();
    ds->select(res->addr);
    ds->write(0x44); // start conversion

    delay(1500);     // maybe 750ms is enough, maybe not

    present = ds->reset();
    ds->select(res->addr);    
    ds->write(0xBE);         // Read Scratchpad

    for (i = 0; i < sizeof data; i++)
    {
        data[i] = ds->read();
    }

    temp_c = (float)( ((data[1] << 8) + data[0]) & 0xffff) / 16.0;
    // Apply a sanity check. We occasionally get a reading of several thousands of degrees
    if (temp_c < -100  ||  temp_c > 300)
    {
        res->ok = DS_NOTHING_FOUND;
        return 1;
    }
    res->ok = ONEWIRE_OK;
    res->temperature_c = temp_c;
    res->temperature_f = res->temperature_c * 1.8 + 32.0;
#ifndef QUIET
    {
        char num_buf[7];
        MYDOPRINT("temp (");
        sprintf(num_buf, "0x%x", ((data[1] << 8) + data[0]) & 0xffff);
        MYDOPRINT(num_buf);
        MYDOPRINT(") ");
        MYDOPRINTLN(res->temperature_c);
    }
#endif
    return 1;   // try to find next sensor
}

void readSensors(SENSOR_DATA *result)
{
    int i;
    int ds_start;
    TEMPERATURE_DATA temp_data;
    int got_values[MAX_TEMPERATURE_SENSORS];
    OneWire  *ds = new OneWire(persistent_data.onewire_pin);

    // Set all readings to "not ok", so caller knows if each reading is valid
    for (i = 0; i < MAX_TEMPERATURE_SENSORS; ++i)
    {
        result->temperature[i].ok = ONEWIRE_NO_RESULT;
    }
    ds_start = 1;
    memset(got_values, 0, sizeof got_values);
    MYDOPRINT("Read temperature sensors on pin ");
    MYDOPRINTLN(persistent_data.onewire_pin);
    pinMode(persistent_data.onewire_pin, INPUT_PULLUP);
    while (readDsTemp(ds, ds_start, &temp_data))
    {
        if (temp_data.ok == ONEWIRE_OK)
        {
            int first_empty = -1;
            int stored = 0;
            for (i = 0; i < MAX_TEMPERATURE_SENSORS; ++i)
            {
                if (!memcmp(temp_data.addr, result->temperature[i].addr, sizeof temp_data.addr))
                {
                    MYDOPRINT("replace temp for ");
                    showaddr(temp_data.addr);
                    MYDOPRINT(" at idx ");
                    MYDOPRINT(i);
                    MYDOPRINT(": ");
                    MYDOPRINTLN(temp_data.temperature_c);
                    result->temperature[i] = temp_data;
                    stored = 1;
                    if (result->nb_temperature_sensors < (i + 1)) 
                    {
                        result->nb_temperature_sensors = i + 1;
                    }
                    break;
                }
                if (first_empty < 0 && !result->temperature[i].addr[0])    // Assumes addr never starts with 0. May be invalid.
                {
                    first_empty = i;
                }
            }
            if (!stored && first_empty >= 0)
            {
                // Not seen this addr before, so put data in an empty slot.
                MYDOPRINT("store temp for ");
                showaddr(temp_data.addr);
                MYDOPRINT(" at idx ");
                MYDOPRINT(first_empty);
                MYDOPRINT(": ");
                MYDOPRINTLN(temp_data.temperature_c);
                result->temperature[first_empty] = temp_data;
                if (result->nb_temperature_sensors < (first_empty + 1)) 
                {
                    result->nb_temperature_sensors = first_empty + 1;
                }
            }
        }
        else
        {
            MYDOPRINTLN("not OK");
        }
        ds_start = 0;
    }

    MYDOPRINT("nb temperatures ");
    MYDOPRINTLN(result->nb_temperature_sensors);
    delete ds;
}
