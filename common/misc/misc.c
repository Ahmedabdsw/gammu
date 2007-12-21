/* (c) 2002-2005 by Marcin Wiacek and Michal Cihar */
/* Checking used compiler (c) 2002 by Michal Cihar */

#include <gammu-config.h>
#include <gammu-misc.h>
#include "misc.h"

#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>
#include <stdio.h>
#include <locale.h>
#include <stdlib.h>
#include <sys/timeb.h>
#ifdef WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif
#ifdef HAVE_SYS_UTSNAME_H
#  include <sys/utsname.h>
#endif

/**
 * Recalculates struct tm content. We can not use mktime directly, as it
 * works only for dates > 1970. Day of week calculation is nased on article
 * in Polish PC-Kurier 8/1998 page 104.
 *
 * @see http://www.pckurier.pl
 */
int RecalcDateTime(struct tm *st, const int year, const int month, const int day, const int hour, const int minute, const int second)
{
	const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
	int i, p, q, r;
	GSM_DateTime Date;

	Date.Year	= year;
	Date.Month	= month;
	Date.Day	= day;
	Date.Hour	= hour;
	Date.Minute	= minute;
	Date.Second	= second;
	Date.Timezone	= 0;

	if (!CheckDate(&Date) || !CheckTime(&Date)) return 0;

	memset(st, 0, sizeof(*st));

	/* Calculate day of year */
	st->tm_yday = day;
	for (i = 0; i < month - 1; i++)
		st->tm_yday += days[i];

	/* Calculate day of week */
	p = (14 - month) / 12;
	q = month + 12 * p - 2;
	r = year - p;
	st->tm_wday = (day + (31 * q) / 12 + r + r / 4 - r / 100 + r / 400) % 7;


	st->tm_hour = hour;
	st->tm_min = minute;
	st->tm_sec = second;
	st->tm_year = year - 1900;
	st->tm_mon = month - 1;
	st->tm_mday = day;

	return 1;
}


/**
 * Recalculates struct tm content.
 */
int RecalcDate(struct tm *st, const int year, const int month, const int day)
{
	return RecalcDateTime(st, year, month, day, 0, 0, 0);
}


/**
 * Return day of year index.
 */
int GetDayOfYear(int year, int month, int day)
{
	struct tm st;

	RecalcDate(&st, year, month, day);

	return st.tm_yday;
}

/**
 * Return day of week index.
 */
int GetWeekOfMonth(int year, int month, int day)
{
	struct tm st;

	RecalcDate(&st, year, month, day);

	return 1 + (day - st.tm_wday) / 7;
}

/**
 * Return day of week index.
 */
int GetDayOfWeek(int year, int month, int day)
{
	struct tm st;

	RecalcDate(&st, year, month, day);

	return st.tm_wday;
}

/**
 * Return textual representation of day of week;
 */
char *DayOfWeek (int year, int month, int day)
{
	static char 	DayOfWeekChar[10];

	strcpy(DayOfWeekChar,"");
	switch (GetDayOfWeek(year, month, day)) {
		case 0: strcpy(DayOfWeekChar,"Sun"); break;
		case 1: strcpy(DayOfWeekChar,"Mon"); break;
		case 2: strcpy(DayOfWeekChar,"Tue"); break;
		case 3: strcpy(DayOfWeekChar,"Wed"); break;
		case 4: strcpy(DayOfWeekChar,"Thu"); break;
		case 5: strcpy(DayOfWeekChar,"Fri"); break;
		case 6: strcpy(DayOfWeekChar,"Sat"); break;
	}
	return DayOfWeekChar;
}

void Fill_GSM_DateTime(GSM_DateTime *Date, time_t timet)
{
	struct tm *now;

	now  		= localtime(&timet);
	Date->Year	= now->tm_year;
	Date->Month	= now->tm_mon+1;
	Date->Day	= now->tm_mday;
	Date->Hour	= now->tm_hour;
	Date->Minute	= now->tm_min;
	Date->Second	= now->tm_sec;
	Date->Timezone	= 0; /* FIXME: can we get it from the OS? */
}

void GSM_GetCurrentDateTime (GSM_DateTime *Date)
{
	Fill_GSM_DateTime(Date, time(NULL));
	if (Date->Year<1900) {
		if (Date->Year>90) Date->Year = Date->Year+1900;
			      else Date->Year = Date->Year+2000;
	}
}

time_t Fill_Time_T(GSM_DateTime DT)
{
	struct tm tm_starttime;

	dbgprintf("  StartTime  : %02i-%02i-%04i %02i:%02i:%02i\n",
		DT.Day,DT.Month,DT.Year,DT.Hour,DT.Minute,DT.Second);

	memset(&tm_starttime, 0, sizeof(tm_starttime));
	tm_starttime.tm_year 	= DT.Year - 1900;
	tm_starttime.tm_mon  	= DT.Month - 1;
	tm_starttime.tm_mday 	= DT.Day;
	tm_starttime.tm_hour 	= DT.Hour;
	tm_starttime.tm_min  	= DT.Minute;
	tm_starttime.tm_sec  	= DT.Second;
	tm_starttime.tm_isdst	= -1;

	return mktime(&tm_starttime);
}

GSM_DateTime GSM_AddTime (GSM_DateTime DT , GSM_DeltaTime delta)
{
	struct tm tm_time;
	time_t t_time;
	GSM_DateTime Date;

	memset(&tm_time, 0, sizeof(tm_time));
	tm_time.tm_year 	= DT.Year - 1900;
	tm_time.tm_mon  	= DT.Month - 1;
	tm_time.tm_mday 	= DT.Day;
	tm_time.tm_hour 	= DT.Hour;
	tm_time.tm_min  	= DT.Minute;
	tm_time.tm_sec  	= DT.Second;
	tm_time.tm_isdst	= -1;

	/* TODO: This works only for dates after 1970. But birthday dates may be before, so a more general
	   method than mktime /localtime should be used. */
	t_time = mktime (&tm_time);
	t_time = t_time + delta.Second + 60* (delta.Minute + 60* (delta.Hour + 24*delta.Day));

	Fill_GSM_DateTime ( &Date, t_time);
	Date.Year += 1900;
	return Date;
}

void GetTimeDifference(unsigned long diff, GSM_DateTime *DT, bool Plus, int multi)
{
	time_t t_time;

	t_time = Fill_Time_T(*DT);

	if (Plus) {
		t_time 		+= diff*multi;
	} else {
		t_time 		-= diff*multi;
	}

	Fill_GSM_DateTime(DT, t_time);
	DT->Year = DT->Year + 1900;
	dbgprintf("  EndTime    : %02i-%02i-%04i %02i:%02i:%02i\n",
		DT->Day,DT->Month,DT->Year,DT->Hour,DT->Minute,DT->Second);
}

char *OSDateTime (GSM_DateTime dt, bool TimeZone)
{
	struct tm 	timeptr;
	static char 	retval[200],retval2[200];

	if (!RecalcDateTime(&timeptr, dt.Year, dt.Month, dt.Day,
				dt.Hour, dt.Minute, dt.Second)) {
		retval2[0] = 0;
		return retval2;
	}

#ifdef WIN32
	setlocale(LC_ALL, ".OCP");
#endif

	/* This is not Y2K safe */
	strftime(retval2, 200, "%c", &timeptr);
	if (TimeZone) {
		if (dt.Timezone >= 0) {
			snprintf(retval, sizeof(retval) - 1, " +%02i00",dt.Timezone);
		} else {
			snprintf(retval, sizeof(retval) - 1, " -%02i00",dt.Timezone);
		}
		strcat(retval2,retval);
	}
	/* If don't have weekday name, include it */
	strftime(retval, 200, "%A", &timeptr);
	if (strstr(retval2,retval)==NULL) {
		/* Check for abbreviated weekday */
		strftime(retval, 200, "%a", &timeptr);
		if (strstr(retval2,retval)==NULL) {
			strcat(retval2," (");
			strcat(retval2,retval);
			strcat(retval2,")");
		}
	}

#ifdef WIN32
	setlocale(LC_ALL, ".ACP");
#endif

	return retval2;
}

char *OSDate (GSM_DateTime dt)
{
	struct tm 	timeptr;
	static char 	retval[200],retval2[200];

#ifdef WIN32
	setlocale(LC_ALL, ".OCP");
#endif

	timeptr.tm_yday 	= 0; 			/* FIXME */
	timeptr.tm_isdst 	= -1; 			/* FIXME */
	timeptr.tm_year 	= dt.Year - 1900;
	timeptr.tm_mon  	= dt.Month - 1;
	timeptr.tm_mday 	= dt.Day;
	timeptr.tm_hour 	= dt.Hour;
	timeptr.tm_min  	= dt.Minute;
	timeptr.tm_sec  	= dt.Second;
	timeptr.tm_wday 	= GetDayOfWeek(dt.Year, dt.Month, dt.Day);
#ifdef _BSD_SOURCE
	timeptr.tm_zone		= NULL;
#endif

	/* This is not Y2K safe */
	strftime(retval2, 200, "%x", &timeptr);

	/* If don't have weekday name, include it */
	strftime(retval, 200, "%A", &timeptr);
	if (strstr(retval2,retval)==NULL) {
		/* Check also for short name */
		strftime(retval, 200, "%a", &timeptr);
		if (strstr(retval2,retval)==NULL) {
            		strcat(retval2," (");
            		strcat(retval2,retval);
            		strcat(retval2,")");
            	}
	}

#ifdef WIN32
	setlocale(LC_ALL, ".ACP");
#endif

	return retval2;
}

bool CheckDate(GSM_DateTime *date)
{
	const unsigned int days[]={31,28,31,30,31,30,31,31,30,31,30,31};

	if (date->Year != 0 &&
	    ((date->Year % 4 == 0 && date->Year % 100 != 0) || date->Year % 400 == 0) &&
	    date->Month == 2) {
		return (date->Day <= 29);
	}
	return date->Year != 0 &&
	       date->Month >= 1 && date->Month <= 12 &&
	       date->Day >= 1 && date->Day <= days[date->Month-1];
}

bool CheckTime(GSM_DateTime *date)
{
	return date->Hour <= 23 &&
		date->Minute <= 59 &&
		date->Second <= 59;
}

int GetLine(FILE *File, char *Line, int count)
{
	int num;

	if (fgets(Line, count, File) != NULL) {
		num = strlen(Line) - 1;
		while (num > 0) {
			if (Line[num] != '\n' && Line[num] != '\r') break;
			Line[num--] = 0;
		}
		return strlen(Line);
	}
	return -1;
}

void InitLines(GSM_CutLines *lines)
{
	lines->numbers = NULL;
	lines->allocated = 0;
}

void FreeLines(GSM_CutLines *lines)
{
	free(lines->numbers);
	lines->numbers = NULL;
	lines->allocated = 0;
}

void SplitLines(const char *message, int messagesize, GSM_CutLines *lines, unsigned char *whitespaces, int spaceslen, bool eot)
{
	int 	i,number=0,j;
	bool 	whitespace=true, nowwhite;

	for (i = 0; i < lines->allocated; i++) 
		lines->numbers[i]=0;

	for (i = 0; i < messagesize; i++) {
		/* Reallocate buffer if needed */
		if (number >= lines->allocated - 1) {
			lines->allocated += 20;
			lines->numbers = realloc(lines->numbers, lines->allocated * sizeof(int));
			if (lines->numbers == NULL) return;
			for (i = lines->allocated - 20; i < lines->allocated; i++) 
				lines->numbers[i] = 0;
		}

		nowwhite = false;
		for (j = 0; j < spaceslen; j++) {
			if (whitespaces[j] == message[i]) {
				nowwhite = true;
				break;
			}
		}
		if (whitespace) {
			if (!nowwhite) {
				lines->numbers[number]=i;
				number++;
				whitespace=false;
			}
		} else {
			if (nowwhite) {
				lines->numbers[number]=i;
				number++;
				whitespace=true;
			}

		}
	}
    	if (eot && !whitespace) 
		lines->numbers[number] = messagesize;
}

char *GetLineString(const char *message, const GSM_CutLines *lines, int start)
{
	static char *retval = NULL;
	int len;

	len = GetLineLength(message, lines, start);
	retval = realloc(retval, len + 1);
	if (retval == NULL) {
		dbgprintf("Allocation failed!\n");
		return NULL;
	}

	memcpy(retval, message + lines->numbers[start * 2 - 2], len);

	retval[len] = 0;

	return retval;
}

int GetLineLength(const char *message UNUSED, const GSM_CutLines *lines, int start)
{
	return lines->numbers[start*2-2+1] - lines->numbers[start*2-2];
}

void CopyLineString(char *dest, const char *src, const GSM_CutLines *lines, int start)
{
	int len = GetLineLength(src, lines, start);
	memcpy(dest, GetLineString(src, lines, start), len);
	dest[len] = 0;
}

GSM_Debug_Info di = {0,NULL,false,"",false};

#ifdef DEBUG
PRINTF_STYLE(1, 2)
int dbgprintf(const char *format, ...)
{
	va_list			argp;
	int 			result;
	static unsigned char 	nextline[2000]="";
	unsigned char		buffer[2000];
	GSM_DateTime 		date_time;

	va_start(argp, format);
	result = vsnprintf(buffer, sizeof(buffer) - 1, format, argp);
	va_end(argp);

	if (di.df != NULL && (di.dl == DL_TEXTALL || di.dl == DL_TEXTALLDATE)) {
		strcat(nextline, buffer);

		if (strstr(buffer, "\n") != NULL) {
			if (di.dl == DL_TEXTALLDATE) {
				GSM_GetCurrentDateTime(&date_time);
				fprintf(di.df,"%s %4d/%02d/%02d %02d:%02d:%02d: %s",
		                	DayOfWeek(date_time.Year, date_time.Month, date_time.Day),
		                	date_time.Year, date_time.Month, date_time.Day,
		                	date_time.Hour, date_time.Minute, date_time.Second,nextline);
			} else {
				fprintf(di.df, "%s", nextline);
			}
			strcpy(nextline, "");
		}

		return result;
	}
	return 0;
}
#endif

PRINTF_STYLE(2, 3)
int smfprintf(GSM_Debug_Info *d, const char *format, ...)
{
        va_list 		argp;
	int 			result=0;
	char			buffer[3000];
	char			*pos, *end;
	char			save = 0;
	GSM_DateTime 		date_time;
	FILE			*f;
	Debug_Level		l;

	if (d->use_global) {
		f = di.df;
		l = di.dl;
	} else {
		f = d->df;
		l = d->dl;
	}

	if (l == DL_NONE || f == NULL) return 0;

	va_start(argp, format);
	result = vsnprintf(buffer, sizeof(buffer) - 1, format, argp);
	va_end(argp);
	pos = buffer;

	while (*pos != 0) {

		/* Find new line in string */
		end = strstr(pos, "\n");

		/* Are we at start of line? */
		if (d->was_lf) {
			/* Show date? */
			if (l == DL_TEXTALLDATE || l == DL_TEXTERRORDATE || l == DL_TEXTDATE) {
				GSM_GetCurrentDateTime(&date_time);
		                fprintf(f,"%s %4d/%02d/%02d %02d:%02d:%02d: ",
		                        DayOfWeek(date_time.Year, date_time.Month, date_time.Day),
		                        date_time.Year, date_time.Month, date_time.Day,
		                        date_time.Hour, date_time.Minute, date_time.Second);
			}
			d->was_lf = false;
		}

		/* Remember end char */
		if (end != NULL) {
			save = *end;
			*end = 0;
		}

		/* Output */
		fprintf(f, "%s", pos);

		if (end != NULL) {
			/* We had new line */
			fprintf(f, "\n");
			d->was_lf = true;

			/* Restore saved char */
			*end = save;

			/* Advance to next line */
			pos = end + strlen("\n");
		} else {
			/* We hit end of string */
			break;
		}
	}

	/* Flush buffers, this might be configurable, but it could cause drop of last log messages */
	fflush(f);

	return result;
}

bool GSM_SetDebugLevel(const char *info, GSM_Debug_Info *privdi)
{
	if (info == NULL)			{privdi->dl = DL_NONE;	 	return true;}
	if (!strcmp(info,"nothing")) 		{privdi->dl = DL_NONE;	 	return true;}
	if (!strcmp(info,"text")) 		{privdi->dl = DL_TEXT;	 	return true;}
	if (!strcmp(info,"textall")) 		{privdi->dl = DL_TEXTALL;    	return true;}
	if (!strcmp(info,"binary"))  		{privdi->dl = DL_BINARY;     	return true;}
	if (!strcmp(info,"errors"))  		{privdi->dl = DL_TEXTERROR;  	return true;}
	if (!strcmp(info,"textdate")) 		{privdi->dl = DL_TEXTDATE;	 	return true;}
	if (!strcmp(info,"textalldate")) 	{privdi->dl = DL_TEXTALLDATE;    	return true;}
	if (!strcmp(info,"errorsdate"))  	{privdi->dl = DL_TEXTERRORDATE;  	return true;}
	return false;
}

bool GSM_SetDebugCoding(const char *info, GSM_Debug_Info *privdi)
{
	privdi->coding = info;
	return true;
}

bool GSM_SetDebugGlobal(bool info, GSM_Debug_Info *privdi)
{
	if (privdi->use_global && info) {
		GSM_SetDebugFileDescriptor(NULL, privdi);
	}
	privdi->use_global = info;
	return true;
}

/* Dumps a message */
void DumpMessage(GSM_Debug_Info *d, const unsigned char *message, int messagesize)
{
	int 		i,j=0,len=16;
	unsigned char	buffer[200];

	if (d->df == NULL || messagesize == 0) return;

	smfprintf(d, "\n");

	memset(buffer,0x20,sizeof(buffer));
	buffer[len*5-1]=0;

	for (i = 0; i < messagesize; i++) {
		snprintf(buffer+j*4, sizeof(buffer) - j*4 - 1, "%02X",message[i]);
		buffer[j*4+2] = 0x20;
		/* 9 = tab */
		if (isprint(message[i]) && message[i]!=0x09
			/* 0x01 = beep in windows xp */
			&& message[i]!=0x01
			/* these are empty in windows xp */
			&& message[i]!=0x85 && message[i]!=0x95
  			&& message[i]!=0xA6 && message[i]!=0xB7) {
			if (j != len-1) buffer[j*4+2] 	= message[i];
			buffer[(len-1)*4+j+3]		= message[i];
		} else {
			buffer[(len-1)*4+j+3]		= '.';
		}
		if (j != len-1 && i != messagesize-1) buffer[j*4+3] = '|';
		if (j == len-1) {
			smfprintf(d, "%s\n", buffer);
			memset(buffer,0x20,sizeof(buffer));
			buffer[len*5-1]=0;
			j = 0;
		} else {
			j++;
		}
	}
	if (j != 0) smfprintf(d, "%s\n", buffer);
}

const char *GetOS(void)
{
#ifdef WIN32
	OSVERSIONINFOEX Ver;
	bool		Extended = true;
#endif
#ifdef HAVE_SYS_UTSNAME_H
	struct utsname	Ver;
#endif
	static char 	Buffer[100] = {0x00};

#ifdef WIN32
	memset(&Ver,0,sizeof(OSVERSIONINFOEX));
	Ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);

   	if (!GetVersionEx((OSVERSIONINFO *)&Ver)) {
		Extended 		= false;
	      	Ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	        if (!GetVersionEx((OSVERSIONINFO *)&Ver)) {
			snprintf(Buffer, sizeof(Buffer) - 1, "Windows");
			return Buffer;
		}
	}

	/* ----------------- 9x family ------------------ */

	/* no info about Win95 SP1, Win95 OSR2.1, Win95 OSR2.5.... */
	if (Ver.dwMajorVersion == 4 && Ver.dwMinorVersion == 0 && Ver.dwBuildNumber == 950) {
		snprintf(Buffer, sizeof(Buffer) - 1, "Windows 95");
	} else if (Ver.dwMajorVersion == 4 && Ver.dwMinorVersion == 0 && Ver.dwBuildNumber == 1111) {
		snprintf(Buffer, sizeof(Buffer) - 1, "Windows 95 OSR2.x");

	/* no info about Win98 SP1.... */
	} else if (Ver.dwMajorVersion == 4 && Ver.dwMinorVersion == 10 && Ver.dwBuildNumber == 1998) {
		snprintf(Buffer, sizeof(Buffer) - 1, "Windows 98");
	} else if (Ver.dwMajorVersion == 4 && Ver.dwMinorVersion == 10 && Ver.dwBuildNumber == 2222) {
		snprintf(Buffer, sizeof(Buffer) - 1, "Windows 98 SE");

	} else if (Ver.dwMajorVersion == 4 && Ver.dwMinorVersion == 90 && Ver.dwBuildNumber == 3000) {
		snprintf(Buffer, sizeof(Buffer) - 1, "Windows ME");

	/* ---------------- NT family ------------------- */

	} else if (Ver.dwMajorVersion == 4 && Ver.dwMinorVersion == 0 && Ver.dwBuildNumber == 1381) {
		snprintf(Buffer, sizeof(Buffer) - 1, "Windows NT 4.0");

	} else if (Ver.dwMajorVersion == 5 && Ver.dwMinorVersion == 0 && Ver.dwBuildNumber == 2195) {
		snprintf(Buffer, sizeof(Buffer) - 1, "Windows 2000");

	} else if (Ver.dwMajorVersion == 5 && Ver.dwMinorVersion == 1 && Ver.dwBuildNumber == 2600) {
		snprintf(Buffer, sizeof(Buffer) - 1, "Windows XP");
#if _MSC_VER > 1200 /* 6.0 has it undeclared */
		if (Extended) {
			if (Ver.wSuiteMask & VER_SUITE_PERSONAL) {
				snprintf(Buffer+strlen(Buffer), sizeof(Buffer) - 1 - strlen(Buffer)," Home");
			} else {
				snprintf(Buffer+strlen(Buffer), sizeof(Buffer) - 1 - strlen(Buffer)," Pro");
			}
		}
#endif

	} else if (Ver.dwMajorVersion == 5 && Ver.dwMinorVersion == 2) {
		snprintf(Buffer, sizeof(Buffer) - 1, "Windows 2003");

	} else if (Ver.dwMajorVersion == 6 && Ver.dwMinorVersion == 0) {
		snprintf(Buffer, sizeof(Buffer) - 1, "Windows Vista");

	} else if (Ver.dwMajorVersion == 6 && Ver.dwMinorVersion > 0) {
		snprintf(Buffer, sizeof(Buffer) - 1, "Windows Server 2007");

	} else {
		snprintf(Buffer, sizeof(Buffer) - 1, "Windows %i.%i.%i",(int)Ver.dwMajorVersion,(int)Ver.dwMinorVersion,(int)Ver.dwBuildNumber);
	}

	if (Extended && Ver.wServicePackMajor != 0) {
		snprintf(Buffer+strlen(Buffer), sizeof(Buffer) - 1 - strlen(Buffer)," SP%i",Ver.wServicePackMajor);
	}
#elif defined(HAVE_SYS_UTSNAME_H)
	uname(&Ver);
	snprintf(Buffer, sizeof(Buffer) - 1, "%s, kernel %s (%s)", Ver.sysname, Ver.release, Ver.version);
#elif defined(__FreeBSD__)
	snprintf(Buffer, sizeof(Buffer) - 1, "FreeBSD");
#elif defined(__NetBSD__)
	snprintf(Buffer, sizeof(Buffer) - 1, "NetBSD");
#elif defined(__OpenBSD__)
	snprintf(Buffer, sizeof(Buffer) - 1, "OpenBSD");
#elif defined(__GNU__)
	snprintf(Buffer, sizeof(Buffer) - 1, "GNU/Hurd");
#elif defined(sun) || defined(__sun) || defined(__sun__)
#  ifdef __SVR4
	snprintf(Buffer, sizeof(Buffer) - 1, "Sun Solaris");
#  else
	snprintf(Buffer, sizeof(Buffer) - 1, "SunOS");
#  endif
#elif defined(hpux) || defined(__hpux) || defined(__hpux__)
	snprintf(Buffer, sizeof(Buffer) - 1, "HP-UX");
#elif defined(ultrix) || defined(__ultrix) || defined(__ultrix__)
	snprintf(Buffer, sizeof(Buffer) - 1, "DEC Ultrix");
#elif defined(sgi) || defined(__sgi)
	snprintf(Buffer, sizeof(Buffer) - 1, "SGI Irix");
#elif defined(__osf__)
	snprintf(Buffer, sizeof(Buffer) - 1, "OSF Unix");
#elif defined(bsdi) || defined(__bsdi__)
	snprintf(Buffer, sizeof(Buffer) - 1, "BSDI Unix");
#elif defined(_AIX)
	snprintf(Buffer, sizeof(Buffer) - 1, "AIX Unix");
#elif defined(_UNIXWARE)
	snprintf(Buffer, sizeof(Buffer) - 1, "SCO Unixware");
#elif defined(DGUX)
	snprintf(Buffer, sizeof(Buffer) - 1, "DG Unix");
#elif defined(__QNX__)
	snprintf(Buffer, sizeof(Buffer) - 1, "QNX");
#endif
	return Buffer;
}

const char *GetCompiler(void)
{
	static char Buffer[100] = {0x00};

#ifdef WIN32
#  ifdef _MSC_VER
	if (_MSC_VER == 1200) { /* ? */
		snprintf(Buffer, sizeof(Buffer) - 1, "MS VC 6.0");
	} else if (_MSC_VER == 1300) {
		snprintf(Buffer, sizeof(Buffer) - 1, "MS VC .NET 2002");
	} else if (_MSC_VER == 1310) {
		snprintf(Buffer, sizeof(Buffer) - 1, "MS VC .NET 2003");
	} else if (_MSC_VER == 1400) {
		snprintf(Buffer, sizeof(Buffer) - 1, "MS VC .NET 2005");
	} else {
		snprintf(Buffer, sizeof(Buffer) - 1, "MS VC %i",_MSC_VER);
	}
#  elif defined(__BORLANDC__)
	snprintf(Buffer, sizeof(Buffer) - 1, "Borland C++ %i",__BORLANDC__);
#  elif defined(__MINGW32__)
	snprintf(Buffer, sizeof(Buffer) - 1, "GCC %i.%i, MinGW %i.%i", __GNUC__, __GNUC_MINOR__, __MINGW32_MAJOR_VERSION, __MINGW32_MINOR_VERSION);
#  elif defined(__GNUC__)
	snprintf(Buffer, sizeof(Buffer) - 1, "GCC %i.%i", __GNUC__, __GNUC_MINOR__);
#  else
	snprintf(Buffer, sizeof(Buffer) - 1, "unknown compiler");
#  endif
#elif defined(DJGPP)
	snprintf(Buffer, sizeof(Buffer) - 1, "djgpp %d.%d", __DJGPP, __DJGPP_MINOR);
#elif defined(__GNUC__)
	snprintf(Buffer, sizeof(Buffer) - 1, "GCC %i.%i", __GNUC__, __GNUC_MINOR__);
#elif defined(__SUNPRO_CC)
	snprintf(Buffer, sizeof(Buffer) - 1, "Sun C++ %x", __SUNPRO_CC);
#elif defined(__INTEL_COMPILER)
	snprintf(Buffer, sizeof(Buffer) - 1, "Intel Compiler %ld", __INTEL_COMPILER);
#else
	snprintf(Buffer, sizeof(Buffer) - 1, "unknown compiler");
#endif

	return Buffer;
}

bool GSM_IsNewerVersion(const char *latest_version, const char *current_version)
{
	size_t i;
	size_t len = strlen(latest_version);

	for (i = 0; i < len ; i++) {
		if (latest_version[i] > current_version[i]) {
			return true;
		}
	}

	return false;
}

/* How should editor hadle tabs in this file? Add editor commands here.
 * vim: noexpandtab sw=8 ts=8 sts=8:
 */
