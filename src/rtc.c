#include "rtc.h"

#include <stdint.h>

#define CMOS_ADDRESS 0x70
#define CMOS_DATA 0x71

static uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static uint8_t cmos_read(uint8_t rtc_register)
{
    outb(CMOS_ADDRESS, (uint8_t)(rtc_register & 0x7fu));
    return inb(CMOS_DATA);
}

static uint8_t bcd_to_binary(uint8_t value)
{
    return (uint8_t)(((value >> 4) & 0x0f) * 10u +
                     (value & 0x0f));
}

int rtc_read(struct rtc_datetime *datetime)
{
    if (datetime == 0) {
        return 0;
    }
    for (uint32_t attempt = 0; attempt < 100000u; ++attempt) {
        if ((cmos_read(0x0a) & 0x80u) == 0) {
            break;
        }
        __asm__ volatile ("pause");
    }
    for (uint32_t attempt = 0; attempt < 100000u; ++attempt) {
        if ((cmos_read(0x0a) & 0x80u) == 0) {
            break;
        }
        __asm__ volatile ("pause");
    }

    uint8_t status_b = cmos_read(0x0b);
    uint8_t second = cmos_read(0x00);
    uint8_t minute = cmos_read(0x02);
    uint8_t hour = cmos_read(0x04);
    uint8_t day = cmos_read(0x07);
    uint8_t month = cmos_read(0x08);
    uint8_t year = cmos_read(0x09);
    uint8_t century = cmos_read(0x32);

    int binary = (status_b & 0x04u) != 0;
    if (!binary) {
        second = bcd_to_binary(second);
        minute = bcd_to_binary(minute);
        hour = bcd_to_binary(hour & 0x7fu) |
               (hour & 0x80u);
        day = bcd_to_binary(day);
        month = bcd_to_binary(month);
        year = bcd_to_binary(year);
        century = century == 0 ? 0 : bcd_to_binary(century);
    }

    if ((status_b & 0x02u) == 0) {
        if ((hour & 0x80u) != 0) {
            hour &= 0x7fu;
            hour = (uint8_t)((hour + 12u) % 24u);
        }
    }

    uint16_t full_year;
    if (century != 0) {
        full_year = (uint16_t)(century * 100u + year);
    } else {
        full_year = (uint16_t)(year >= 70 ? 1900u + year : 2000u + year);
    }

    datetime->year = full_year;
    datetime->month = month;
    datetime->day = day;
    datetime->hour = hour;
    datetime->minute = minute;
    datetime->second = second;
    return 1;
}
