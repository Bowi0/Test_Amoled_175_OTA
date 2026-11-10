#ifndef FIRMWARE_VERSION_H
#define FIRMWARE_VERSION_H

#include <stddef.h>
#include <stdint.h>

static inline bool parse_firmware_version(const char *version, uint32_t parts[3])
{
    if(version == NULL || *version == '\0') return false;
    if(*version == 'v' || *version == 'V') version++;

    for(size_t index = 0; index < 3; index++) {
        if(*version < '0' || *version > '9') return false;
        uint32_t value = 0;
        while(*version >= '0' && *version <= '9') {
            const uint32_t digit = (uint32_t)(*version++ - '0');
            if(value > (UINT32_MAX - digit) / 10) return false;
            value = value * 10 + digit;
        }
        parts[index] = value;
        if(index < 2 && *version++ != '.') return false;
    }
    return *version == '\0';
}

static inline bool is_newer_firmware_version(const uint32_t available[3],
                                             const uint32_t current[3])
{
    for(size_t index = 0; index < 3; index++) {
        if(available[index] != current[index]) return available[index] > current[index];
    }
    return false;
}

#endif
