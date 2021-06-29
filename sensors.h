/* Licensed under GNU General Public License v3.0
  See https://github.com/jeffasuk/thermostat/blob/master/LICENSE
  jeff at jamcupboard.co.uk
*/

#ifndef _SENSORS_H
#define _SENSORS_H

typedef enum {
    DS_OK = 0,
    DS_NOTHING_FOUND,
    DS_INVALID_CRC,
    DS_UNKNOWN_DEVICE
} DS_RESULT_CODE;

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

// OneWire return codes
#define ONEWIRE_OK  0
#define ONEWIRE_CHKSUM_ERR -1
#define ONEWIRE_TIMEOUT -2
#define ONEWIRE_NO_RESULT -3    // Not actually a return code, but indicator that we didn't get a result

void readSensors(SENSOR_DATA *result);
int readDsTemp(int start, TEMPERATURE_DATA *res);
extern void generateSensorPage(String *page, SENSOR_DATA sensor_data);

#endif  // _SENSORS_H
