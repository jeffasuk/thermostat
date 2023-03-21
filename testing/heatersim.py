#!/usr/bin/python
'''Heater simulator.
Read state file. Adjust temperature in the temperature file.
Maintain an internal temperature to represent temperature of the heating element.
While heating is on, increase at a regular amount up to a maximum.
While heating is off, decrease at a regular amount down to the ambient temperature.
On each iteration, change the temperature by a proportion of the difference between internal and current.
Don't need separate cooling factor, as "heating element" represents ambient.
'''


import os
import sys
import time
time_acceleration = float(os.environ.get('TIME_ACCELERATION', 1))
temperature_filename = 'temperature'
ambient_filename = 'ambient'
state_filename = 'state'
min_temp = 5
max_temp = 65
sleep_time = 0.1
heat_delta_ratio  = 0.001     # proportion per second
cool_delta_ratio  = 0.05    # proportion per second
transfer_delta_ratio  = 0.05     # proportion per second

mode = 'heating'    # or cooling

if len(sys.argv) > 1:
    mode = sys.argv[1]

temp = float(open(temperature_filename).read())
ambient = float(open(ambient_filename).read())
internal_temp = ambient

print(ambient, internal_temp, temp)
n = 0
while True:
    power_state = open(state_filename).read().strip()
    if power_state == 'OFF':
        internal_temp += cool_delta_ratio * sleep_time * (ambient - internal_temp)
    else:
        if mode == 'heating':
            change = heat_delta_ratio * sleep_time * (max_temp - internal_temp)
        else:
            change = heat_delta_ratio * sleep_time * (min_temp - internal_temp)
        internal_temp += change
    temp = float(open(temperature_filename).read())
    diff = internal_temp - temp 
    if diff != 0:
        incr = diff * transfer_delta_ratio * sleep_time
        temp += incr
        print(temp, file=open(temperature_filename, 'w'))
    time.sleep(sleep_time / time_acceleration)
    n += 1
    if (n % 10) == 0:
        print(ambient, internal_temp, temp)
