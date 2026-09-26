#ifndef SOLIS_RTC_H
#define SOLIS_RTC_H

#include <solis/types.h>

#define RTC_MAX_TIMEZONES 14

/* weekday: 0 = Sunday, 1 = Monday, ... 6 = Saturday.
   tz_offset: minutes east of UTC for the selected timezone.
   The OS treats the CMOS RTC as UTC and shifts by the timezone offset. */
struct rtc_time {
    int second;
    int minute;
    int hour;
    int day;
    int month;
    int year;
    int weekday;
    int tz_offset;
    int tz_index;
};
void rtc_init(void);
void rtc_load_settings(void);
void rtc_get_time(struct rtc_time *out);
void rtc_set_timezone(int index);
int  rtc_get_timezone(void);
int  rtc_get_timezone_count(void);
void rtc_get_timezone_name(int index, char *buf, int max_len);
int  rtc_get_timezone_offset(int index);

#endif
