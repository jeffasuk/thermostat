#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "globals.h"
#define DOPRINT(x) std::cerr << x
#define DOPRINTLN(x) {DOPRINT(x); std::cerr << "\n"; }

/* globals in the .py simulator
    temperature, target_temperature, switch_temperature, past_max, past_min, history_length,
    max_temperature, min_temperature, done_cycle
*/

float min(float f1, float f2)
{
    return f1 < f2 ? f1 : f2;
}

float max(float f1, float f2)
{
    return f1 > f2 ? f1 : f2;
}

float readFloatFromFile(char *fname)
{
    FILE *inf;
    float res;
    while (inf = fopen(fname, "r"), fscanf(inf, "%f", &res) < 0)
    {
        fclose(inf);
    }
    fclose(inf);
    return res;
}

static char *state_name[2] = {"OFF", "ON"};

static uint32_t millis_now;
static time_t   sec_at_start;

int main(int argc, char **argv)
{
    FILE *state_file;
    int relay_state, prev_relay_state = -999;
    char state_buf[20];
    float time_acceleration = 1.0;
    float prev_switch_temperature = switch_temperature + 1; // ensure change
    {
        const char* s = getenv("TIME_ACCELERATION");
        if (s)
        {
            sscanf(s, "%f", &time_acceleration);
            printf("Accelerate at x %f\n", time_acceleration);
        }
    }
    time(&sec_at_start);
    state_file = fopen("state", "r");
    fscanf(state_file, "%s", state_buf);
    fclose(state_file);
    printf("'%s'\n", state_buf);
    while (1)
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);

        current_temperature = readFloatFromFile("temperature");
        persistent_data.desired_temperature = readFloatFromFile("target_temperature");
        // default to heating
        state_buf[0] = 'h';
        if ( (state_file = fopen("mode", "r")) != NULL)
        {
            fscanf(state_file, "%s", state_buf);
            fclose(state_file);
        }
        persistent_data.mode = (state_buf[0] == 'c') ? COOLING : HEATING;
        prev_relay_state = relay_state;
        millis_now = time_acceleration * ((tv.tv_sec - sec_at_start) * 1000000 + tv.tv_usec) / 1000;
        relay_state = assessRelayState(relay_state);
        if (relay_state != prev_relay_state)
        {
            state_file = fopen("state", "w");
            fprintf(state_file, "%s\n", state_name[relay_state]);
            fclose(state_file);
            printf("%d.%6.6d: Switched %s\n", tv.tv_sec, tv.tv_usec, state_name[relay_state]);
        }
        if (prev_switch_temperature != switch_temperature)
        {
            state_file = fopen("switch", "w");
            fprintf(state_file, "%f\n", switch_temperature);
            fclose(state_file);
        }
        usleep(1000000/ time_acceleration);
    }
    return 0;
}
