#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "globals.h"


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
    int errcount = 0;
    while (inf = fopen(fname, "r"), fscanf(inf, "%f", &res) < 0)
    {
        fclose(inf);
        if (++errcount > 10)
            break;
        usleep(200000);
    }
    fclose(inf);
    return res;
}


void writeFloatToFile(char *fname, float val)
{
    FILE *inf;
    int errcount = 0;
    inf = fopen(fname, "w");
    fprintf(inf, "%f\n", val);
    fclose(inf);
}

static char *state_name[2] = {"OFF", "ON"};

static uint32_t millis_now;
static time_t   sec_at_start;

static uint32_t sleep_time_usec = 1000000;

int main(int argc, char **argv)
{
    FILE *state_file;
    int relay_state = 0, prev_relay_state = -999;
    char state_buf[20];
    float time_acceleration = 1.0;

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

        usleep(sleep_time_usec / time_acceleration);

        current_temperature = readFloatFromFile("temperature");
        if (abs(current_temperature - previous_temperature) < 0.06)
        {
            continue;   // not enough change to bother with (as per the live code/sensor resolution)
        }

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
            writeFloatToFile("switch_offset_below", switch_offset_below);
            writeFloatToFile("switch_offset_above", switch_offset_above);
            printf("%d: Switched %s\n", millis_now, state_name[relay_state]);
        }
        previous_temperature = current_temperature;
    }
    return 0;
}
