/*
 * Bacula time and date routines -- John Walker
 *
 *   Version $Id$
 */
/*
   Copyright (C) 2000-2003 Kern Sibbald and John Walker

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of
   the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with this program; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
   MA 02111-1307, USA.

 */


/* Concerning times. There are a number of differnt time standards
 * in Bacula (fdate_t, ftime_t, time_t (Unix standard), btime_t, and
 *  utime_t).  fdate_t and ftime_t are deprecated and should no longer
 *  be used, and in general, Unix time time_t should no longer be used,
 *  it is being phased out. 
 *     
 *  Epoch is the base of Unix time in seconds (time_t, ...) 
 *     and is 1 Jan 1970 at 0:0
 *
 *  The major two times that should be left are:
 *     btime_t	(64 bit integer in microseconds base Epoch)
 *     utime_t	(64 bit integer in seconds base Epoch)
 */

#include "bacula.h"
#include <math.h>

/* Formatted time for user display: dd-Mon-yyyy hh:mm */
void bstrftime(char *dt, int maxlen, utime_t tim)
{
   time_t ttime = tim;
   struct tm tm;
   
   /* ***FIXME**** the format and localtime_r() should be user configurable */
   localtime_r(&ttime, &tm);
   strftime(dt, maxlen, "%d-%b-%Y %H:%M", &tm);
}

/* Unix time to standard time string yyyy-mm-dd hh:mm:ss */
char *bstrutime(char *dt, int maxlen, utime_t tim)
{
   time_t ttime = tim;
   struct tm tm;
   localtime_r(&ttime, &tm);
   strftime(dt, maxlen, "%Y-%m-%d %H:%M:%S", &tm);
   return dt;
}

/* Convert standard time string yyyy-mm-dd hh:mm:ss to Unix time */
utime_t str_to_utime(char *str) 
{
   struct tm tm;
   time_t ttime;

   if (sscanf(str, "%d-%d-%d %d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
					&tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) {
      return 0;
   }
   if (tm.tm_mon > 0) {
      tm.tm_mon--;
   } else { 
      return 0;
   }
   if (tm.tm_year >= 1900) {
      tm.tm_year -= 1900;
   } else {
      return 0;
   }
   tm.tm_wday = tm.tm_yday = 0;
   tm.tm_isdst = -1;
   ttime = mktime(&tm);
   if (ttime == -1) {	    
      ttime = 0;
   }
   return (utime_t)ttime;
}

/* Deprecated. Do not use. */
void get_current_time(struct date_time *dt)
{
   struct tm tm;
   time_t now;

   now = time(NULL);
   gmtime_r(&now, &tm);
   Dmsg6(200, "m=%d d=%d y=%d h=%d m=%d s=%d\n", tm.tm_mon+1, tm.tm_mday, tm.tm_year+1900,
      tm.tm_hour, tm.tm_min, tm.tm_sec);
   tm_encode(dt, &tm);
#ifdef DEBUG
   Dmsg2(200, "jday=%f jmin=%f\n", dt->julian_day_number, dt->julian_day_fraction);
   tm_decode(dt, &tm);
   Dmsg6(200, "m=%d d=%d y=%d h=%d m=%d s=%d\n", tm.tm_mon+1, tm.tm_mday, tm.tm_year+1900,
      tm.tm_hour, tm.tm_min, tm.tm_sec);
#endif
}

/*
 * Bacula's time (btime_t) is an unsigned 64 bit integer that contains
 *   the number of microseconds since Epoch Time (1 Jan 1970).
 */

btime_t get_current_btime()
{
   struct timeval tv;
   if (gettimeofday(&tv, NULL) != 0) {
      tv.tv_sec = (long)time(NULL);   /* fall back to old method */
      tv.tv_usec = 0;
   }
   return ((btime_t)tv.tv_sec) * 1000000 + (btime_t)tv.tv_usec;
}

/* Convert btime to Unix time */
time_t btime_to_unix(btime_t bt)
{
   return (time_t)(bt/1000000); 		   
}

/* Convert btime to utime */
utime_t btime_to_utime(btime_t bt)
{
   return (utime_t)bt;
}



/*  date_encode  --  Encode civil date as a Julian day number.	*/

/* Deprecated. Do not use. */
fdate_t date_encode(uint32_t year, uint8_t month, uint8_t day)
{

    /* Algorithm as given in Meeus, Astronomical Algorithms, Chapter 7, page 61 */

    int32_t a, b, m;
    uint32_t y;

    ASSERT(month < 13);
    ASSERT(day > 0 && day < 32);

    m = month;
    y = year;

    if (m <= 2) {
	y--;
	m += 12;
    }

    /* Determine whether date is in Julian or Gregorian calendar based on
       canonical date of calendar reform. */

    if ((year < 1582) || ((year == 1582) && ((month < 9) || (month == 9 && day < 5)))) {
	b = 0;
    } else {
	a = ((int) (y / 100));
	b = 2 - a + (a / 4);
    }

    return (((int32_t) (365.25 * (y + 4716))) + ((int) (30.6001 * (m + 1))) +
		day + b - 1524.5);
}

/*  time_encode  --  Encode time from hours, minutes, and seconds
		     into a fraction of a day.	*/

/* Deprecated. Do not use. */
ftime_t time_encode(uint8_t hour, uint8_t minute, uint8_t second,
		   float32_t second_fraction)
{
    ASSERT((second_fraction >= 0.0) || (second_fraction < 1.0));
    return (ftime_t) (((second + 60L * (minute + 60L * hour)) / 86400.0)) +
		     second_fraction;
}

/*  date_time_encode  --  Set day number and fraction from date
			  and time.  */

/* Deprecated. Do not use. */
void date_time_encode(struct date_time *dt,
		      uint32_t year, uint8_t month, uint8_t day,
		      uint8_t hour, uint8_t minute, uint8_t second,
		      float32_t second_fraction)
{
    dt->julian_day_number = date_encode(year, month, day);
    dt->julian_day_fraction = time_encode(hour, minute, second, second_fraction);
}

/*  date_decode  --  Decode a Julian day number into civil date.  */

/* Deprecated. Do not use. */
void date_decode(fdate_t date, uint32_t *year, uint8_t *month,
		 uint8_t *day)
{
    fdate_t z, f, a, alpha, b, c, d, e;

    date += 0.5;
    z = floor(date);
    f = date - z;

    if (z < 2299161.0) {
	a = z;
    } else {
	alpha = floor((z - 1867216.25) / 36524.25);
	a = z + 1 + alpha - floor(alpha / 4);
    }

    b = a + 1524;
    c = floor((b - 122.1) / 365.25);
    d = floor(365.25 * c);
    e = floor((b - d) / 30.6001);

    *day = (uint8_t) (b - d - floor(30.6001 * e) + f);
    *month = (uint8_t) ((e < 14) ? (e - 1) : (e - 13));
    *year = (uint32_t) ((*month > 2) ? (c - 4716) : (c - 4715));
}

/*  time_decode  --  Decode a day fraction into civil time.  */

/* Deprecated. Do not use. */
void time_decode(ftime_t time, uint8_t *hour, uint8_t *minute,
		 uint8_t *second, float32_t *second_fraction)
{
    uint32_t ij;

    ij = (uint32_t) ((time - floor(time)) * 86400.0);
    *hour = (uint8_t) (ij / 3600L);
    *minute = (uint8_t) ((ij / 60L) % 60L);
    *second = (uint8_t) (ij % 60L);
    if (second_fraction != NULL) {
	*second_fraction = time - floor(time);
    }
}

/*  date_time_decode  --  Decode a Julian day and day fraction
			  into civil date and time.  */

/* Deprecated. Do not use. */
void date_time_decode(struct date_time *dt,
		      uint32_t *year, uint8_t *month, uint8_t *day,
		      uint8_t *hour, uint8_t *minute, uint8_t *second,
		      float32_t *second_fraction)
{
    date_decode(dt->julian_day_number, year, month, day);
    time_decode(dt->julian_day_fraction, hour, minute, second, second_fraction);
}

/*  tm_encode  --  Encode a civil date and time from a tm structure   
 *		   to a Julian day and day fraction.
 */

/* Deprecated. Do not use. */
void tm_encode(struct date_time *dt,
		      struct tm *tm) 
{
    uint32_t year;
    uint8_t month, day, hour, minute, second;

    year = tm->tm_year + 1900;
    month = tm->tm_mon + 1;
    day = tm->tm_mday;
    hour = tm->tm_hour;
    minute = tm->tm_min;
    second = tm->tm_sec;
    dt->julian_day_number = date_encode(year, month, day);
    dt->julian_day_fraction = time_encode(hour, minute, second, 0.0);
}


/*  tm_decode  --  Decode a Julian day and day fraction
		   into civil date and time in tm structure */

/* Deprecated. Do not use. */
void tm_decode(struct date_time *dt,
		      struct tm *tm) 
{
    uint32_t year;
    uint8_t month, day, hour, minute, second;

    date_decode(dt->julian_day_number, &year, &month, &day);
    time_decode(dt->julian_day_fraction, &hour, &minute, &second, NULL);
    tm->tm_year = year - 1900;
    tm->tm_mon = month - 1;
    tm->tm_mday = day;
    tm->tm_hour = hour;
    tm->tm_min = minute;
    tm->tm_sec = second;
}


/*  date_time_compare  --  Compare two dates and times and return
			   the relationship as follows:

				    -1	  dt1 < dt2
				     0	  dt1 = dt2
				     1	  dt1 > dt2
*/

/* Deprecated. Do not use. */
int date_time_compare(struct date_time *dt1, struct date_time *dt2)
{
    if (dt1->julian_day_number == dt2->julian_day_number) {
	if (dt1->julian_day_fraction == dt2->julian_day_fraction) {
	    return 0;
	}
	return (dt1->julian_day_fraction < dt2->julian_day_fraction) ? -1 : 1;
    }
    return (dt1->julian_day_number - dt2->julian_day_number) ? -1 : 1;
}
