#include "Modem.h"

#include <cstring>
#include <cstdio>

void Modem::init(URCReceiver &receiver) {
    this->receiver = &receiver;
}

const char* Modem::lastResponse() {
    return respbuffer;
}

void Modem::startSet(const char* cmd) {
    strcpy(cmdbuffer, cmd);
    valoffset = valbuffer;
}

void Modem::appendSet(int value) {
    valoffset += sprintf(valoffset, "%d", value);
}

void Modem::appendSet(const char* value) {
    valoffset += sprintf(valoffset, "%s", value);
}

void Modem::appendSet(char value) {
    *valoffset++ = value;
}

void Modem::appendSet(uint8_t *value, uint32_t len) {
    for(int i=0; i<len; i++)
        *valoffset++ = (char)value[i];
}

modem_result Modem::intermediateSet(char expected, uint32_t timeout, uint32_t retries) {
    do {
        respbuffer[0] = 0;
        modemwrite(cmdbuffer, CMD_STARTAT);
        modemwrite("=");
        modemwrite(valbuffer, CMD_END);
        uint32_t startMillis = msTick();
        while (msTick() - startMillis < timeout) {
            while(modemavailable()) {
                char c = modempeek();
                debugout("<");
                if(c == '\r') {
                    debugout("\\r");
                } else if(c == '\n') {
                    debugout("\\n");
                } else if(c < 0x21 || c > 0x7E)
                    debugout((int)c);
                else
                    debugout(c);
                debugout(">\r\n");

                if(c == expected) {
                    modemread();
                    return MODEM_OK;
                } else if(c == '+') {
                    checkURC();
                } else {
                    modemread();
                }
            }
        }
    }while(retries--);
    return MODEM_TIMEOUT;
}

modem_result Modem::waitSetComplete(uint32_t timeout, uint32_t retries)
{
    return waitSetComplete(NULL, timeout, retries);
}

modem_result Modem::waitSetComplete(const char* expected, uint32_t timeout, uint32_t retries) {
    modem_result r = MODEM_TIMEOUT;
    do {
        respbuffer[0] = 0;
        modem_result r = processResponse(timeout, cmdbuffer);
        if(r == MODEM_OK) {
            if(expected) {
                if(strcmp(expected, respbuffer) == 0) {
                    return MODEM_OK;
                } else {
                    return MODEM_NO_MATCH;
                }
            } else {
                return MODEM_OK;
            }
        }
    }while(retries--);
    return r;
}

modem_result Modem::completeSet(uint32_t timeout, uint32_t retries) {
    *valoffset = 0;
    set(cmdbuffer, valbuffer, timeout, retries);
}

modem_result Modem::completeSet(const char* expected, uint32_t timeout, uint32_t retries) {
    *valoffset = 0;
    set(cmdbuffer, valbuffer, expected, timeout, retries);
}

bool Modem::findline(char *buffer, uint32_t timeout, uint32_t startMillis) {
    char *rx = buffer;
    while (msTick() - startMillis < timeout) {
        while(modemavailable()) {
            *rx = modemread();
            if(*rx == '\n') {
                while(*rx == '\n' || *rx == '\r')
                    *rx-- = 0;
                debugout("{");
                debugout(buffer);
                debugout("}\r\n");
                return true;
            } else {
                rx++;
            }
        }
    }
    *rx = 0;
    debugout("{");
    debugout(buffer);
    debugout("}!\r\n");
    return false;
}

void Modem::modemwrite(const char* cmd, cmd_flags flags) {
    if(flags & CMD_START) {
        debugout("[");
    }
    if(flags & CMD_AT) {
        modemout("AT");
    }
    modemout(cmd);
    if(flags & CMD_QUERY) {
        modemout("?");
    }
    if(flags & CMD_END) {
        debugout("]");
        modemout("\r\n");
    }
}

void Modem::checkURC() {
    while(modemavailable()) {
        uint32_t startMillis = msTick();
        if(findline(okbuffer, 1000, startMillis)) {
            if(okbuffer[0] == '+') {
                debugout("!URC: '");
                debugout(okbuffer);
                debugout("'\r\n");
                if(receiver)
                    receiver->onURC(okbuffer);
            }
        }
    }
}

int Modem::strncmpci(const char* str1, const char* str2, size_t num) {
    for(int i=0; i<num; i++) {
        char c1 = str1[i];
        char c2 = str2[i];
        if(c1 >= 'a' && c1 <= 'z') c1 -= 32;
        if(c2 >= 'a' && c2 <= 'z') c2 -= 32;
        if(c1 == c2) {
            if(c1 == 0)
                return 0;
        } else {
            return c1 - c2;
        }
    }
    return 0;
}

modem_result Modem::processResponse(uint32_t timeout, const char* cmd) {
    uint32_t startMillis = msTick();
    while(findline(okbuffer, timeout, startMillis)) {
        if(strcmp(okbuffer, "ERROR") == 0) {
            return MODEM_ERROR;
        } else if(strncmp(okbuffer, "+CME ERROR:", 11) == 0) {
            strcpy(respbuffer, okbuffer);
            return MODEM_ERROR;
        } else if(strcmp(okbuffer, "OK") == 0) {
            return MODEM_OK;
        } else if(okbuffer[0] == '+' && strncmpci(cmd, okbuffer, strlen(cmd)) != 0) {
            debugout(">URC: '");
            debugout(okbuffer);
            debugout("'\r\n");
            if(receiver)
                receiver->onURC(okbuffer);
            startMillis = msTick();
        } else if(strncmp(okbuffer, "AT", 2) == 0 && strncmp(&okbuffer[2], cmd, strlen(cmd)) == 0) {
            debugout(">ECHO: '");
            debugout(okbuffer);
            debugout("'\r\n");
        } else if(strlen(okbuffer) > 0) {
            strcpy(respbuffer, okbuffer);
        }
    }
    return MODEM_TIMEOUT;
}

modem_result Modem::command(const char* cmd, const char* expected, uint32_t timeout, uint32_t retries, bool query) {
    modem_result r = MODEM_TIMEOUT;
    do {
        respbuffer[0] = 0;
        modemwrite(cmd, query ? CMD_FULL_QUERY : CMD_FULL);
        modem_result r = processResponse(timeout, cmd);
        if(r == MODEM_OK) {
            if(expected) {
                if(strcmp(expected, respbuffer) == 0) {
                    return MODEM_OK;
                } else {
                    return MODEM_NO_MATCH;
                }
            } else {
                return MODEM_OK;
            }
        }
    }while(retries--);
    return r;
}

modem_result Modem::command(const char* cmd, uint32_t timeout, uint32_t retries, bool query) {
    return command(cmd, NULL, timeout, retries, query);
}

modem_result Modem::set(const char* cmd, const char* value, const char* expected, uint32_t timeout, uint32_t retries) {
    modem_result r = MODEM_TIMEOUT;
    do {
        respbuffer[0] = 0;
        modemwrite(cmd, CMD_STARTAT);
        modemwrite("=");
        modemwrite(value, CMD_END);
        r = processResponse(timeout, cmd);
        if(r == MODEM_OK) {
            if(expected) {
                if(strcmp(expected, respbuffer) == 0) {
                    debugout(">set match: '");
                    debugout(expected);
                    debugout("' '");
                    debugout(respbuffer);
                    debugout("'\r\n");
                    return MODEM_OK;
                } else {
                    return MODEM_NO_MATCH;
                }
            } else {
                return MODEM_OK;
            }
        }
    }while(retries--);
    return r;
}

modem_result Modem::set(const char* cmd, const char* value, uint32_t timeout, uint32_t retries) {
    return set(cmd, value, NULL, timeout, retries);
}

void Modem::rawWrite(char c) {
    modemout(c);
}

void Modem::rawWrite(const char* content) {
    modemwrite(content);
}

void Modem::dataWrite(uint8_t b) {
    modemout(b);
}

void Modem::dataWrite(const uint8_t* content, uint32_t length) {
    for(uint32_t i=0; i<length; i++)
        modemout(content[i]);
}

void Modem::rawRead(int length, void* buffer) {
    uint32_t startMillis = msTick();
    uint32_t timeout = 30000;
    uint8_t* pbuffer = (uint8_t*)buffer;
    int read = 0;
    while(read < length && msTick() - startMillis < timeout) {
        if(modemavailable()) {
            if(buffer)
                pbuffer[read++] = modemread();
            else
                modemread();
        }
    }
    if(buffer)
        pbuffer[read] = 0;
}
