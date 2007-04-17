/* (c) 2002-2004 by Marcin Wiacek, 2005-2007 by Michal Cihar */

/**
 * @file gsmcal.c
 * @author Michal Čihař <michal@cihar.com>
 * @author Marcin Wiacek
 * @date 2002-2007
 */
/**
 * \addtogroup Calendar
 * @{
 */
#define _GNU_SOURCE
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "gsmcal.h"
#include "gsmmisc.h"
#include "../misc/coding/coding.h"

void GSM_SetCalendarRecurranceRepeat(unsigned char *rec, unsigned char *endday, GSM_CalendarEntry *entry)
{
	int		i,start=-1,frequency=-1,dow=-1,day=-1,month=-1,end=-1,Recurrance = 0, Repeat=0;
	char 		Buf[20];
	GSM_DateTime	DT;
	time_t		t_time1,t_time2;

	rec[0] = 0;
	rec[1] = 0;

	for (i=0;i<entry->EntriesNum;i++) {
		if (entry->Entries[i].EntryType == CAL_START_DATETIME)   start 		= i;
		if (entry->Entries[i].EntryType == CAL_REPEAT_FREQUENCY) frequency 	= i;
		if (entry->Entries[i].EntryType == CAL_REPEAT_DAYOFWEEK) dow 		= i;
		if (entry->Entries[i].EntryType == CAL_REPEAT_DAY)       day 		= i;
		if (entry->Entries[i].EntryType == CAL_REPEAT_MONTH)     month 		= i;
		if (entry->Entries[i].EntryType == CAL_REPEAT_STOPDATE)  end 		= i;
	}
	if (start == -1) return;

	if (frequency != -1 && dow == -1 && day == -1 && month == -1) {
		if (entry->Entries[frequency].Number == 1) {
			//each day
			Recurrance = 24;
		}
	}

	sprintf(Buf,"%s",DayOfWeek(entry->Entries[start].Date.Year,
			entry->Entries[start].Date.Month,
			entry->Entries[start].Date.Day));
	if (!strcmp(Buf,"Mon")) i = 1;
	if (!strcmp(Buf,"Tue")) i = 2;
	if (!strcmp(Buf,"Wed")) i = 3;
	if (!strcmp(Buf,"Thu")) i = 4;
	if (!strcmp(Buf,"Fri")) i = 5;
	if (!strcmp(Buf,"Sat")) i = 6;
	if (!strcmp(Buf,"Sun")) i = 7;

	if (frequency != -1 && dow != -1 && day == -1 && month == -1) {
		if (entry->Entries[frequency].Number == 1 &&
		    entry->Entries[dow].Number == i) {
			//one week
			Recurrance = 24*7;
		}
	}
	if (frequency != -1 && dow != -1 && day == -1 && month == -1) {
		if (entry->Entries[frequency].Number == 2 &&
		    entry->Entries[dow].Number == i) {
			//two weeks
			Recurrance = 24*14;
		}
	}
	if (frequency != -1 && dow == -1 && day != -1 && month == -1) {
		if (entry->Entries[frequency].Number == 1 &&
		    entry->Entries[day].Number == entry->Entries[start].Date.Day) {
			//month
			Recurrance = 0xffff-1;
		}
	}
	if (frequency != -1 && dow == -1 && day != -1 && month != -1) {
		if (entry->Entries[frequency].Number == 1 &&
		    entry->Entries[day].Number == entry->Entries[start].Date.Day &&
		    entry->Entries[month].Number == entry->Entries[start].Date.Month) {
			//year
			Recurrance = 0xffff;
		}
	}

	rec[0] = Recurrance / 256;
	rec[1] = Recurrance % 256;

	if (endday == NULL) return;

	endday[0] = 0;
	endday[1] = 0;

	if (end == -1) return;

	t_time1 = Fill_Time_T(entry->Entries[start].Date);
	t_time2 = Fill_Time_T(entry->Entries[end].Date);
	if (t_time2 - t_time1 <= 0) return;

	switch (Recurrance) {
		case 24:
		case 24*7:
		case 24*14:
			Repeat = (t_time2 - t_time1) / (60*60*Recurrance) + 1;
			break;
		case 0xffff-1:
			memcpy(&DT,&entry->Entries[start].Date,sizeof(GSM_DateTime));
			while (1) {
				if ((DT.Year == entry->Entries[end].Date.Year && DT.Month > entry->Entries[end].Date.Month) ||
				    (DT.Year >  entry->Entries[end].Date.Year)) break;
				if (DT.Month == 12) {
					DT.Month = 1;
					DT.Year++;
				} else {
					DT.Month++;
				}
				Repeat++;
			}
			break;
		case 0xffff:
			Repeat = entry->Entries[end].Date.Year-entry->Entries[start].Date.Year+1;
			break;
	}

	endday[0] = Repeat/256;
	endday[1] = Repeat%256;

	dbgprintf("Repeat number: %i\n",Repeat);
}

void GSM_GetCalendarRecurranceRepeat(unsigned char *rec, unsigned char *endday, GSM_CalendarEntry *entry)
{
	int 	Recurrance,num=-1,i;
	char 	Buf[20];

	Recurrance = rec[0]*256 + rec[1];
	if (Recurrance == 0) return;
	/* dct3 and dct4: 65535 (0xffff) is 1 year */
	if (Recurrance == 0xffff) Recurrance=24*365;
	/* dct3: unavailable, dct4: 65534 (0xffff-1) is 30 days */
	if (Recurrance == 0xffff-1) Recurrance=24*30;
	dbgprintf("Recurrance   : %i hours\n",Recurrance);

	for (i=0;i<entry->EntriesNum;i++) {
		if (entry->Entries[i].EntryType == CAL_START_DATETIME) {
			num = i;
			break;
		}
	}
	if (num == -1) return;
	sprintf(Buf,"%s",DayOfWeek(entry->Entries[num].Date.Year,
			entry->Entries[num].Date.Month,
			entry->Entries[num].Date.Day));

	if (Recurrance == 24    || Recurrance == 24*7 ||
	    Recurrance == 24*30 || Recurrance == 24*365) {
		entry->Entries[entry->EntriesNum].EntryType	= CAL_REPEAT_FREQUENCY;
		entry->Entries[entry->EntriesNum].Number	= 1;
		entry->EntriesNum++;
	}
	if (Recurrance == 24*14) {
		entry->Entries[entry->EntriesNum].EntryType	= CAL_REPEAT_FREQUENCY;
		entry->Entries[entry->EntriesNum].Number	= 2;
		entry->EntriesNum++;
	}
	if (Recurrance == 24*7 || Recurrance == 24*14) {
		entry->Entries[entry->EntriesNum].EntryType	 = CAL_REPEAT_DAYOFWEEK;
		if (!strcmp(Buf,"Mon")) {
			entry->Entries[entry->EntriesNum].Number = 1;
		} else if (!strcmp(Buf,"Tue")) {
			entry->Entries[entry->EntriesNum].Number = 2;
		} else if (!strcmp(Buf,"Wed")) {
			entry->Entries[entry->EntriesNum].Number = 3;
		} else if (!strcmp(Buf,"Thu")) {
			entry->Entries[entry->EntriesNum].Number = 4;
		} else if (!strcmp(Buf,"Fri")) {
			entry->Entries[entry->EntriesNum].Number = 5;
		} else if (!strcmp(Buf,"Sat")) {
			entry->Entries[entry->EntriesNum].Number = 6;
		} else if (!strcmp(Buf,"Sun")) {
			entry->Entries[entry->EntriesNum].Number = 7;
		}
		entry->EntriesNum++;
	}
	if (Recurrance == 24*30) {
		entry->Entries[entry->EntriesNum].EntryType	= CAL_REPEAT_DAY;
		entry->Entries[entry->EntriesNum].Number	= entry->Entries[num].Date.Day;
		entry->EntriesNum++;
	}
	if (Recurrance == 24*365) {
		entry->Entries[entry->EntriesNum].EntryType	= CAL_REPEAT_DAY;
		entry->Entries[entry->EntriesNum].Number	= entry->Entries[num].Date.Day;
		entry->EntriesNum++;
		entry->Entries[entry->EntriesNum].EntryType	= CAL_REPEAT_MONTH;
		entry->Entries[entry->EntriesNum].Number	= entry->Entries[num].Date.Month;
		entry->EntriesNum++;
	}
	if (endday == NULL || endday[0]*256+endday[1] == 0) return;
	dbgprintf("Repeat   : %i times\n",endday[0]*256+endday[1]);
	memcpy(&entry->Entries[entry->EntriesNum].Date,&entry->Entries[num].Date,sizeof(GSM_DateTime));
	entry->Entries[entry->EntriesNum].EntryType = CAL_REPEAT_STOPDATE;
	switch (Recurrance) {
		case 24:
		case 24*7:
		case 24*14:
			GetTimeDifference(60*60*Recurrance*(endday[0]*256+endday[1]-1), &entry->Entries[entry->EntriesNum].Date, true, 1);
			entry->EntriesNum++;
			break;
		case 24*30:
			for (i=0;i<endday[0]*256+endday[1]-1;i++) {
				if (entry->Entries[entry->EntriesNum].Date.Month == 12) {
					entry->Entries[entry->EntriesNum].Date.Month = 1;
					entry->Entries[entry->EntriesNum].Date.Year++;
				} else {
					entry->Entries[entry->EntriesNum].Date.Month++;
				}
			}
			entry->EntriesNum++;
			break;
		case 24*365:
			entry->Entries[entry->EntriesNum].Date.Year += (endday[0]*256+endday[1] - 1);
			entry->EntriesNum++;
			break;
	}
	dbgprintf("End Repeat Time: %04i-%02i-%02i %02i:%02i:%02i\n",
		entry->Entries[entry->EntriesNum-1].Date.Year,
		entry->Entries[entry->EntriesNum-1].Date.Month,
		entry->Entries[entry->EntriesNum-1].Date.Day,
		entry->Entries[entry->EntriesNum-1].Date.Hour,
		entry->Entries[entry->EntriesNum-1].Date.Minute,
		entry->Entries[entry->EntriesNum-1].Date.Second);
}

bool IsCalendarNoteFromThePast(GSM_CalendarEntry *note)
{
	bool 		Past = true;
	int		i,End=-1;
	GSM_DateTime	DT;
	char		rec[20],endday[20];

	GSM_GetCurrentDateTime (&DT);
	for (i = 0; i < note->EntriesNum; i++) {
		switch (note->Entries[i].EntryType) {
		case CAL_START_DATETIME :
			if (note->Entries[i].Date.Year > DT.Year) Past = false;
			if (note->Entries[i].Date.Year == DT.Year &&
			    note->Entries[i].Date.Month > DT.Month) Past = false;
			if (note->Entries[i].Date.Year == DT.Year &&
			    note->Entries[i].Date.Month == DT.Month &&
			    note->Entries[i].Date.Day >= DT.Day) Past = false;
			break;
		case CAL_REPEAT_STOPDATE:
			if (End == -1) End = i;
		default:
			break;
		}
		if (!Past) break;
	}
	if (note->Type == GSM_CAL_BIRTHDAY) Past = false;
	GSM_SetCalendarRecurranceRepeat(rec, endday, note);
	if (rec[0] != 0 || rec[1] != 0) {
		if (End == -1) {
			Past = false;
		} else {
			if (note->Entries[End].Date.Year > DT.Year) Past = false;
			if (note->Entries[End].Date.Year == DT.Year &&
			    note->Entries[End].Date.Month > DT.Month) Past = false;
			if (note->Entries[End].Date.Year == DT.Year &&
			    note->Entries[End].Date.Month == DT.Month &&
			    note->Entries[End].Date.Day >= DT.Day) Past = false;
		}
	}
	return Past;
}

void GSM_CalendarFindDefaultTextTimeAlarmPhone(GSM_CalendarEntry *entry, int *Text, int *Time, int *Alarm, int *Phone, int *EndTime, int *Location)
{
	int i;

	*Text		= -1;
	*Time		= -1;
	*Alarm		= -1;
	*Phone		= -1;
	*EndTime	= -1;
	*Location	= -1;
	for (i = 0; i < entry->EntriesNum; i++) {
		switch (entry->Entries[i].EntryType) {
		case CAL_START_DATETIME :
			if (*Time == -1) *Time = i;
			break;
		case CAL_END_DATETIME :
			if (*EndTime == -1) *EndTime = i;
			break;
		case CAL_TONE_ALARM_DATETIME :
		case CAL_SILENT_ALARM_DATETIME:
			if (*Alarm == -1) *Alarm = i;
			break;
		case CAL_TEXT:
			if (*Text == -1) *Text = i;
			break;
		case CAL_PHONE:
			if (*Phone == -1) *Phone = i;
			break;
		case CAL_LOCATION:
			if (*Location == -1) *Location = i;
			break;
		default:
			break;
		}
	}
}


/**
 * Function to compute time difference between alarm and event time.
 */
GSM_DateTime	VCALTimeDiff ( GSM_DateTime *Alarm,  GSM_DateTime *Time)
{
	int dt;
	struct tm talarm, ttime;
	GSM_DateTime delta;

	talarm.tm_mday = Alarm->Day;
	talarm.tm_mon  = Alarm->Month-1;
	talarm.tm_year = Alarm->Year -1900;
	talarm.tm_hour = Alarm->Hour;
	talarm.tm_min  = Alarm->Minute;
	talarm.tm_sec  = Alarm->Second;
	talarm.tm_isdst = 0;

	ttime.tm_mday = Time->Day;
	ttime.tm_mon  = Time->Month-1;
	ttime.tm_year = Time->Year -1900;
	ttime.tm_hour = Time->Hour;
	ttime.tm_min  = Time->Minute;
	ttime.tm_sec  = Time->Second;
	ttime.tm_isdst = 0;

	dt = mktime(&ttime) - mktime(&talarm);

	if (dt <= 0) dt = 0;

	/* Mozilla Calendar only accepts relative times for alarm.
	   Maximum representation of time differences is in days.*/
	delta.Year	= 0;
	delta.Month  	= 0;
	delta.Day	= dt / 86400	  ; dt = dt - delta.Day * 86400;
	delta.Hour	= dt / 3600	  ; dt = dt - delta.Hour * 3600;
	delta.Minute 	= dt / 60	  ; dt = dt - delta.Minute * 60;
	delta.Second 	= dt;

	/* Use only one representation. If delta has minutes convert all to minutes etc.*/
	if (delta.Minute !=0) {
		delta.Minute = delta.Day * 24*60 + delta.Hour * 60 + delta.Minute;
		delta.Day=0; delta.Hour=0;
	} else if (delta.Hour !=0) {
		delta.Hour = delta.Day * 24 + delta.Hour;
		delta.Day=0;
	}

	delta.Timezone = 0;

	return delta;
}


GSM_Error GSM_Translate_Category (GSM_CatTranslation direction, char *string, GSM_CalendarNoteType *Type)
{
	/* Mozilla has user defined categories. These must be converted to GSM_CAL_xxx types.
	   TODO: For now we use hardcoded conversions. Should be user configurable. */

	switch (direction) {
	case TRANSL_TO_GSM:
		if (strstr(string,"MEETING")) 			*Type = GSM_CAL_MEETING;
		else if (strstr(string,"REMINDER")) 		*Type = GSM_CAL_REMINDER;
		else if (strstr(string,"DATE"))	 		*Type = GSM_CAL_REMINDER; //SE
		else if (strstr(string,"TRAVEL"))	 	*Type = GSM_CAL_TRAVEL;   //SE
		else if (strstr(string,"VACATION"))	 	*Type = GSM_CAL_VACATION; //SE
		else if (strstr(string,"MISCELLANEOUS"))	*Type = GSM_CAL_MEMO;
		else if (strstr(string,"PHONE CALL")) 		*Type = GSM_CAL_CALL;
		else if (strstr(string,"SPECIAL OCCASION")) 	*Type = GSM_CAL_BIRTHDAY;
		else if (strstr(string,"ANNIVERSARY")) 		*Type = GSM_CAL_BIRTHDAY;
		else if (strstr(string,"APPOINTMENT")) 		*Type = GSM_CAL_MEETING;
		/* These are the Nokia 6230i categories in german. */
		else if (strstr(string,"Erinnerung"))	 	*Type = GSM_CAL_REMINDER;
		else if (strstr(string,"Besprechung"))	 	*Type = GSM_CAL_MEETING;
		else if (strstr(string,"Anrufen"))	 	*Type = GSM_CAL_CALL;
		else if (strstr(string,"Geburtstag"))	 	*Type = GSM_CAL_BIRTHDAY;
		else if (strstr(string,"Notiz"))	 	*Type = GSM_CAL_MEMO;
		/* default */
		else *Type = GSM_CAL_MEETING;
		break;

	case TRANSL_TO_VCAL:
		switch (*Type) {
			case GSM_CAL_CALL:
				strcpy(string, "PHONE CALL");
				break;
			case GSM_CAL_MEETING:
				strcpy(string, "MEETING");
				break;
			case GSM_CAL_REMINDER:
				strcpy(string, "DATE");
				break;
			case GSM_CAL_TRAVEL:
				strcpy(string, "TRAVEL");
				break;
			case GSM_CAL_VACATION:
				strcpy(string, "VACATION");
				break;
			case GSM_CAL_BIRTHDAY:
				strcpy(string, "ANNIVERSARY");
				break;
			case GSM_CAL_MEMO:
			default:
				strcpy(string, "MISCELLANEOUS");
				break;
		}
		break;
	}
	return 0;
}

/**
 * Grabs single value of type from calendar note starting with record
 * start.
 */
GSM_Error GSM_Calendar_GetValue(const GSM_CalendarEntry *note, int *start, const GSM_CalendarType type, int *number, GSM_DateTime *date)
{
	for (; *start < note->EntriesNum; (*start)++) {
		if (note->Entries[*start].EntryType == type) {
			if (number != NULL) {
				*number = note->Entries[*start].Number;
			}
			if (date != NULL) {
				*date = note->Entries[*start].Date;
			}
			(*start)++;
			return ERR_NONE;
		}
	}
	return ERR_EMPTY;
}

/**
 * Converts Gammu recurrence to vCal format. See GSM_DecodeVCAL_RRULE
 * for grammar description.
 */
GSM_Error GSM_EncodeVCAL_RRULE(char *Buffer, int *Length, GSM_CalendarEntry *note, int TimePos, GSM_VCalendarVersion Version)
{
	int i;
	int j;
	const char *DaysOfWeek[8] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA", "SU"};
	bool repeating = false;
	int repeat_dayofweek = -1;
	int repeat_day = -1;
	int repeat_dayofyear = -1;
	int repeat_weekofmonth = -1;
	int repeat_month = -1;
	int repeat_count = -1;
	int repeat_frequency = -1;
	GSM_DateTime repeat_startdate = {0,0,0,0,0,0,0};
	GSM_DateTime repeat_stopdate = {0,0,0,0,0,0,0};

	/* First scan for entry, whether there is  recurrence at all */
	for (i = 0; i < note->EntriesNum; i++) {
		switch (note->Entries[i].EntryType) {
			/* We don't care about following here */
			case CAL_PRIVATE:
			case CAL_CONTACTID:
			case CAL_START_DATETIME:
			case CAL_END_DATETIME:
			case CAL_LAST_MODIFIED:
			case CAL_TONE_ALARM_DATETIME:
			case CAL_SILENT_ALARM_DATETIME:
			case CAL_TEXT:
			case CAL_DESCRIPTION:
			case CAL_PHONE:
			case CAL_LOCATION:
			case CAL_LUID:
				break;
			case CAL_REPEAT_DAYOFWEEK:
				repeat_dayofweek 	= note->Entries[i].Number;
				repeating 		= true;
				break;
			case CAL_REPEAT_DAY:
				repeat_day 		= note->Entries[i].Number;
				repeating 		= true;
				break;
			case CAL_REPEAT_DAYOFYEAR:
				repeat_dayofyear	= note->Entries[i].Number;
				repeating 		= true;
				break;
			case CAL_REPEAT_WEEKOFMONTH:
				repeat_weekofmonth 	= note->Entries[i].Number;
				repeating 		= true;
				break;
			case CAL_REPEAT_MONTH:
				repeat_month 		= note->Entries[i].Number;
				repeating 		= true;
				break;
			case CAL_REPEAT_FREQUENCY:
				repeat_frequency 	= note->Entries[i].Number;
				repeating 		= true;
				break;
			case CAL_REPEAT_COUNT:
				repeat_count	 	= note->Entries[i].Number;
				repeating 		= true;
				break;
			case CAL_REPEAT_STARTDATE:
				repeat_startdate 	= note->Entries[i].Date;
				repeating 		= true;
				break;
			case CAL_REPEAT_STOPDATE:
				repeat_stopdate 	= note->Entries[i].Date;
				repeating 		= true;
				break;
		}
	}
	/* Did we found something? */
	if (repeating) {
		*Length += sprintf(Buffer + (*Length), "RRULE:");

		/* Safe fallback */
		if (repeat_frequency == -1) {
			repeat_frequency = 1;
		}

		if ((repeat_dayofyear != -1) || (Version == Siemens_VCalendar && repeat_day != -1 && repeat_month != -1)) {
			/* Yearly by day */
			*Length += sprintf(Buffer + (*Length), "YD%d", repeat_frequency);

			/* Store month numbers */
			for (i = 0; i < note->EntriesNum; i++) {
				if (note->Entries[i].EntryType == CAL_REPEAT_DAYOFYEAR) {
					*Length += sprintf(Buffer + (*Length), " %d",
						note->Entries[i].Number);
				}
			}
		} else if (repeat_day != -1 && repeat_month != -1) {
			/* Yearly by month and day */
			*Length += sprintf(Buffer + (*Length), "YM%d", repeat_frequency);

			/* Store month numbers */
			for (i = 0; i < note->EntriesNum; i++) {
				if (note->Entries[i].EntryType == CAL_REPEAT_MONTH) {
					*Length += sprintf(Buffer + (*Length), " %d",
						note->Entries[i].Number);
				}
			}
		} else if (repeat_day != -1) {
			/* Monthly by day */
			*Length += sprintf(Buffer + (*Length), "MD%d", repeat_frequency);

			/* Store day numbers */
			for (i = 0; i < note->EntriesNum; i++) {
				if (note->Entries[i].EntryType == CAL_REPEAT_DAY) {
					*Length += sprintf(Buffer + (*Length), " %d",
						note->Entries[i].Number);
				}
			}
		} else if (repeat_dayofweek != -1 && repeat_weekofmonth != -1) {
			/* Monthly by day and week */
			*Length += sprintf(Buffer + (*Length), "MP%d", repeat_frequency);

			/* Store week numbers and week days */
			for (i = 0; i < note->EntriesNum; i++) {
				if (note->Entries[i].EntryType == CAL_REPEAT_WEEKOFMONTH) {
					*Length += sprintf(Buffer + (*Length), " %d+",
						note->Entries[i].Number);
					for (j = 0; j < note->EntriesNum; j++) {
						if (note->Entries[j].EntryType == CAL_REPEAT_DAYOFWEEK) {
							*Length += sprintf(Buffer + (*Length), " %s",
								DaysOfWeek[note->Entries[j].Number]);
						}
					}
				}
			}
		} else if (repeat_dayofweek != -1) {
			/* Weekly by day */
			*Length += sprintf(Buffer + (*Length), "W%d", repeat_frequency);

			/* Store week days */
			for (i = 0; i < note->EntriesNum; i++) {
				if (note->Entries[i].EntryType == CAL_REPEAT_DAYOFWEEK) {
					*Length += sprintf(Buffer + (*Length), " %s",
						DaysOfWeek[note->Entries[i].Number]);
				}
			}
		} else {
			/* Daily */
			*Length += sprintf(Buffer + (*Length), "D%d", repeat_frequency);
		}

		/* Store number of repetitions if available */
		if (repeat_count != -1) {
			*Length += sprintf(Buffer + (*Length), " #%d", repeat_count);
		}

		/* Store end of repetition date if available */
		if (repeat_stopdate.Day != 0) {
			SaveVCALDate(Buffer, Length, &repeat_stopdate, NULL);
		} else {
			/* Add EOL */
			*Length += sprintf(Buffer + (*Length), "%c%c", 13, 10);
		}

		return ERR_NONE;
	}
	return ERR_EMPTY;
}

/**
 * Adjusts all datetime information in calendar entry according to delta.
 */
void GSM_Calendar_AdjustDate(GSM_CalendarEntry *note, GSM_DeltaTime *delta)
{
	int i;

	/* Loop over entries */
	for (i=0; i < note->EntriesNum; i++) {
		switch (note->Entries[i].EntryType) {
			case CAL_START_DATETIME :
			case CAL_END_DATETIME :
			case CAL_TONE_ALARM_DATETIME :
			case CAL_SILENT_ALARM_DATETIME:
			case CAL_LAST_MODIFIED:
			case CAL_REPEAT_STARTDATE:
			case CAL_REPEAT_STOPDATE:
				note->Entries[i].Date = GSM_AddTime(note->Entries[i].Date, *delta);
				break;
			case CAL_TEXT:
			case CAL_DESCRIPTION:
			case CAL_PHONE:
			case CAL_LOCATION:
			case CAL_LUID:
			case CAL_REPEAT_DAYOFWEEK:
			case CAL_REPEAT_DAY:
			case CAL_REPEAT_WEEKOFMONTH:
			case CAL_REPEAT_MONTH:
			case CAL_REPEAT_FREQUENCY:
			case CAL_REPEAT_DAYOFYEAR:
			case CAL_REPEAT_COUNT:
			case CAL_PRIVATE:
			case CAL_CONTACTID:
				/* No need to care */
				break;
		}
	}
}

void GSM_ToDo_AdjustDate(GSM_ToDoEntry *note, GSM_DeltaTime *delta)
{
	int i;

	/* Loop over entries */
	for (i=0; i < note->EntriesNum; i++) {
		switch (note->Entries[i].EntryType) {
			case TODO_END_DATETIME :
			case TODO_ALARM_DATETIME :
			case TODO_SILENT_ALARM_DATETIME:
			case TODO_LAST_MODIFIED:
				note->Entries[i].Date = GSM_AddTime(note->Entries[i].Date, *delta);
				break;
			case TODO_TEXT:
			case TODO_DESCRIPTION:
			case TODO_PHONE:
			case TODO_LOCATION:
			case TODO_LUID:
			case TODO_PRIVATE:
			case TODO_COMPLETED:
			case TODO_CONTACTID:
			case TODO_CATEGORY:
				/* No need to care */
				break;
		}
	}
}

GSM_Error GSM_EncodeVCALENDAR(char *Buffer, int *Length, GSM_CalendarEntry *note, bool header, GSM_VCalendarVersion Version)
{
	GSM_DateTime 	deltatime;
	char 		dtstr[20];
	char		category[100];
	int		i, alarm = -1, date = -1;

	/* Write header */
	if (header) {
		*Length+=sprintf(Buffer, "BEGIN:VCALENDAR%c%c",13,10);
		*Length+=sprintf(Buffer+(*Length), "VERSION:%s%c%c", Version == Mozilla_VCalendar ? "2.0" : "1.0", 13, 10);
	}
	*Length+=sprintf(Buffer+(*Length), "BEGIN:VEVENT%c%c",13,10);

	if (Version == Mozilla_VCalendar) {
		/* Mozilla Calendar needs UIDs. http://www.innerjoin.org/iCalendar/events-and-uids.html */
		*Length+=sprintf(Buffer+(*Length), "UID:calendar-%i%c%c",note->Location,13,10);
		*Length+=sprintf(Buffer+(*Length), "STATUS:CONFIRMED%c%c",13,10);
	}

	/* Store category */
	GSM_Translate_Category(TRANSL_TO_VCAL, category, &note->Type);
	*Length += sprintf(Buffer+(*Length), "CATEGORIES:%s%c%c", category, 13, 10);

	/* Loop over entries */
	for (i=0; i < note->EntriesNum; i++) {
		switch (note->Entries[i].EntryType) {
			case CAL_START_DATETIME :
				date = i;
				if (Version == Mozilla_VCalendar && (note->Type == GSM_CAL_MEMO || note->Type == GSM_CAL_BIRTHDAY)) {
					SaveVCALDate(Buffer, Length, &note->Entries[i].Date, "DTSTART;VALUE=DATE");
				} else {
					SaveVCALDateTime(Buffer, Length, &note->Entries[i].Date, "DTSTART");
				}
				break;
			case CAL_END_DATETIME :
				if (Version == Mozilla_VCalendar && (note->Type == GSM_CAL_MEMO || note->Type == GSM_CAL_BIRTHDAY)) {
					SaveVCALDate(Buffer, Length, &note->Entries[i].Date, "DTEND;VALUE=DATE");
				} else {
					SaveVCALDateTime(Buffer, Length, &note->Entries[i].Date, "DTEND");
				}
				break;
			case CAL_TONE_ALARM_DATETIME :
				alarm = i;
				/* Disable alarm for birthday entries. Mozilla would generate an alarm before birth! */
				if (Version != Mozilla_VCalendar || note->Type != GSM_CAL_BIRTHDAY) {
					SaveVCALDateTime(Buffer, Length, &note->Entries[i].Date, "AALARM");
				}
				break;
			case CAL_SILENT_ALARM_DATETIME:
				alarm = i;
				/* Disable alarm for birthday entries. Mozilla would generate an alarm before birth! */
				if (Version != Mozilla_VCalendar || note->Type != GSM_CAL_BIRTHDAY) {
					SaveVCALDateTime(Buffer, Length, &note->Entries[i].Date, "DALARM");
				}
				break;
			case CAL_LAST_MODIFIED:
				if (Version == Mozilla_VCalendar && (note->Type == GSM_CAL_MEMO || note->Type == GSM_CAL_BIRTHDAY)) {
					SaveVCALDate(Buffer, Length, &note->Entries[i].Date, "LAST-MODIFIED;VALUE=DATE");
				} else {
					SaveVCALDateTime(Buffer, Length, &note->Entries[i].Date, "LAST-MODIFIED");
				}
				break;
			case CAL_TEXT:
				SaveVCALText(Buffer, Length, note->Entries[i].Text, "SUMMARY");
				break;
			case CAL_DESCRIPTION:
				SaveVCALText(Buffer, Length, note->Entries[i].Text, "DESCRIPTION");
				break;
			case CAL_PHONE:
				/* There is no specific field for phone number, use description */
				SaveVCALText(Buffer, Length, note->Entries[i].Text, "DESCRIPTION");
				break;
			case CAL_LOCATION:
				SaveVCALText(Buffer, Length, note->Entries[i].Text, "LOCATION");
				break;
			case CAL_LUID:
				SaveVCALText(Buffer, Length, note->Entries[i].Text, "X-IRMC-LUID");
				break;
			case CAL_REPEAT_DAYOFWEEK:
			case CAL_REPEAT_DAY:
			case CAL_REPEAT_WEEKOFMONTH:
			case CAL_REPEAT_MONTH:
			case CAL_REPEAT_FREQUENCY:
			case CAL_REPEAT_STARTDATE:
			case CAL_REPEAT_STOPDATE:
			case CAL_REPEAT_DAYOFYEAR:
			case CAL_REPEAT_COUNT:
				/* Handled later */
				break;
			case CAL_PRIVATE:
				if (note->Entries[i].Number == 0) {
					*Length+=sprintf(Buffer+(*Length), "CLASS:PUBLIC%c%c",13,10);
				} else {
					*Length+=sprintf(Buffer+(*Length), "CLASS:PRIVATE%c%c",13,10);
				}
				break;
			case CAL_CONTACTID:
				/* Not supported */
				break;
		}
	}

	/* Handle recurrance */
	if (note->Type == GSM_CAL_BIRTHDAY) {
		if (Version == Mozilla_VCalendar) {
			*Length+=sprintf(Buffer+(*Length), "X-MOZILLA-RECUR-DEFAULT-UNITS:years%c%c",13,10);
		} else if (Version == Siemens_VCalendar) {
			*Length+=sprintf(Buffer+(*Length), "RRULE:YD1%c%c",13,10);
		} else {
			*Length+=sprintf(Buffer+(*Length), "RRULE:YM1%c%c",13,10);
		}
	} else {
		GSM_EncodeVCAL_RRULE(Buffer, Length, note, date, Version);
	}

	/* Include mozilla specific alarm encoding */
	if (Version == Mozilla_VCalendar && alarm != -1 && date != -1) {
		deltatime = VCALTimeDiff(&note->Entries[alarm].Date, &note->Entries[date].Date);

		dtstr[0]='\0';
		if (deltatime.Minute !=0) {
			*Length+=sprintf(Buffer+(*Length), "X-MOZILLA-ALARM-DEFAULT-UNITS:minutes%c%c",13,10);
			*Length+=sprintf(Buffer+(*Length), "X-MOZILLA-ALARM-DEFAULT-LENGTH:%i%c%c",
				deltatime.Minute,13,10);
			sprintf(dtstr,"-PT%iM",deltatime.Minute);
		} else if (deltatime.Hour !=0) {
			*Length+=sprintf(Buffer+(*Length), "X-MOZILLA-ALARM-DEFAULT-UNITS:hours%c%c",13,10);
			*Length+=sprintf(Buffer+(*Length), "X-MOZILLA-ALARM-DEFAULT-LENGTH:%i%c%c",
				deltatime.Hour,13,10);
			sprintf(dtstr,"-PT%iH",deltatime.Hour);
		} else if (deltatime.Day !=0) {
			*Length+=sprintf(Buffer+(*Length), "X-MOZILLA-ALARM-DEFAULT-UNITS:days%c%c",13,10);
			*Length+=sprintf(Buffer+(*Length), "X-MOZILLA-ALARM-DEFAULT-LENGTH:%i%c%c",
				deltatime.Day,13,10);
			sprintf(dtstr,"-P%iD",deltatime.Day);
		}
		if (dtstr[0] != '\0') {
			*Length+=sprintf(Buffer+(*Length), "BEGIN:VALARM%c%c",13,10);
			*Length+=sprintf(Buffer+(*Length), "TRIGGER;VALUE=DURATION%c%c",13,10);
			*Length+=sprintf(Buffer+(*Length), " :%s%c%c",dtstr,13,10);
			*Length+=sprintf(Buffer+(*Length), "END:VALARM%c%c",13,10);
		}
	}

	*Length+=sprintf(Buffer+(*Length), "END:VEVENT%c%c",13,10);
	if (header) *Length+=sprintf(Buffer+(*Length), "END:VCALENDAR%c%c",13,10);

	return ERR_NONE;
}

void GSM_ToDoFindDefaultTextTimeAlarmCompleted(GSM_ToDoEntry *entry, int *Text, int *Alarm, int *Completed, int *EndTime, int *Phone)
{
	int i;

	*Text		= -1;
	*EndTime	= -1;
	*Alarm		= -1;
	*Completed	= -1;
	*Phone		= -1;
	for (i = 0; i < entry->EntriesNum; i++) {
		switch (entry->Entries[i].EntryType) {
		case TODO_END_DATETIME :
			if (*EndTime == -1) *EndTime = i;
			break;
		case TODO_ALARM_DATETIME :
		case TODO_SILENT_ALARM_DATETIME:
			if (*Alarm == -1) *Alarm = i;
			break;
		case TODO_TEXT:
			if (*Text == -1) *Text = i;
			break;
		case TODO_COMPLETED:
			if (*Completed == -1) *Completed = i;
			break;
		case TODO_PHONE:
			if (*Phone == -1) *Phone = i;
			break;
		default:
			break;
		}
	}
}

GSM_Error GSM_EncodeVTODO(char *Buffer, int *Length, GSM_ToDoEntry *note, bool header, GSM_VToDoVersion Version)
{
	GSM_DateTime 	deltatime;
	char 		dtstr[20];
	char		category[100];
	int		i, alarm = -1, date = -1;

	/* Write header */
	if (header) {
		*Length+=sprintf(Buffer, "BEGIN:VCALENDAR%c%c",13,10);
		*Length+=sprintf(Buffer+(*Length), "VERSION:%s%c%c", Version == Mozilla_VToDo ? "2.0" : "1.0", 13, 10);
	}
	*Length+=sprintf(Buffer+(*Length), "BEGIN:VTODO%c%c",13,10);

	if (Version == Mozilla_VCalendar) {
		/* Mozilla Calendar needs UIDs. http://www.innerjoin.org/iCalendar/events-and-uids.html */
		*Length+=sprintf(Buffer+(*Length), "UID:calendar-%i%c%c",note->Location,13,10);
		*Length+=sprintf(Buffer+(*Length), "STATUS:CONFIRMED%c%c",13,10);
	}

	if (Version == Mozilla_VToDo) {
		switch (note->Priority) {
			case GSM_Priority_None	: *Length+=sprintf(Buffer+(*Length), "PRIORITY:0%c%c",13,10); break;
			case GSM_Priority_Low	: *Length+=sprintf(Buffer+(*Length), "PRIORITY:1%c%c",13,10); break;
			case GSM_Priority_Medium: *Length+=sprintf(Buffer+(*Length), "PRIORITY:5%c%c",13,10); break;
			case GSM_Priority_High	: *Length+=sprintf(Buffer+(*Length), "PRIORITY:9%c%c",13,10); break;
		}
	} else {
		switch (note->Priority) {
			case GSM_Priority_None	: *Length+=sprintf(Buffer+(*Length), "PRIORITY:0%c%c",13,10); break;
			case GSM_Priority_Low	: *Length+=sprintf(Buffer+(*Length), "PRIORITY:1%c%c",13,10); break;
			case GSM_Priority_Medium: *Length+=sprintf(Buffer+(*Length), "PRIORITY:2%c%c",13,10); break;
			case GSM_Priority_High	: *Length+=sprintf(Buffer+(*Length), "PRIORITY:3%c%c",13,10); break;
		}
	}
	/* Store category */
	GSM_Translate_Category(TRANSL_TO_VCAL, category, &note->Type);
	*Length += sprintf(Buffer+(*Length), "CATEGORIES:%s%c%c", category, 13, 10);

	/* Loop over entries */
	for (i=0; i < note->EntriesNum; i++) {
		switch (note->Entries[i].EntryType) {
			case TODO_END_DATETIME :
				if (note->Entries[i].Date.Year   != 2037	&&
				    note->Entries[i].Date.Month  != 12	&&
				    note->Entries[i].Date.Day    != 31	&&
				    note->Entries[i].Date.Hour   != 23	&&
				    note->Entries[i].Date.Minute != 59 ) {
					SaveVCALDateTime(Buffer, Length, &note->Entries[i].Date, "DUE");
				}
				break;
			case TODO_ALARM_DATETIME :
				alarm = i;
				/* Disable alarm for birthday entries. Mozilla would generate an alarm before birth! */
				if (Version != Mozilla_VCalendar || note->Type != GSM_CAL_BIRTHDAY) {
					SaveVCALDateTime(Buffer, Length, &note->Entries[i].Date, "AALARM");
				}
				break;
			case TODO_SILENT_ALARM_DATETIME:
				alarm = i;
				/* Disable alarm for birthday entries. Mozilla would generate an alarm before birth! */
				if (Version != Mozilla_VCalendar || note->Type != GSM_CAL_BIRTHDAY) {
					SaveVCALDateTime(Buffer, Length, &note->Entries[i].Date, "DALARM");
				}
				break;
			case TODO_LAST_MODIFIED:
				SaveVCALDateTime(Buffer, Length, &note->Entries[i].Date, "LAST-MODIFIED");
				break;
			case TODO_TEXT:
				SaveVCALText(Buffer, Length, note->Entries[i].Text, "SUMMARY");
				break;
			case TODO_DESCRIPTION:
				SaveVCALText(Buffer, Length, note->Entries[i].Text, "DESCRIPTION");
				break;
			case TODO_PHONE:
				/* There is no specific field for phone number, use description */
				SaveVCALText(Buffer, Length, note->Entries[i].Text, "DESCRIPTION");
				break;
			case TODO_LOCATION:
				SaveVCALText(Buffer, Length, note->Entries[i].Text, "LOCATION");
				break;
			case TODO_LUID:
				SaveVCALText(Buffer, Length, note->Entries[i].Text, "X-IRMC-LUID");
				break;
			case TODO_PRIVATE:
				if (note->Entries[i].Number == 0) {
					*Length+=sprintf(Buffer+(*Length), "CLASS:PUBLIC%c%c",13,10);
				} else {
					*Length+=sprintf(Buffer+(*Length), "CLASS:PRIVATE%c%c",13,10);
				}
				break;
			case TODO_COMPLETED:
				if (note->Entries[i].Number == 1) {
					*Length+=sprintf(Buffer+(*Length), "STATUS:COMPLETED%c%c",13,10);
					*Length+=sprintf(Buffer+(*Length), "PERCENT-COMPLETE:100%c%c",13,10);
				} else {
					*Length+=sprintf(Buffer+(*Length), "STATUS:NEEDS ACTION%c%c",13,10);
				}
				break;
			case TODO_CONTACTID:
			case TODO_CATEGORY:
				/* Not supported */
				break;
		}

		/* Include mozilla specific alarm encoding */
		if (Version == Mozilla_VCalendar && alarm != -1 && date != -1) {
			deltatime = VCALTimeDiff(&note->Entries[alarm].Date, &note->Entries[date].Date);

			dtstr[0]='\0';
			if (deltatime.Minute !=0) {
				*Length+=sprintf(Buffer+(*Length), "X-MOZILLA-ALARM-DEFAULT-UNITS:minutes%c%c",13,10);
				*Length+=sprintf(Buffer+(*Length), "X-MOZILLA-ALARM-DEFAULT-LENGTH:%i%c%c",
					deltatime.Minute,13,10);
				sprintf(dtstr,"-PT%iM",deltatime.Minute);
			} else if (deltatime.Hour !=0) {
				*Length+=sprintf(Buffer+(*Length), "X-MOZILLA-ALARM-DEFAULT-UNITS:hours%c%c",13,10);
				*Length+=sprintf(Buffer+(*Length), "X-MOZILLA-ALARM-DEFAULT-LENGTH:%i%c%c",
					deltatime.Hour,13,10);
				sprintf(dtstr,"-PT%iH",deltatime.Hour);
			} else if (deltatime.Day !=0) {
				*Length+=sprintf(Buffer+(*Length), "X-MOZILLA-ALARM-DEFAULT-UNITS:days%c%c",13,10);
				*Length+=sprintf(Buffer+(*Length), "X-MOZILLA-ALARM-DEFAULT-LENGTH:%i%c%c",
					deltatime.Day,13,10);
				sprintf(dtstr,"-P%iD",deltatime.Day);
			}
			if (dtstr[0] != '\0') {
				*Length+=sprintf(Buffer+(*Length), "BEGIN:VALARM%c%c",13,10);
				*Length+=sprintf(Buffer+(*Length), "TRIGGER;VALUE=DURATION%c%c",13,10);
				*Length+=sprintf(Buffer+(*Length), " :%s%c%c",dtstr,13,10);
				*Length+=sprintf(Buffer+(*Length), "END:VALARM%c%c",13,10);
			}
		}
	}

	*Length+=sprintf(Buffer+(*Length), "END:VTODO%c%c",13,10);
	if (header) *Length+=sprintf(Buffer+(*Length), "END:VCALENDAR%c%c",13,10);

	return ERR_NONE;
}

GSM_TimeUnit ReadVCALTimeUnits (unsigned char *Buffer)
{
	if (strcasestr(Buffer,"days"))	return GSM_TimeUnit_Days;
	if (strcasestr(Buffer,"hours"))	return GSM_TimeUnit_Hours;
	if (strcasestr(Buffer,"minutes")) return GSM_TimeUnit_Minutes;
	if (strcasestr(Buffer,"seconds")) return GSM_TimeUnit_Seconds;
	return GSM_TimeUnit_Unknown;
}

GSM_DeltaTime ReadVCALTriggerTime (unsigned char *Buffer)
{
	GSM_DeltaTime 	dt;
	int 		sign = 1;
	int 		pos = 0;
	int 		val;
	char 		unit;

	dt.Timezone = 0;
	dt.Year = 0 ; dt.Day = 0; dt.Month = 0; dt.Hour = 0; dt.Minute = 0; dt.Second = 0;

	if (Buffer[pos] == '+') {
		sign = 1; pos++;
	} else if (Buffer[pos] == '-') {
		sign = -1; pos++;
	}
	if (Buffer[pos] == 'P') pos++;
	if (Buffer[pos] == 'T') pos++;

	if ( !sscanf(Buffer+pos,"%i%c",&val,&unit) ) return dt;

	switch (unit) {
		case 'D': dt.Day    = sign * val ; break;
		case 'H': dt.Hour   = sign * val ; break;
		case 'M': dt.Minute = sign * val ; break;
		case 'S': dt.Second = sign * val ; break;
	}

	return dt;
}

/**
 * Prepare input buffer (notably line continuations).
 */
int GSM_Make_VCAL_Lines (unsigned char *Buffer, int *lBuffer)
{
	int src=0;
	int dst=0;

	for (src=0; src <= *lBuffer; src++) {
		if (Buffer[src] == '\r') src++;
		if (Buffer[src] == '\n') {
			if (Buffer[src+1] == ' ' && Buffer[src+2] == ':' ) src = src + 2;
			if (Buffer[src+1] == ' ' && Buffer[src+2] == ';' ) src = src + 2;
		}
		if (dst > src) return ERR_UNKNOWN;
		Buffer[dst] = Buffer[src];
		dst++;
	}
	*lBuffer = dst-1;
	return ERR_NONE;
}

/**
 * Decode day of week to gammu enumeration (1 = Monday...7 = Sunday).
 */
GSM_Error GSM_DecodeVCAL_DOW(const char *Buffer, int *Output)
{
	if (toupper(Buffer[0])== 'M' && toupper(Buffer[1]) == 'O') {
		*Output = 1;
		return ERR_NONE;
	} else if (toupper(Buffer[0])== 'T' && toupper(Buffer[1]) == 'U') {
		*Output = 2;
		return ERR_NONE;
	} else if (toupper(Buffer[0])== 'W' && toupper(Buffer[1]) == 'E') {
		*Output = 3;
		return ERR_NONE;
	} else if (toupper(Buffer[0])== 'T' && toupper(Buffer[1]) == 'H') {
		*Output = 4;
		return ERR_NONE;
	} else if (toupper(Buffer[0])== 'F' && toupper(Buffer[1]) == 'R') {
		*Output = 5;
		return ERR_NONE;
	} else if (toupper(Buffer[0])== 'S' && toupper(Buffer[1]) == 'A') {
		*Output = 6;
		return ERR_NONE;
	} else if (toupper(Buffer[0])== 'S' && toupper(Buffer[1]) == 'U') {
		*Output = 7;
		return ERR_NONE;
	}
	return ERR_UNKNOWN;
}

/**
 * Decodes vCalendar RRULE recurrance format into calendar entry. It
 * should be implemented according to following grammar:
 *
 * @code
 *   {}         0 or more
 *
 *   []         0 or 1
 *
 *   start      ::= <daily> [<enddate>] |
 *
 *               <weekly> [<enddate>] |
 *
 *               <monthlybypos> [<enddate>] |
 *
 *               <monthlybyday> [<enddate>] |
 *
 *               <yearlybymonth> [<enddate>] |
 *
 *               <yearlybyday> [<enddate>]
 *
 *   digit ::= <0|1|2|3|4|5|6|7|8|9>
 *
 *   digits ::= <digit> {<digits>}
 *
 *   enddate    ::= ISO 8601_date_time value(e.g., 19940712T101530Z)
 *
 *   interval   ::= <digits>
 *
 *   duration   ::= #<digits>
 *
 *   lastday    ::= LD
 *
 *   plus               ::= +
 *
 *   minus              ::= -
 *
 *   daynumber          ::= <1-31> [<plus>|<minus>]| <lastday>
 *
 *   daynumberlist      ::= daynumber {<daynumberlist>}
 *
 *   month              ::= <1-12>
 *
 *   monthlist  ::= <month> {<monthlist>}
 *
 *   day                ::= <1-366>
 *
 *   daylist            ::= <day> {<daylist>}
 *
 *   occurrence ::= <1-5><plus> | <1-5><minus>
 *
 *   weekday    ::= <SU|MO|TU|WE|TH|FR|SA>
 *
 *   weekdaylist        ::= <weekday> {<weekdaylist>}
 *
 *   occurrenceweekday  ::= [<occurrence>] <weekday>
 *
 *   occurenceweekdaylist       ::= <occurenceweekday>
 *
 *      {<occurenceweekdaylist>}
 *
 *   daily              ::= D<interval> [<duration>]
 *
 *   weekly             ::= W<interval> [<weekdaylist>] [<duration>]
 *
 *   monthlybypos       ::= MP<interval> [<occurrenceweekdaylist>]
 *
 *      [<duration>]
 *
 *   monthlybyday       ::= MD<interval> [<daynumberlist>] [<duration>]
 *
 *   yearlybymonth      ::= YM<interval> [<monthlist>] [<duration>]
 *
 *   yearlybyday        ::= YD<interval> [<daylist>] [<duration>]
 *
 * @endcode
 *
 * @li @b enddate      Controls when a repeating event terminates. The enddate
 *              is the last time an event can occur.
 *
 * @li @b Interval     Defines the frequency in which a rule repeats.
 *
 * @li @b duration     Controls the number of events a rule generates.
 *
 * @li @b Lastday      Can be used as a replacement to daynumber to indicate
 * the last day of the month.
 *
 * @li @b daynumber    A number representing a day of the month.
 *
 * @li @b month                A number representing a month of the year.
 *
 * @li @b day          A number representing a day of the year.
 *
 * @li @b occurrence   Controls which week of the month a particular weekday
 * event occurs.
 *
 * @li @b weekday      A symbol representing a day of the week.
 *
 * @li @b daily                Defines a rule that repeats on a daily basis.
 *
 * @li @b weekly               Defines a rule that repeats on a weekly basis.
 *
 * @li @b monthlybypos Defines a rule that repeats on a monthly basis on a
 * relative day and week.
 *
 * @li @b monthlybyday Defines a rule that repeats on a monthly basis on an
 * absolute day.
 *
 * @li @b yearlybymonth        Defines a rule that repeats on specific months
 * of the year.
 *
 * @li @b yearlybyday  Defines a rule that repeats on specific days of the
 * year.
 *
 * @todo Negative week of month and day of month are not supported.
 */
GSM_Error GSM_DecodeVCAL_RRULE(const char *Buffer, GSM_CalendarEntry *Calendar, int TimePos)
{
	const char *pos = Buffer;
	bool have_info;

/* Skip spaces */
#define NEXT_NOSPACE(terminate) \
	while (isspace(*pos) && *pos) pos++; \
	if (terminate && *pos == 0) return ERR_NONE;
/* Skip numbers */
#define NEXT_NONUMBER(terminate) \
	while (isdigit(*pos) && *pos) pos++; \
	if (terminate && *pos == 0) return ERR_NONE;
/* Go to next char */
#define NEXT_CHAR(terminate) \
	if (*pos) pos++; \
	if (terminate && *pos == 0) return ERR_UNKNOWN;
/* Go to next char */
#define NEXT_CHAR_NOERR(terminate) \
	if (*pos) pos++; \
	if (terminate && *pos == 0) return ERR_NONE;

#define GET_DOW(type, terminate) \
	Calendar->Entries[Calendar->EntriesNum].EntryType = type; \
	if (GSM_DecodeVCAL_DOW(pos, &Calendar->Entries[Calendar->EntriesNum].Number) != ERR_NONE) return ERR_UNKNOWN; \
	Calendar->EntriesNum++; \
	NEXT_CHAR(1); \
	NEXT_CHAR_NOERR(terminate);

#define GET_NUMBER(type, terminate) \
	Calendar->Entries[Calendar->EntriesNum].EntryType = type; \
	Calendar->Entries[Calendar->EntriesNum].Number = atoi(pos); \
	Calendar->EntriesNum++; \
	NEXT_NONUMBER(terminate);

#define GET_FREQUENCY(terminate) \
	GET_NUMBER(CAL_REPEAT_FREQUENCY, terminate);

	/* This should not happen */
	if (TimePos == -1) {
		return ERR_UNKNOWN;
	}

	NEXT_NOSPACE(1);

	/* Detect primary rule type */
	switch (*pos) {
		/* Daily */
		case 'D':
			NEXT_CHAR(1);
			GET_FREQUENCY(1);
			break;
		/* Weekly */
		case 'W':
			NEXT_CHAR(1);
			GET_FREQUENCY(0);
			NEXT_NOSPACE(0);
			/* There might be now list of months, if there is none, we use date */
			have_info = false;

			while (isalpha(*pos)) {
				have_info = true;
				GET_DOW(CAL_REPEAT_DAYOFWEEK, 1);
				NEXT_NOSPACE(0);
			}

			if (!have_info) {
				Calendar->Entries[Calendar->EntriesNum].EntryType = CAL_REPEAT_DAYOFWEEK;
				Calendar->Entries[Calendar->EntriesNum].Number =
					GetDayOfWeek(
						Calendar->Entries[TimePos].Date.Year,
						Calendar->Entries[TimePos].Date.Month,
						Calendar->Entries[TimePos].Date.Day);
				if (Calendar->Entries[Calendar->EntriesNum].Number == 0) {
					Calendar->Entries[Calendar->EntriesNum].Number = 7;
				}
				Calendar->EntriesNum++;
			}
			break;
		/* Monthly */
		case 'M':
			NEXT_CHAR(1);
			switch (*pos) {
				/* Monthly by position */
				case 'P':
					NEXT_CHAR(1);
					GET_FREQUENCY(0);
					NEXT_NOSPACE(0);
					if (isdigit(*pos)) {
						GET_NUMBER(CAL_REPEAT_WEEKOFMONTH, 0);
						if (*pos == '+') {
							pos++;
						} else if (*pos == '-') {
							pos++;
							dbgprintf("WARNING: Negative week position not supported!");
						}
						NEXT_NOSPACE(0);

						while (isalpha(*pos)) {
							have_info = true;
							GET_DOW(CAL_REPEAT_DAYOFWEEK, 0);
							NEXT_NOSPACE(0);
						}
					} else {
						/* Need to fill in info from current date */
						Calendar->Entries[Calendar->EntriesNum].EntryType = CAL_REPEAT_WEEKOFMONTH;
						Calendar->Entries[Calendar->EntriesNum].Number =
							GetWeekOfMonth(
								Calendar->Entries[TimePos].Date.Year,
								Calendar->Entries[TimePos].Date.Month,
								Calendar->Entries[TimePos].Date.Day);
						Calendar->EntriesNum++;

						Calendar->Entries[Calendar->EntriesNum].EntryType = CAL_REPEAT_DAYOFWEEK;
						Calendar->Entries[Calendar->EntriesNum].Number =
							GetDayOfWeek(
								Calendar->Entries[TimePos].Date.Year,
								Calendar->Entries[TimePos].Date.Month,
								Calendar->Entries[TimePos].Date.Day);
						if (Calendar->Entries[Calendar->EntriesNum].Number == 0) {
							Calendar->Entries[Calendar->EntriesNum].Number = 7;
						}
						Calendar->EntriesNum++;
					}
					break;
				/* Monthly by day */
				case 'D':
					NEXT_CHAR(1);
					GET_FREQUENCY(0);
					NEXT_NOSPACE(0);
					if (isdigit(*pos)) {
						while (isdigit(*pos)) {
							GET_NUMBER(CAL_REPEAT_DAY, 0);
							if (*pos == '+') {
								pos++;
							} else if (*pos == '-') {
								pos++;
								dbgprintf("WARNING: Negative day position not supported!");
							}
							NEXT_NOSPACE(0);
						}
					} else {
						/* Need to fill in info from current date */
						Calendar->Entries[Calendar->EntriesNum].EntryType = CAL_REPEAT_DAY;
						Calendar->Entries[Calendar->EntriesNum].Number = Calendar->Entries[TimePos].Date.Day;
						Calendar->EntriesNum++;
					}

					break;
				default:
					dbgprintf("Could not decode recurrency: %s\n", pos);
					return ERR_UNKNOWN;
			}
			break;
		/* Yearly */
		case 'Y':
			NEXT_CHAR(1);
			switch (*pos) {
				/* Yearly by month */
				case 'M':
					NEXT_CHAR(1);
					GET_FREQUENCY(0);
					NEXT_NOSPACE(0);
					/* We need date of event */
					Calendar->Entries[Calendar->EntriesNum].EntryType = CAL_REPEAT_DAY;
					Calendar->Entries[Calendar->EntriesNum].Number =
						Calendar->Entries[TimePos].Date.Day;
					Calendar->EntriesNum++;
					/* There might be now list of months, if there is none, we use date */
					have_info = false;

					while (isdigit(*pos)) {
						have_info = true;
						GET_NUMBER(CAL_REPEAT_MONTH, 0);
						NEXT_NOSPACE(0);
					}

					if (!have_info) {
						Calendar->Entries[Calendar->EntriesNum].EntryType = CAL_REPEAT_MONTH;
						Calendar->Entries[Calendar->EntriesNum].Number =
							Calendar->Entries[TimePos].Date.Month;
						Calendar->EntriesNum++;
					}
					break;
				/* Yearly by day */
				case 'D':
					NEXT_CHAR(1);
					GET_FREQUENCY(0);
					NEXT_NOSPACE(0);
					/* There might be now list of days, if there is none, we use date */
					have_info = false;

					while (isdigit(*pos)) {
						have_info = true;
						GET_NUMBER(CAL_REPEAT_DAYOFYEAR, 0);
						NEXT_NOSPACE(0);
					}

					if (!have_info) {
#if 0
						/*
						 * This seems to be according to specification,
						 * however several vendors (Siemens, some web based
						 * calendars use YD1 for simple year repeating. So
						 * we handle this as YM1 just to be compatbile with
						 * those.
						 */
						Calendar->Entries[Calendar->EntriesNum].EntryType = CAL_REPEAT_DAYOFYEAR;
						Calendar->Entries[Calendar->EntriesNum].Number =
							GetDayOfYear(
								Calendar->Entries[TimePos].Date.Year,
								Calendar->Entries[TimePos].Date.Month,
								Calendar->Entries[TimePos].Date.Day);
						Calendar->EntriesNum++;
#endif
						Calendar->Entries[Calendar->EntriesNum].EntryType = CAL_REPEAT_DAY;
						Calendar->Entries[Calendar->EntriesNum].Number =
							Calendar->Entries[TimePos].Date.Day;
						Calendar->EntriesNum++;

						Calendar->Entries[Calendar->EntriesNum].EntryType = CAL_REPEAT_MONTH;
						Calendar->Entries[Calendar->EntriesNum].Number =
							Calendar->Entries[TimePos].Date.Month;
						Calendar->EntriesNum++;
					}
					break;
				default:
					dbgprintf("Could not decode recurrency: %s\n", pos);
					return ERR_UNKNOWN;
			}
			break;
		default:
			dbgprintf("Could not decode recurrency: %s\n", pos);
			return ERR_UNKNOWN;
	}

	/* Go to duration */
	NEXT_NOSPACE(1);

	/* Do we have duration encoded? */
	if (*pos == '#') {
		pos++;
		if (*pos == 0) return ERR_UNKNOWN;
		GET_NUMBER(CAL_REPEAT_COUNT, 0);
	}

	/* Go to end date */
	NEXT_NOSPACE(1);

	/* Do we have end date encoded? */
	if (ReadVCALDateTime(pos, &(Calendar->Entries[Calendar->EntriesNum].Date))) {
		Calendar->Entries[Calendar->EntriesNum].EntryType = CAL_REPEAT_STOPDATE;
		Calendar->EntriesNum++;
	}

	return ERR_NONE;
}

GSM_Error GSM_DecodeVCALENDAR_VTODO(unsigned char *Buffer, int *Pos, GSM_CalendarEntry *Calendar,
					GSM_ToDoEntry *ToDo, GSM_VCalendarVersion CalVer, GSM_VToDoVersion ToDoVer)
{
	unsigned char 	Line[2000],Buff[2000];
	int		Level = 0;
	GSM_DateTime	Date;
	GSM_DeltaTime	OneHour = {
		.Timezone = 0,
		.Second = 0,
		.Minute = 0,
		.Hour = 1,
		.Day = 0,
		.Month = 0,
		.Year = 0};
	GSM_TimeUnit	unit = GSM_TimeUnit_Unknown;
	GSM_DeltaTime	trigger;
	GSM_Error	error;
	int		deltatime = 0;
	int		dstflag = 0;
	bool		is_date_only;
	bool		date_only = false;
	int		lBuffer;
 	int 		Text=-1, Time=-1, Alarm=-1, EndTime=-1, Location=-1;
	char		*rrule = NULL;

	if (!Buffer) return ERR_EMPTY;

	Calendar->EntriesNum 	= 0;
	ToDo->EntriesNum 	= 0;
	lBuffer = strlen(Buffer);
	trigger.Timezone = -999;

	if (CalVer == Mozilla_VCalendar && *Pos ==0) {
		int error;
		error = GSM_Make_VCAL_Lines (Buffer, &lBuffer);
		if (error != ERR_NONE) return error;
	}

	while (1) {
		MyGetLine(Buffer, Pos, Line, lBuffer, true);
		if (strlen(Line) == 0) break;

		switch (Level) {
		case 0:
			if (strstr(Line,"BEGIN:VEVENT")) {
				Calendar->Type = -1;
				date_only = true;
				dstflag = 0;
				Text=-1; Time=-1; Alarm=-1; EndTime=-1; Location=-1;
				Level 		= 1;
			}
			if (strstr(Line,"BEGIN:VTODO")) {
				ToDo->Priority 	= GSM_Priority_None;
				dstflag = 0;
				Text=-1; Time=-1; Alarm=-1; EndTime=-1; Location=-1;
				Level 		= 2;
			}
			break;
		case 1: /* Calendar note */
			if (strstr(Line,"END:VEVENT")) {
				if (Time == -1) return ERR_UNKNOWN;
				if (rrule != NULL) {
					error = GSM_DecodeVCAL_RRULE(rrule, Calendar, Time);
					free(rrule);
					if (error != ERR_NONE) {
						return error;
					}
				}
				if (Calendar->EntriesNum == 0) return ERR_EMPTY;

				if (trigger.Timezone != -999) {
					Alarm = Calendar->EntriesNum;
					Calendar->Entries[Alarm].Date = GSM_AddTime (Calendar->Entries[Time].Date, trigger);
					Calendar->Entries[Alarm].EntryType = CAL_TONE_ALARM_DATETIME;
					Calendar->EntriesNum++;
				}

				if (dstflag != 0) {
					/*
					 * Day saving time was active while entry was created,
					 * add one hour to adjust it.
					 */
					if (dstflag == 4) {
						GSM_Calendar_AdjustDate(Calendar, &OneHour);
						dbgprintf("Adjusting DST: %i\n", dstflag);
					} else {
						dbgprintf("Unknown DST flag: %i\n", dstflag);
					}
				}

				/* If event type is undefined choose appropriate type. Memos carry dates only, no times.
				   Use Meetings for events with full date+time settings. */
				if (Calendar->Type == -1) {
					if (date_only)
						Calendar->Type = GSM_CAL_MEMO;
					else
						Calendar->Type = GSM_CAL_MEETING;
				}

				return ERR_NONE;
			}

			/* Read Mozilla calendar entries. Some of them will not be used here. Notably alarm time
			   can defined in several ways. We will use the trigger value only since this is the value
			   Mozilla calendar uses when importing ics-files. */
			if (strncmp(Line, "UID:", 4) == 0) {
				ReadVCALText(Line, "UID", Buff);  // Any use for UIDs?
				break;
			}
			if (strstr(Line,"X-MOZILLA-ALARM-DEFAULT-UNITS:")) {
				if (ReadVCALText(Line, "X-MOZILLA-ALARM-DEFAULT-UNITS", Buff)) {
					unit = ReadVCALTimeUnits(DecodeUnicodeString(Buff));
					break;
				}
			}
			if (strstr(Line,"X-MOZILLA-ALARM-DEFAULT-LENGTH:")) {
				if (ReadVCALInt(Line, "X-MOZILLA-ALARM-DEFAULT-LENGTH", &deltatime)) {
					break;
				}
			}

			if (strstr(Line,"BEGIN:VALARM")) {
				MyGetLine(Buffer, Pos, Line, lBuffer, true);
				if (strlen(Line) == 0) break;
				if (ReadVCALText(Line, "TRIGGER;VALUE=DURATION", Buff)) {
					trigger = ReadVCALTriggerTime(DecodeUnicodeString(Buff));
					break;
				}
			}

			/* Event type. Must be set correctly to let phone calendar work as expected. For example
			   without GSM_CAL_MEETING the time part of an event date/time will be dropped. */
			if (strstr(Line,"CATEGORIES:")) {
				GSM_Translate_Category(TRANSL_TO_GSM, Line + 11, &Calendar->Type);
				break;
			}

			if (strstr(Line,"RRULE:")) {
				if (rrule == NULL) {
					rrule = strdup(Line + 6);
				} else {
					dbgprintf("Ignoring second recurrence: %s\n", Line);
				}
				break;
			}

			if ((ReadVCALText(Line, "SUMMARY", Buff))) {
				Calendar->Entries[Calendar->EntriesNum].EntryType = CAL_TEXT;
				CopyUnicodeString(Calendar->Entries[Calendar->EntriesNum].Text,
					DecodeUnicodeSpecialChars(Buff));
				Text = Calendar->EntriesNum;
				Calendar->EntriesNum++;
			}
			if ((ReadVCALText(Line, "DESCRIPTION", Buff))) {
				CopyUnicodeString(Buff,DecodeUnicodeSpecialChars(Buff));
				Calendar->Entries[Calendar->EntriesNum].EntryType = CAL_DESCRIPTION;
				CopyUnicodeString(Calendar->Entries[Calendar->EntriesNum].Text,
					DecodeUnicodeSpecialChars(Buff));
				Calendar->EntriesNum++;
			}
			if (ReadVCALText(Line, "LOCATION", Buff)) {
				Calendar->Entries[Calendar->EntriesNum].EntryType = CAL_LOCATION;
				CopyUnicodeString(Calendar->Entries[Calendar->EntriesNum].Text,
					DecodeUnicodeSpecialChars(Buff));
				Location = Calendar->EntriesNum;
				Calendar->EntriesNum++;
			}
			if ((ReadVCALText(Line, "X-IRMC-LUID", Buff))) {
				Calendar->Entries[Calendar->EntriesNum].EntryType = CAL_LUID;
				CopyUnicodeString(Calendar->Entries[Calendar->EntriesNum].Text,
					DecodeUnicodeSpecialChars(Buff));
				Calendar->EntriesNum++;
			}
			if ((ReadVCALText(Line, "CLASS", Buff))) {
				Calendar->Entries[Calendar->EntriesNum].EntryType = CAL_PRIVATE;
				if (mywstrncasecmp(Buff, "\0P\0U\0B\0L\0I\0C\0\0", 0)) {
					Calendar->Entries[Calendar->EntriesNum].Number = 0;
				} else {
					Calendar->Entries[Calendar->EntriesNum].Number = 1;
				}
				Calendar->EntriesNum++;
			}
			if (ReadVCALDate(Line, "DTSTART", &Date, &is_date_only)) {
				Calendar->Entries[Calendar->EntriesNum].Date = Date;
				Calendar->Entries[Calendar->EntriesNum].EntryType = CAL_START_DATETIME;
				Time = Calendar->EntriesNum;
				Calendar->EntriesNum++;
				if (!is_date_only) date_only = false;
			}
			if (ReadVCALDate(Line, "DTEND", &Date, &is_date_only)) {
				Calendar->Entries[Calendar->EntriesNum].Date = Date;
				Calendar->Entries[Calendar->EntriesNum].EntryType = CAL_END_DATETIME;
				EndTime = Calendar->EntriesNum;
				Calendar->EntriesNum++;
				if (!is_date_only) date_only = false;
			}
			if (ReadVCALDate(Line, "DALARM", &Date, &is_date_only)) {
				Calendar->Entries[Calendar->EntriesNum].Date = Date;
				if (CalVer == Siemens_VCalendar) {
					Calendar->Entries[Calendar->EntriesNum].EntryType = CAL_TONE_ALARM_DATETIME;
				} else {
					Calendar->Entries[Calendar->EntriesNum].EntryType = CAL_SILENT_ALARM_DATETIME;
				}
				Alarm = Calendar->EntriesNum;
				Calendar->EntriesNum++;
			}
			if (ReadVCALDate(Line, "AALARM", &Date, &is_date_only)) {
				Calendar->Entries[Calendar->EntriesNum].Date = Date;
				Calendar->Entries[Calendar->EntriesNum].EntryType = CAL_TONE_ALARM_DATETIME;
				Alarm = Calendar->EntriesNum;
				Calendar->EntriesNum++;
			}
			if (strstr(Line,"X-SONYERICSSON-DST:")) {
				if (ReadVCALInt(Line, "X-SONYERICSSON-DST", &dstflag)) {
					break;
				}
			}
			break;

		case 2: /* ToDo note */
			if (strstr(Line,"END:VTODO")) {
				if (ToDo->EntriesNum == 0) return ERR_EMPTY;

				if (dstflag != 0) {
					/*
					 * Day saving time was active while entry was created,
					 * add one hour to adjust it.
					 */
					if (dstflag == 4) {
						GSM_ToDo_AdjustDate(ToDo, &OneHour);
						dbgprintf("Adjusting DST: %i\n", dstflag);
					} else {
						dbgprintf("Unknown DST flag: %i\n", dstflag);
					}
				}

				return ERR_NONE;
			}

			if (strstr(Line,"CATEGORIES:")) {
				GSM_Translate_Category(TRANSL_TO_GSM, Line+11, &ToDo->Type);
			}

			if (strncmp(Line, "UID:", 4) == 0) {
				ReadVCALText(Line, "UID", Buff);  // Any use for UIDs?
				break;
			}
			if (strstr(Line,"X-MOZILLA-ALARM-DEFAULT-UNITS:")) {
				if (ReadVCALText(Line, "X-MOZILLA-ALARM-DEFAULT-UNITS", Buff)) {
					unit = ReadVCALTimeUnits(DecodeUnicodeString(Buff));
					break;
				}
			}
			if (strstr(Line,"X-MOZILLA-ALARM-DEFAULT-LENGTH:")) {
				if (ReadVCALInt(Line, "X-MOZILLA-ALARM-DEFAULT-LENGTH", &deltatime)) {
					break;
				}
			}
			if (strstr(Line,"X-SONYERICSSON-DST:")) {
				if (ReadVCALInt(Line, "X-SONYERICSSON-DST", &dstflag)) {
					break;
				}
			}

			if (ReadVCALDate(Line, "DUE", &Date, &is_date_only)) {
				if (ToDo->Entries[ToDo->EntriesNum].Date.Year   != 2037	&&
				    ToDo->Entries[ToDo->EntriesNum].Date.Month  != 12	&&
				    ToDo->Entries[ToDo->EntriesNum].Date.Day    != 31	&&
				    ToDo->Entries[ToDo->EntriesNum].Date.Hour   != 23	&&
				    ToDo->Entries[ToDo->EntriesNum].Date.Minute != 59 ) {
					ToDo->Entries[ToDo->EntriesNum].Date = Date;
					ToDo->Entries[ToDo->EntriesNum].EntryType = TODO_END_DATETIME;
					EndTime = Calendar->EntriesNum;
					ToDo->EntriesNum++;
				}
			}
			if (ReadVCALDate(Line, "DALARM", &Date, &is_date_only)) {
				ToDo->Entries[ToDo->EntriesNum].Date = Date;
				ToDo->Entries[ToDo->EntriesNum].EntryType = TODO_SILENT_ALARM_DATETIME;
				Alarm = Calendar->EntriesNum;
				ToDo->EntriesNum++;
			}
			if (ReadVCALDate(Line, "AALARM", &Date, &is_date_only)) {
				ToDo->Entries[ToDo->EntriesNum].Date = Date;
				ToDo->Entries[ToDo->EntriesNum].EntryType = TODO_ALARM_DATETIME;
				Alarm = Calendar->EntriesNum;
				ToDo->EntriesNum++;
			}

			if ((ReadVCALText(Line, "SUMMARY", Buff))) {
				ToDo->Entries[ToDo->EntriesNum].EntryType = TODO_TEXT;
				CopyUnicodeString(ToDo->Entries[ToDo->EntriesNum].Text,
					DecodeUnicodeSpecialChars(Buff));
				Text = ToDo->EntriesNum;
				ToDo->EntriesNum++;
			}
			if ((ReadVCALText(Line, "DESCRIPTION", Buff))) {
				ToDo->Entries[ToDo->EntriesNum].EntryType = TODO_DESCRIPTION;
				CopyUnicodeString(ToDo->Entries[ToDo->EntriesNum].Text,
					DecodeUnicodeSpecialChars(Buff));
				Text = ToDo->EntriesNum;
				ToDo->EntriesNum++;
			}
			if ((ReadVCALText(Line, "LOCATION", Buff))) {
				ToDo->Entries[ToDo->EntriesNum].EntryType = TODO_LOCATION;
				CopyUnicodeString(ToDo->Entries[ToDo->EntriesNum].Text,
					DecodeUnicodeSpecialChars(Buff));
				Text = ToDo->EntriesNum;
				ToDo->EntriesNum++;
			}
			if (ReadVCALText(Line, "PRIORITY", Buff)) {
				if (atoi(DecodeUnicodeString(Buff))==3) ToDo->Priority = GSM_Priority_Low;
				else if (atoi(DecodeUnicodeString(Buff))==2) ToDo->Priority = GSM_Priority_Medium;
				else if (atoi(DecodeUnicodeString(Buff))==1) ToDo->Priority = GSM_Priority_High;
				else ToDo->Priority = GSM_Priority_None;
			}
			if (strstr(Line,"STATUS:COMPLETED")) {
				ToDo->Entries[ToDo->EntriesNum].EntryType = TODO_COMPLETED;
				ToDo->Entries[ToDo->EntriesNum].Number	  = 1;
				ToDo->EntriesNum++;
			}
			if ((ReadVCALText(Line, "X-IRMC-LUID", Buff))) {
				ToDo->Entries[ToDo->EntriesNum].EntryType = TODO_LUID;
				CopyUnicodeString(ToDo->Entries[ToDo->EntriesNum].Text,
					DecodeUnicodeSpecialChars(Buff));
				ToDo->EntriesNum++;
			}
			if ((ReadVCALText(Line, "CLASS", Buff))) {
				ToDo->Entries[ToDo->EntriesNum].EntryType = TODO_PRIVATE;
				if (mywstrncasecmp(Buff, "\0P\0U\0B\0L\0I\0C\0", 0)) {
					ToDo->Entries[ToDo->EntriesNum].Number = 0;
				} else {
					ToDo->Entries[ToDo->EntriesNum].Number = 1;
				}
				ToDo->EntriesNum++;
			}
			break;
		}
	}

	if (Calendar->EntriesNum == 0 && ToDo->EntriesNum == 0) return ERR_EMPTY;
	return ERR_NONE;
}

GSM_Error GSM_EncodeVNTFile(unsigned char *Buffer, int *Length, GSM_NoteEntry *Note)
{
	*Length+=sprintf(Buffer+(*Length), "BEGIN:VNOTE%c%c",13,10);
	*Length+=sprintf(Buffer+(*Length), "VERSION:1.1%c%c",13,10);
	SaveVCALText(Buffer, Length, Note->Text, "BODY");
	*Length+=sprintf(Buffer+(*Length), "END:VNOTE%c%c",13,10);

	return ERR_NONE;
}
/*@}*/

/* How should editor hadle tabs in this file? Add editor commands here.
 * vim: noexpandtab sw=8 ts=8 sts=8:
 */
