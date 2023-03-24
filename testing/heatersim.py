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
internal_filename = 'internal'
ambient_filename = 'ambient'
state_filename = 'state'
min_temp = 5
max_temp = 65
sleep_time = 0.1
heat_rate         = 3       # fixed amount of degrees of heat per iteration
cool_delta_ratio  = 0.1     # proportion per second by which internal temp approaches ambient temp
transfer_delta_ratio  = 0.005     # proportion per second by which temp approaches internal temp
switch_delay = 3

mode = 'heating'    # or cooling

if len(sys.argv) > 1:
    mode = sys.argv[1]

temp = float(open(temperature_filename).read())
ambient = float(open(ambient_filename).read())
internal_temp = ambient

print(ambient, internal_temp, temp)
n = 0
power_state = open(state_filename).read().strip()
change_at = pending_power_state = next_power_state = None

while True:
    next_power_state = open(state_filename).read().strip()
    if next_power_state != power_state:
        if next_power_state != pending_power_state:
            # change after a delay
            change_at = time.time() + switch_delay / time_acceleration
            pending_power_state = next_power_state
    elif next_power_state != pending_power_state:
        # changing back to previous state before pending state got applied
        change_at = pending_power_state = None

    if change_at and time.time() >= change_at:
        power_state = pending_power_state
        change_at = pending_power_state = None

    try:
        internal_temp = float(open(internal_filename).read().strip())
        os.remove(internal_filename)
    except:
        pass
    if power_state == 'OFF':
        internal_temp += cool_delta_ratio * sleep_time * (ambient - internal_temp)
    else:
        if mode == 'heating':
            internal_temp = min(max_temp, internal_temp + heat_rate * sleep_time)
        else:
            internal_temp = max(min_temp, internal_temp - heat_rate * sleep_time)
    temp = float(open(temperature_filename).read())
    diff = internal_temp - temp 
    if diff != 0:
        incr = diff * transfer_delta_ratio * sleep_time
        temp += incr
        print(temp, file=open(temperature_filename, 'w'))
    time.sleep(sleep_time / time_acceleration)
    n += 1
    if (n % 10) == 0:
        print(f'{power_state}({pending_power_state}) ambient: {ambient} internal: {internal_temp} current: {temp}')
