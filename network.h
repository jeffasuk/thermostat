/* Licensed under GNU General Public License v3.0
  See https://github.com/jeffasuk/thermostat
  jeff at jamcupboard.co.uk
*/

#ifndef _NETWORK_H
#define _NETWORK_H

#include "sensors.h"
void startAccessPoint();
uint8_t connectWiFi();
int connectTCP();
void getResponse(WiFiClient client);
void sendReport(float current_temperature, int8_t power_state, int8_t main_state,
        float switch_offset_below, float switch_offset_above,
        const char *comment, SENSOR_DATA *sensor_data);
uint8_t getSettings();

#endif
