/* Licensed under GNU General Public License v3.0
  See https://github.com/jeffasuk/thermostat
  jeff at jamcupboard.co.uk
*/

#include <Arduino.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebSrv.h>

#include "globals.h"
#include "home_html.h"
#include "eepromutils.h"
#include "network.h"
#include "persistence.h"
#include "utils.h"

// enable debug printing in this module
//#define DEBUGDOPRINT   DOPRINT
//#define DEBUGDOPRINTLN DOPRINTLN

// disable debug printing in this module
#define DEBUGDOPRINT(x)
#define DEBUGDOPRINTLN(x)

static const char page_head[] =
        "<html>\n"
        "<head><title>ESP Thermostatic Controller</title></head>\n"
        "<body>\n";
static const char page_tail[] = "</body></html>\n";

static char settings_form_page[] = \
        "<form method='POST' action='/setup' >\n"
        "%%EXTRA%%"
        "<table>\n"
        "<tr><td>WiFi SSID</td><td><input type=\"text\" name=\"ssid\" width=\"30\" value=\"%%SSID%%\"></td></tr>\n"
        //"<tr><td>WiFi password</td><td><input type=\"password\" name=\"pswd\" width=\"30\"></td></tr>\n"
        "<tr><td>WiFi password (visible)</td><td><input type=\"text\" name=\"pswd\" width=\"30\"></td></tr>\n"
        "<tr><td>Unit identifier</td><td><input type=\"text\" name=\"ident\" width=\"30\" value=\"%%IDENT%%\"></td></tr>\n"
        "<tr><td>Server hostname</td><td><input type=\"text\" name=\"host\" width=\"30\" value=\"%%HOST%%\"></td></tr>\n"
        "<tr><td>Server port</td><td><input type=\"text\" name=\"port\" width=\"30\" value=\"%%PORT%%\"></td></tr>\n"
        "<tr><td>Settings path</td><td><input type=\"text\" name=\"cpath\" width=\"30\" value=\"%%CFGPATH%%\"></td></tr>\n"
        "<tr><td>Report path</td><td><input type=\"text\" name=\"rpath\" width=\"30\" value=\"%%RPTPATH%%\"></td></tr>\n"
        "</table>\n"
        "<input type=\"checkbox\" name=\"normalmode\" />Switch to normal mode\n"
        "<br><input type=\"submit\" value=\"Send\">\n"
        "</form>\n"
        ;

static AsyncWebServer *server;

static void sendMainPage(AsyncWebServerRequest *request)
{
    AsyncWebHeader  *ifnonematch_header;
    DEBUGDOPRINTLN("Web request for sendMainPage");
    if ( (ifnonematch_header = request->getHeader("if-none-match")) && ifnonematch_header->value() == String(home_etag))
    {
        // unchanged, so don't need to send content
        DEBUGDOPRINTLN("Unchanged. Send 304");
        request->send(304);
    }
    else
    {
        AsyncWebServerResponse * response = request->beginResponse(200, "text/html", home_html);
        response->addHeader("Etag", home_etag);
        request->send(response);
    }
}

static void sendStatus(AsyncWebServerRequest *request)
{
    int sensor_index;
    char addr_buf[17];
    DEBUGDOPRINTLN("Web request for sendStatus");
    String response = String("<status>\n");
    for (sensor_index = 0; sensor_index < sensor_data.nb_temperature_sensors; ++ sensor_index)
    {
        response += String(" <tmp id=\"") +
            String(formatAddr(addr_buf, sensor_data.temperature[sensor_index].addr)) +
            String("\">") +
            String(sensor_data.temperature[sensor_index].temperature_c) +
            String("</tmp>\n");
    }
    response += String(" <state>") + String(relay_state) + String("</state>\n") +
            String(" <des>")   + String(persistent_data.desired_temperature) + String("</des>\n") +
            String(" <prec>")   + String(persistent_data.precision) + String("</prec>\n") +
            String(" <switchoffsetabove>")   + String(switch_offset_above) + String("</switchoffsetabove>\n") +
            String(" <switchoffsetbelow>")   + String(switch_offset_below) + String("</switchoffsetbelow>\n") +
            String(" <mode>")   + String(persistent_data.mode == HEATING ? "heating" : "cooling") + String("</mode>\n") +
            String(" <maxrep>")   + String(persistent_data.max_time_between_reports) + String("</maxrep>\n") +
        String("</status>\n");
    DEBUGDOPRINTLN(response);
    request->send(200, "text/xml", response);
}

static int checkAndSetPersistentFloatValue(const char *name, const char *val_str, float *target)
{
    if (!val_str || !*val_str)
    {
        // empty, don't set anything
        return 0;
    }
    float new_temp = String(val_str).toFloat();
    if (new_temp != *target)
    {
        DOPRINT  ("Setting ");
        DOPRINT  (name);
        DOPRINT  (" to ");
        DOPRINTLN(new_temp);
        *target = new_temp;
        return 1;
    }
    return 0;
}

static int checkAndSetPersistentUint8Value(const char *name, const char *val_str, uint8_t *target)
{
    if (!val_str || !*val_str)
    {
        // empty, don't set anything
        return 0;
    }
    uint8_t new_val = String(val_str).toInt();
    if (new_val != *target)
    {
        DOPRINT  ("Setting ");
        DOPRINT  (name);
        DOPRINT  (" to ");
        DOPRINTLN(new_val);
        *target = new_val;
        return 1;
    }
    return 0;
}

static int checkAndSetPersistentUint32Value(const char *name, const char *val_str, uint32_t *target)
{
    if (!val_str || !*val_str)
    {
        // empty, don't set anything
        return 0;
    }
    uint32_t new_temp = String(val_str).toInt();
    if (new_temp != *target)
    {
        DOPRINT  ("Setting ");
        DOPRINT  (name);
        DOPRINT  (" to ");
        DOPRINTLN(new_temp);
        *target = new_temp;
        return 1;
    }
    return 0;
}

static void settings(AsyncWebServerRequest *request)
{
    int nb_params = request->params();
    int made_a_change = 0;
    DOPRINTLN("Web request for settings");
    for (int i = 0; i < nb_params; i++)
    {
        AsyncWebParameter* p = request->getParam(i);
        if (p->name() == "des_temp")
        {
            made_a_change |= checkAndSetPersistentFloatValue("des_temp", p->value().c_str(), &persistent_data.desired_temperature);
        }
        else if (p->name() == "precision")
        {
            made_a_change |= checkAndSetPersistentFloatValue("precision", p->value().c_str(), &persistent_data.precision);
        }
        else if (p->name() == "maxreporttime")
        {
            made_a_change |= checkAndSetPersistentUint32Value("maxreporttime", p->value().c_str(), &persistent_data.max_time_between_reports);
        }
        else if (p->name() == "mode")
        {
            made_a_change |= checkAndSetPersistentUint8Value("mode", p->value().c_str(), &persistent_data.mode);
        }
    } 
    if (made_a_change)
    {
        DOPRINTLN("Writing settings to EEPROM");
        writeToEeprom();
    }
    sendStatus(request);
}

static int getNameIndex(const char *name)
{
    int i;
    for (i = 0; name_mapping[i].html_field_name; ++i)
    {
        if (!strcmp(name, name_mapping[i].html_field_name))
        {
            return i;
        }
    }
    return -1;
}

static void processSetupPath(AsyncWebServerRequest *request)
{
    char    *value;
    char    name[200];
    uint8_t changed_something = 0;
    uint8_t switch_to_normal_mode = 0;
    String post_extra_response;
    DOPRINTLN("Web request for processSetupPath");
    // We'll end up sending the form page, but for POST, it will have extra stuff in it to indicate
    // how the changes went.
    if (request->methodToString() == "POST")
    {
        int nb_params = request->params();
        DOPRINT(nb_params);
        DOPRINTLN(" params");
        for (int i = 0; i < nb_params; i++)
        {
            AsyncWebParameter* p = request->getParam(i);
            int item_index;
            DOPRINT("arg: ");
            DOPRINT(i);
            DOPRINT(": ");
            DOPRINTLN(p->name());
            p->name().toCharArray(name, 199);
            DOPRINTLN(name);
            // parse input. Assumes application/x-www-form-urlencoded
            if (p->name() == String("normalmode"))
            {
                switch_to_normal_mode = 1;
                continue;
            }
            if ( (item_index = getNameIndex(name)) >= 0)
            {
                // found it
                char *item_name = name_mapping[item_index].persistent_item_name;
                String value;
                DOPRINT("found at index ");
                DOPRINTLN(item_index);
                value = p->value();
                DOPRINTLN("got value");
                DOPRINTLN(value);
                if (!strncmp(item_name, "rot", 3))
                {
                    // It's some sort of password to be obfuscated for storage
                    int charindex;
                    if (value.length() == 0)
                    {
                        // Issue #1
                        // This will not have been filled in in the form, so don't do anything with it if it's empty.
                        // Otherwise, the password (or whatever) will be blanked if the user has not re-entered the correct value.
                        // (Yes, I did that myself before this check went in. Very annoying!)
                        continue;
                    }
                    for (charindex=0; charindex < value.length(); ++charindex)
                    {
                        char c = value.charAt(charindex);
                        value.setCharAt(charindex, (char)(((((unsigned int)c << 8) + (unsigned int)c) >> persistent_data.rot ) & 0xFF));
                    }
                }
                if (setPersistentValue(item_name, value.c_str()))
                {
                    changed_something = 1;
                }
            }
            else
            {
                DOPRINTLN("not found");
            }
        }
        if (changed_something)
        {
            // Now check settings. Return appropriate page
            // If OK, write to EEPROM, and then enter normal running.
            uint8_t res;
            DOPRINTLN("Writing settings to EEPROM");
            writeToEeprom();
            post_extra_response += "<p><b>Settings saved to EEPROM.</b>\n";
            DOPRINTLN("Check connection to WiFi");
            if ( (res = connectWiFi()) != 0)
            {
                // failed
                DOPRINTLN("Failed to connect to WiFi");
                post_extra_response += "<p><b>Failed to connect to WiFi '";
                post_extra_response += "'</b>\n<br>Please check settings</p>\n";
            }
            else if ( (res = connectTCP()) != 0)
            {
                // failed
                DOPRINTLN("Failed to connect to web server");
                post_extra_response += "<p><b>Failed to connect to web server '";
                post_extra_response += "'</b>\n<br>Please check settings</p>\n";
            }
            else
            {
                post_extra_response += "<br>Connected successfully to WiFi and web server\n";
                if (switch_to_normal_mode)
                {
                    post_extra_response += "<br>Switching to monitoring mode.\n<br>This website is now unavailable.</p>\n";
                    in_setup_mode = 0;
                }
            }
        }
    }
    String page = settings_form_page;
    page.replace("%%EXTRA%%", post_extra_response);
    page.replace("%%SSID%%", String(p_ssid ? p_ssid : ""));
    page.replace("%%IDENT%%", String(p_report_hostname ? p_identifier : ""));
    page.replace("%%HOST%%", String(p_report_hostname ? p_report_hostname : ""));
    page.replace("%%PORT%%", String(persistent_data.port));
    page.replace("%%CFGPATH%%", String(p_cfg_path ? p_cfg_path : ""));
    page.replace("%%RPTPATH%%", String(p_report_path ? p_report_path : ""));
    request->send(200, "text/html", page_head + page + page_tail);
}

void startAsyncWebServer()
{
    server = new AsyncWebServer(80);
    server->on("/",             HTTP_GET,   [](AsyncWebServerRequest *request) { sendMainPage(request); });
    server->on("/status",       HTTP_GET,   [](AsyncWebServerRequest *request) { sendStatus(request); });
    server->on("/settings",     HTTP_GET,   [](AsyncWebServerRequest *request) { settings(request); });
    server->on("/setup",        HTTP_GET | HTTP_POST,   [](AsyncWebServerRequest *request) { processSetupPath(request); });
    server->begin();
}
