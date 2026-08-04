#include <solis/rtc.h>
#include <solis/ports.h>

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

#define RTC_SECONDS    0x00
#define RTC_MINUTES    0x02
#define RTC_HOURS      0x04
#define RTC_DAY        0x07
#define RTC_MONTH      0x08
#define RTC_YEAR       0x09
#define RTC_STATUS_A   0x0A
#define RTC_STATUS_B   0x0B
#define RTC_CENTURY    0x32

#define RTC_UPDATE_IN_PROGRESS 0x80
#define RTC_BINARY_MODE        0x04

struct tz_entry {
    const char *name;
    int offset_min;
};

static const struct tz_entry tz_table[RTC_MAX_TIMEZONES] = {
    {"UTC",            0},
    {"Los Angeles", -480},
    {"Denver",      -420},
    {"Chicago",     -360},
    {"New York",    -300},
    {"London",         0},
    {"Paris",        +60},
    {"Berlin",       +60},
    {"Moscow",      +180},
    {"Dubai",       +240},
    {"Mumbai",      +330},
    {"Beijing",     +480},
    {"Tokyo",       +540},
    {"Sydney",      +600},
};

static int current_tz = 0;

static inline uint8_t rtc_read_cmos(uint8_t reg) {
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

static void rtc_wait_update(void) {
    while (rtc_read_cmos(RTC_STATUS_A) & RTC_UPDATE_IN_PROGRESS) { }
}

static int bcd_to_bin(uint8_t value) {
    return (value & 0x0F) + ((value >> 4) * 10);
}

static int days_in_month(int month, int year) {
    static const int dim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
        return 29;
    return dim[month - 1];
}

static int day_of_week(int year, int month, int day) {
    if (month < 3) {
        month += 12;
        year -= 1;
    }
    int k = year % 100;
    int j = year / 100;
    int h = (day + 13 * (month + 1) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    return (h + 6) % 7;
}

void rtc_init(void) {
    current_tz = 0;
}

void rtc_get_time(struct rtc_time *out) {
    if (!out) return;

    rtc_wait_update();
    uint8_t sec      = rtc_read_cmos(RTC_SECONDS);
    uint8_t min      = rtc_read_cmos(RTC_MINUTES);
    uint8_t hour     = rtc_read_cmos(RTC_HOURS);
    uint8_t day      = rtc_read_cmos(RTC_DAY);
    uint8_t mon      = rtc_read_cmos(RTC_MONTH);
    uint8_t year     = rtc_read_cmos(RTC_YEAR);
    uint8_t century  = rtc_read_cmos(RTC_CENTURY);
    uint8_t status_b = rtc_read_cmos(RTC_STATUS_B);

    if (status_b & RTC_BINARY_MODE) {
        out->second = sec;
        out->minute = min;
        out->hour = hour;
        out->day = day;
        out->month = mon;
        out->year = (century >= 19 && century <= 21) ? century * 100 + year : 2000 + year;
    } else {
        out->second = bcd_to_bin(sec);
        out->minute = bcd_to_bin(min);
        out->hour = bcd_to_bin(hour);
        out->day = bcd_to_bin(day);
        out->month = bcd_to_bin(mon);
        out->year = (century >= 19 && century <= 21)
                        ? bcd_to_bin(century) * 100 + bcd_to_bin(year)
                        : 2000 + bcd_to_bin(year);
    }

    if ((status_b & 0x02) == 0) {
        int is_pm = 0;
        if (out->hour & 0x80) {
            out->hour &= 0x7F;
            is_pm = 1;
        }
        if (out->hour == 0) out->hour = 12;
        if (is_pm && out->hour < 12) out->hour += 12;
        if (!is_pm && out->hour == 12) out->hour = 0;
    }

    int offset = (current_tz >= 0 && current_tz < RTC_MAX_TIMEZONES)
                     ? tz_table[current_tz].offset_min
                     : 0;

    int minutes = out->hour * 60 + out->minute + offset;
    while (minutes < 0) {
        minutes += 1440;
        out->day -= 1;
    }
    while (minutes >= 1440) {
        minutes -= 1440;
        out->day += 1;
    }
    out->hour = minutes / 60;
    out->minute = minutes % 60;

    while (out->day < 1) {
        out->month -= 1;
        if (out->month < 1) {
            out->month = 12;
            out->year -= 1;
        }
        out->day += days_in_month(out->month, out->year);
    }
    while (out->day > days_in_month(out->month, out->year)) {
        out->day -= days_in_month(out->month, out->year);
        out->month += 1;
        if (out->month > 12) {
            out->month = 1;
            out->year += 1;
        }
    }

    out->weekday = day_of_week(out->year, out->month, out->day);

    out->tz_offset = offset;
    out->tz_index = current_tz;
}

void rtc_set_timezone(int index) {
    if (index >= 0 && index < RTC_MAX_TIMEZONES)
        current_tz = index;
}

int rtc_get_timezone(void) {
    return current_tz;
}

int rtc_get_timezone_count(void) {
    return RTC_MAX_TIMEZONES;
}

void rtc_get_timezone_name(int index, char *buf, int max_len) {
    if (!buf || max_len <= 0) return;
    if (index < 0 || index >= RTC_MAX_TIMEZONES) {
        buf[0] = '\0';
        return;
    }
    const char *src = tz_table[index].name;
    int i;
    for (i = 0; i < max_len - 1 && src[i]; i++)
        buf[i] = src[i];
    buf[i] = '\0';
}

int rtc_get_timezone_offset(int index) {
    if (index < 0 || index >= RTC_MAX_TIMEZONES) return 0;
    return tz_table[index].offset_min;
}
