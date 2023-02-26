/* Licensed under GNU General Public License v3.0
  See https://github.com/jeffasuk/thermostat
  jeff at jamcupboard.co.uk
*/
#include <string.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "globals.h"
#include "led.h"
#include "sensors.h"
#include "network.h"
#include "persistence.h"
#include "utils.h"

WiFiClient client;

static char APtag[] = "thermostat  ";

static char *unrot(char *rotated_pswd)
{
    char *passclear, *in, *out;
    int lenpswd;
    lenpswd = strlen(rotated_pswd);
    passclear = (char*)malloc(lenpswd+1);
    for (in=rotated_pswd, out=passclear; *in; ++in, ++out)
    {
        *out = (char)(((((unsigned int)(*in) << 8) + (unsigned int)(*in)) >> (8 - persistent_data.rot) ) & 0xFF);
    }
    *out = 0;
    return passclear;
}

void startAccessPoint()
{
    uint8_t mac[WL_MAC_ADDR_LENGTH];
    char *tag_p;
    uint8_t *mac_ptr;

    WiFi.persistent(0); // Don't save config to flash.
    WiFi.mode(WIFI_AP_STA);

    // Fill trailing spaces in tag with chars determined by the MAC address for a little uniqueness
    tag_p = APtag + sizeof APtag - 2;   // pointer to final char of APtag
    mac_ptr = mac + WL_MAC_ADDR_LENGTH - 1;
    WiFi.softAPmacAddress(mac);
    while (tag_p >= APtag && *tag_p == ' ' && mac_ptr >= mac)
    {
        *tag_p-- = 'A' + (*mac_ptr-- % 26);
    }
    WiFi.softAP(APtag, "thermoESP");   // NB. Password must be at least 8 characters
}


uint8_t connectWiFi()
{
    int ret = 0;
    DOPRINTLN("");
    if (WiFi.status() == WL_CONNECTED)
    {
        DOPRINT("Already connected to ");
        DOPRINTLN(p_ssid);
        return 0;
    }

    if (!p_passrot || !*p_passrot)
    {
        DOPRINT("No password when connecting to ");
        DOPRINTLN(p_ssid);
        return 1;
    }

    DOPRINT("Connecting to ");
    DOPRINTLN(p_ssid);

    WiFi.persistent(0); // Don't save config to flash.

    /* I'm not sure of the exact behaviour of, or relationship between, WiFi.begin and WiFi.status.
        It would seem to be obvious that you 'begin' and then get status until the status is OK,
        but when I do that:
            - I always get a timeout
            - but the WiFi actually seems to be working OK
        The following method, of occasionally repeating the begin while checking status, seems to work.
    */
    setLEDpulse(1, 100, 0, 400);
    for (int i=0; WiFi.status() != WL_CONNECTED && i < 5; ++i)
    {
        char *passclear;
        unsigned long end_time;
        DOPRINT(" attempt ");
        DOPRINTLN(i);
        setLED();
        end_time = millis() + 6000;
        passclear = unrot(p_passrot);
        DOPRINTLN(passclear);
        DOPRINTLN(strlen(passclear));
        WiFi.begin(p_ssid, passclear);
        DOPRINTLN("clear passclear");
        memset(passclear, 0, strlen(passclear)); // Don't leave the password lying about in memory.
        DOPRINTLN("free passclear");
        free(passclear);
        DOPRINTLN("null passclear");
        passclear = 0;
        DOPRINTLN("delay 100 awaiting WiFi status");
        delay(100);
        DOPRINT("WiFi.status() ");
        DOPRINTLN(WiFi.status());
        while ( (millis() < end_time) && WiFi.status() != WL_CONNECTED)
        {
            setLED();
            DOPRINT(".");
            delay(100);
        }
    }
    DOPRINTLN("");
    setLEDflashing(0, 0);
    if (WiFi.status() != WL_CONNECTED)
    {
        DOPRINTLN("");
        DOPRINTLN("Timed out.");
        return 1;
    }
    DOPRINT("WiFi connected: IP ");  
    DOPRINTLN(WiFi.localIP());
    return 0;
}

int connectTCP()
{

    if (!p_report_hostname || !*p_report_hostname)
    {
        DOPRINTLN("Not sending report. No server configured.");
        return 1;   // server not configured
    }
    DOPRINT("connecting to ");
    DOPRINT(p_report_hostname);
    DOPRINT(":");
    DOPRINTLN(persistent_data.port);

    // Use WiFiClient class to create TCP connections
    if (! client.connect(p_report_hostname, persistent_data.port))
    {
        DOPRINTLN("connection failed");
        return 1;
    }
    DOPRINTLN("TCP connected");
    return 0;
}

static enum {GET_STATUS, GET_HEADERS, GET_BODY, END_OF_RESPONSE} http_response_state;

static int content_length = -1;
static int received_length;
static uint8_t changed_something;

static void handleHeader(char *name, char *value)
{
    if (!strcmp(name, "Content-Length"))
    {
        content_length = atoi(value);
    }
    else if (!strcmp(name, "ETag"))
    {
        strdupWithFree(value, &new_etag);
    }
#ifdef COMMS_VERBOSE
    Serial.print("got header '");
    Serial.print(name);
    Serial.print("' : '");
    Serial.print(value);
    DOPRINTLN("'");
#endif
}

// returns 0 at end-of-response, 1 otherwise
static int processLine(char *buf, int term_len)
{
    switch(http_response_state)
    {
      case GET_STATUS:
        {
            char *proto, *stat, *comment;
            proto = buf;
            stat = strchr(buf, ' ');
            *(stat++) = '\0';
            comment = strchr(stat, ' ');
            *(comment++) = '\0';
            http_response_state = GET_HEADERS;
            content_length = -1;
        }
        break;
      case GET_HEADERS:
        // NB. Does not handle continuation lines
        if (*buf == '\0')
        {
#ifdef COMMS_VERBOSE
            Serial.println("End of headers");
#endif
            if (content_length == 0)
            {
#ifdef COMMS_VERBOSE
                Serial.println("End of empty response");
#endif
                return 0;
            }
            http_response_state = GET_BODY;
            received_length = 0;
            changed_something = 0;
        }
        else
        {
            char *value;
            value = strchr(buf, ':');
            if (value > 0)
            {
                *(value++) = '\0';
                while (*value && *value == ' ')
                {
                    value++;
                }
                handleHeader(buf, value);
            }
        }
        break;
      case GET_BODY:
        // each line is name=value
        {
            char *value_str;
            received_length += strlen(buf) + term_len;
            if ( (value_str = strchr(buf, '=')) > 0)
            {
                *(value_str++) = '\0';
#ifndef QUIET
                Serial.print(buf);
                Serial.print(" = '");
                Serial.print(value_str);
                Serial.println("'");
#endif
                if (setPersistentValue(buf, value_str))
                {
#ifndef QUIET
                    Serial.println("        changed");
#endif
                    changed_something = 1;
                    break;
                }
            }
        }
        if (content_length >= 0 && received_length >= content_length)
        {
#ifndef QUIET
            Serial.println("End of response");
#endif
            return 0;
        }
        break;
    }
    return 1;
}

void getResponse()
{
    unsigned long timeout;
    int read_len;
    int got_so_far;
    timeout = millis();
    while (!client.available())
    {
        setLED();
        if ( (millis() - timeout) > 5000)
        {
#ifndef QUIET
            Serial.println(">>> Client Timeout !");
#endif
            client.stop();
            return;
        }
    }
    http_response_state = GET_STATUS;
    content_length = 0;
    got_so_far = 0;
    while (client.available() && http_response_state != END_OF_RESPONSE)
    {
        char inbuf[80];
        char *p;
        setLED();
        read_len = client.read((uint8_t*)&inbuf[got_so_far], 80 - got_so_far - 1);
        if (read_len <= 0)
        {
#ifndef QUIET
            Serial.println("End of reading");
#endif
            if (inbuf[0])
            {
                processLine(inbuf, 0);
            }
            break;
        }
        got_so_far += read_len;
        inbuf[got_so_far] = '\0';
        if ( (p = strchr(inbuf, '\n')) > 0)
        {
            // at least one line
            int extra_data_len;
            int term_len;
            while ( (p = strchr(inbuf, '\n')) > 0)
            {
                *p = '\0';
                term_len = 1;
                if (p > inbuf && *(p-1) == '\r')
                {
                    term_len = 2;
                    *(p-1) = '\0';
                }
                if (!processLine(inbuf, term_len))
                {
                    return;
                }
                ++p;
                extra_data_len = got_so_far - (p - inbuf );
                if (extra_data_len <= 0)
                {
                    inbuf[0] = '\0';
                    got_so_far = 0;
                    break;
                }
                // need to move text after the \n to start of buffer
                memmove(inbuf, p, extra_data_len);
                inbuf[extra_data_len] = '\0';
                got_so_far = extra_data_len;
            }
        }
    }
    client.stop();
}

void sendReport(float current_temperature, int relay_state, const char *comment, SENSOR_DATA *sensor_data)
{
    int i;

    DOPRINTLN("sendReport");
    if (!connectTCP())
    {
        char *sanitized_txt = (char*)malloc(strlen(comment) + 1);
        const char *ps;
        char *pd;
        for (ps=comment, pd=sanitized_txt; *ps; ++ps, ++pd)
        {
            *pd = (*ps == ' ') ? '+' : *ps;
        }
        *pd = '\0';
        client.print("GET ");
        client.print(p_report_path);
        client.print("?ident=");
        client.print(p_identifier);
        client.print("&des=");
        client.print(persistent_data.desired_temperature);
        client.print("&tmp=");
        client.print(current_temperature);
        client.print("&relay=");
        client.print(relay_state ? "on" : "off");
        client.print("&txt=");
        client.print(sanitized_txt);
        for (i = 0; i < MAX_TEMPERATURE_SENSORS; ++i)
        {
            if (sensor_data->temperature[i].ok == ONEWIRE_OK)
            {
                char buf[20];
                client.print("&sensor_");
                client.print(formatAddr(buf, sensor_data->temperature[i].addr));
                client.print("=");
                client.print(sensor_data->temperature[i].temperature_c);
            }
        }
        free(sanitized_txt);
        client.print(" HTTP/1.0\r\n");  // 1.0 because we don't want to bother with 1.1 features like chunked transfer
        client.print("Host:");          // Send Host header even though it's optional in 1.0, because Nginx insists on it. Sigh...
        client.print(p_report_hostname);
        client.print("\r\n\r\n");
        getResponse();
    }
    setLEDflashing(0, 0);
}
