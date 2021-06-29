/* Licensed under GNU General Public License v3.0
  See https://github.com/jeffasuk/thermostat/blob/master/LICENSE
  jeff at jamcupboard.co.uk
*/
#include <Arduino.h>
#include <EEPROM.h>
#include "globals.h"

/* The following macro caters for all the various int types in persistent data.
    Although using a macro doesn't reduce code size, it does make modifications/bug fixes easier.
*/
#define SHOW_SIMPLE_VALUE(VAR_TYPE) \
{ \
    VAR_TYPE newval; \
    VAR_TYPE *current_value = (VAR_TYPE*)p_settable->value; \
    DOPRINTLN(*((VAR_TYPE*)(p_settable->value))); \
}

uint8_t eepromIsUninitialized()
{
    char in;
    int i;
    int ret = 0;
    EEPROM.begin(512);
    DOPRINTLN("Checking magic tag");
    for (i=0; i < sizeof magic_tag; ++i)
    {
        in = EEPROM.read(i);
        if (in != magic_tag[i])
        {
            DOPRINT("Found difference on byte ");
            DOPRINTLN(i);
            ret = i+1;
            break;
        }
    }
    EEPROM.end();
    return ret;
}

static void writeMagicTag()
{
    char in;
    int i;
    EEPROM.begin(512);
    DOPRINTLN("Writing magic tag");
    for (i=0; i < sizeof magic_tag; ++i)
    {
        DOPRINTLN(magic_tag[i]);
        EEPROM.write(i, magic_tag[i]);
    }
    EEPROM.end();
    DOPRINTLN("Done writing magic tag");
    //delay(500);   The ESP with relay does not like a delay, but doesn't seem to need it anyway, or maybe 'cos I'm threading
}

static void readWriteEeprom(int dowrite)
{
    uint8_t *p;
    int c, l;
    PERSISTENT_STRING_INFO *pers_str_ptr;

    DOPRINTLN("Start read/write EEPROM");
    DOPRINTLN(dowrite);
    l = sizeof persistent_data;
    EEPROM.begin(512);
    for (p = (uint8_t*)&persistent_data, l = sizeof persistent_data, c=sizeof magic_tag; l; c++, l--)
    {
        if (dowrite)
        {
            char buf[10];
            DOPRINT("EEP write ");
            DOPRINT(c);
            DOPRINT(" ");
            sprintf(buf, " %x", *p & 0xff);
            DOPRINT(buf);
            DOPRINTLN("");
            EEPROM.write(c, *p++);
        }
        else
        {
            *p++ = EEPROM.read(c);
        }
    }
    // That's the easy bit; the struct. Now for the variable-length strings.
    // In the EEPROM, use Pascal-style (length,value) strings for ease of memory allocation.
    for (pers_str_ptr = persistent_strings; pers_str_ptr->name; ++pers_str_ptr)
    {
        uint16_t string_len;
        char    *p;
        DOPRINT("EEP field ");
        DOPRINTLN((char*)(pers_str_ptr->name));
        if (dowrite)
        {
            if (*(pers_str_ptr->value))
            {
                string_len = strlen(*(pers_str_ptr->value));
                DOPRINTLN((char*)(pers_str_ptr->value));
                EEPROM.write(c++, string_len & 0xff);
                EEPROM.write(c++, (string_len >> 8) & 0xff);
                for (p = *(pers_str_ptr->value); *p; ++p)
                {
                    EEPROM.write(c++, *p);
                }
            }
            else
            {
                // 16 bits of zero for item with no value
                DOPRINTLN("  empty");
                EEPROM.write(c++, 0);
                EEPROM.write(c++, 0);
            }
        }
        else
        {
            string_len = (EEPROM.read(c++) & 0xff) + ((EEPROM.read(c++) << 8) & 0xff00);
            if (string_len > 200)
            {
                DOPRINT("String too long: ");
                DOPRINT(pers_str_ptr->name);
                DOPRINT(", ");
                DOPRINTLN(string_len);
                break;
            }
            if (*(pers_str_ptr->value))
            {
                free(*(pers_str_ptr->value));
            }
            if (string_len)
            {
                p = *(pers_str_ptr->value) = (char*)malloc(string_len+1);
                while (string_len--)
                {
                    *p++ = EEPROM.read(c++);
                }
                *p = '\0';
            }
            else
            {
                *(pers_str_ptr->value) = 0;
            }
        }
    }
    EEPROM.end();
    if (dowrite)
    {
        //delay(100);   The ESP with relay does not like a delay, but doesn't seem to need it anyway, or maybe 'cos I'm threading
    }
}
void readFromEeprom()
{
    readWriteEeprom(0);
}
void writeToEeprom()
{
    writeMagicTag();
    DOPRINTLN("Returned from writing magic tag");
    readWriteEeprom(1);
}

void showSettings()
{
    PERSISTENT_INFO *p_settable;
    PERSISTENT_STRING_INFO *pers_str_ptr;
    // simple values
    for (p_settable = persistents; p_settable->name; ++p_settable)
    {
        DOPRINT("  ");
        DOPRINT(p_settable->name);
        DOPRINT("=");
        switch (p_settable->type)
        {
          case PERS_INT8:
            SHOW_SIMPLE_VALUE(int8_t);
            break;
          case PERS_UINT8:
            SHOW_SIMPLE_VALUE(uint8_t);
            break;
          case PERS_INT16:
            SHOW_SIMPLE_VALUE(int16_t);
            break;
          case PERS_UINT16:
            SHOW_SIMPLE_VALUE(uint16_t);
            break;
          case PERS_INT32:
            SHOW_SIMPLE_VALUE(int32_t);
            break;
          case PERS_UINT32:
            SHOW_SIMPLE_VALUE(uint32_t);
            break;
          case PERS_FLOAT:
            SHOW_SIMPLE_VALUE(float);
            break;
          case PERS_STR:
            {
                // not sure how to do this w.r.t. writing to EEPROM
            }
            break;
        }
    }
    // string values
    for (pers_str_ptr = persistent_strings; pers_str_ptr->name; ++pers_str_ptr)
    {
        DOPRINT("  ");
        DOPRINT(pers_str_ptr->name);
        DOPRINT("=");
        if (*(pers_str_ptr->value))
        {
            DOPRINT("'");
            DOPRINT(*(pers_str_ptr->value));
            DOPRINT("'");
        }
        else
        {
            DOPRINT("empty");
        }
        DOPRINTLN("");
    }
}
