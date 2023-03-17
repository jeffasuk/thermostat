#!/usr/bin/python
'''Heater simulator.
Read state file. Adjust temperature in the temperature file.
Regular reduction in temperature to simulate steady cooling.
    Could vary this by allowing for difference between temp and ambient temp.
When power is switched on, steadily increase temperature increments up to a maximum.
When power is switched off, steadily decrease temperature increments down to zero.
'''


import os
import sys
import time
time_acceleration = float(os.environ.get('TIME_ACCELERATION', 1))
temperature_filename = 'temperature'
state_filename = 'state'
sleep_time = 0.1

max_heat_delta  = 0.1        # degrees per second
cool_delta      = 0.02       # degrees per second

build_up_time   =  5         # seconds over which heat_delta builds to max
wind_down_time  = 20         # seconds over which heat_delta drops to zero

# states
# OFF, RAMPING_UP, ON, RAMPING_DOWN
state = 'OFF'
old_power_state = None
mode = 'heating'    # or cooling

if len(sys.argv) > 1:
    mode = sys.argv[1]

def setState(new_state):
    global state
    state = new_state

while True:
    temp_change = 0     # as a per-second value
    power_state = open(state_filename).read().strip()
    if power_state != old_power_state:
        old_power_state = power_state
    if state == 'OFF':
        if power_state == 'ON':
            setState('RAMPING_UP')
            heat_delta = 0
    elif state == 'RAMPING_UP':
        heat_delta += sleep_time * max_heat_delta / build_up_time
        temp_change = heat_delta
        if heat_delta >= max_heat_delta:
            setState('ON')    # finished ramping up
        if power_state == 'OFF':
            setState('RAMPING_DOWN')
            #print(f'heat_delta = {heat_delta}')
    elif state == 'RAMPING_DOWN':
        heat_delta -= sleep_time * max_heat_delta / wind_down_time
        if heat_delta > 0:
            temp_change = heat_delta
        else:
            setState('OFF')   # finished ramping down
            heat_delta = 0
        if power_state == 'ON':
            setState('RAMPING_UP')
    elif state == 'ON':
        temp_change = max_heat_delta
        if power_state == 'OFF':
            setState('RAMPING_DOWN')

    temp = float(open(temperature_filename).read())
    if mode == 'cooling':
        temp = -temp
    change = (temp_change - cool_delta) * sleep_time
    #print(temp_change, cool_delta, change, temp, temp+change)
    if change != 0:
        temp += change
        if mode == 'cooling':
            temp = -temp
        print(temp, file=open(temperature_filename, 'w'))
    time.sleep(sleep_time / time_acceleration)
