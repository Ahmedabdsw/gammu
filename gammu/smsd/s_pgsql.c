/* (c) 2006 by Andrea Riciputi */

#include <gammu.h>

#ifdef HAVE_POSTGRESQL_LIBPQ_FE_H

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#ifdef WIN32
#  include <windows.h>
#ifndef __GNUC__
#  pragma comment(lib, "libmysql.lib")
#endif
#endif

#include "../gammu.h"
#include "smsdcore.h"
#include "../../common/misc/locales.h"

/* Connects to database */
static GSM_Error SMSDPgSQL_Init(GSM_SMSDConfig *Config)
{
	unsigned char		buf[400];
	PGresult 		*Res;

	unsigned int port = 0;
	char * pport;

	pport = strstr( Config->PC, ":" );
	if (pport) {
		*pport ++ = '\0';
		port = atoi( pport );
	}

	sprintf(buf, "host = '%s' user = '%s' password = '%s' dbname = '%s' port = %d", \
		Config->PC, Config->user, Config->password, Config->database, port);

	Config->DBConnPgSQL = PQconnectdb(buf);
	if (PQstatus(Config->DBConnPgSQL) != CONNECTION_OK) {
	  WriteSMSDLog(_("Error connecting to database: %s\n"), PQerrorMessage(Config->DBConnPgSQL));
	  PQfinish(Config->DBConnPgSQL);
	  return ERR_UNKNOWN;
	}

	sprintf(buf, "SELECT id FROM outbox WHERE TRUE");
	Res = PQexec(Config->DBConnPgSQL, buf);
	if ((!Res) || (PQresultStatus(Res) != PGRES_TUPLES_OK)) {
		WriteSMSDLog(_("No table for outbox sms: %s\n"), PQresultErrorMessage(Res));
		PQclear(Res);
		PQfinish(Config->DBConnPgSQL);
		return ERR_UNKNOWN;
	}
	PQclear(Res);

	sprintf(buf, "SELECT id FROM outbox_multipart WHERE TRUE");
	Res = PQexec(Config->DBConnPgSQL, buf);
	if ((!Res) || (PQresultStatus(Res) != PGRES_TUPLES_OK)) {
		WriteSMSDLog(_("No table for outbox sms: %s\n"), PQresultErrorMessage(Res));
		PQclear(Res);
		PQfinish(Config->DBConnPgSQL);
		return ERR_UNKNOWN;
	}
	PQclear(Res);

	sprintf(buf, "SELECT id FROM sentitems WHERE TRUE");
	Res = PQexec(Config->DBConnPgSQL, buf);
	if ((!Res) || (PQresultStatus(Res) != PGRES_TUPLES_OK)) {
		WriteSMSDLog(_("No table for sent sms: %s\n"), PQresultErrorMessage(Res));
		PQclear(Res);
		PQfinish(Config->DBConnPgSQL);
		return ERR_UNKNOWN;
	}
	PQclear(Res);

	sprintf(buf, "SELECT id FROM inbox WHERE TRUE");
	Res = PQexec(Config->DBConnPgSQL, buf);
	if ((!Res) || (PQresultStatus(Res) != PGRES_TUPLES_OK)) {
		WriteSMSDLog(_("No table for inbox sms: %s\n"), PQresultErrorMessage(Res));
		PQclear(Res);
		PQfinish(Config->DBConnPgSQL);
		return ERR_UNKNOWN;
	}
	PQclear(Res);

	sprintf(buf, "SELECT Version FROM gammu WHERE TRUE");
	Res = PQexec(Config->DBConnPgSQL, buf);
	if ((!Res) || (PQresultStatus(Res) != PGRES_TUPLES_OK)) {
		WriteSMSDLog(_("No Gammu table: %s\n"), PQresultErrorMessage(Res));
		PQclear(Res);
		PQfinish(Config->DBConnPgSQL);
		return ERR_UNKNOWN;
	}
	if (PQntuples(Res) == 0) {
	  WriteSMSDLog(_("No version info in Gammu table: %s\n"), PQresultErrorMessage(Res));
	  PQclear(Res);
	  PQfinish(Config->DBConnPgSQL);
	  return ERR_UNKNOWN;
	}
	if (atoi(PQgetvalue(Res, 0, 0)) > 7) {
	  WriteSMSDLog(_("DataBase structures are from higher Gammu version"));
	  WriteSMSDLog(_("Please update this client application"));
	  PQclear(Res);
	  PQfinish(Config->DBConnPgSQL);
	  return ERR_UNKNOWN;
	}
	if (atoi(PQgetvalue(Res, 0, 0)) < 7) {
	  //	if (atoi(Row[0]) < 7) {
		WriteSMSDLog(_("DataBase structures are from older Gammu version"));
		WriteSMSDLog(_("Please update DataBase, if you want to use this client application"));
		PQclear(Res);
		PQfinish(Config->DBConnPgSQL);
		return ERR_UNKNOWN;
	}
	PQclear(Res);

	Res = PQexec(Config->DBConnPgSQL,"SET NAMES UTF8");
	PQclear(Res);

	return ERR_NONE;
}

static GSM_Error SMSDPgSQL_InitAfterConnect(GSM_SMSDConfig *Config)
{
	unsigned char buf[400],buf2[200];

	PGresult *Res;

	sprintf(buf,"DELETE FROM phones WHERE IMEI = '%s'",s.Phone.Data.IMEI);
	dbgprintf("%s\n",buf);
	Res = PQexec(Config->DBConnPgSQL, buf);
	if ((!Res) || (PQresultStatus(Res) != PGRES_COMMAND_OK)) {
	  WriteSMSDLog(_("Error deleting from database (Init): %s\n"), PQresultErrorMessage(Res));
	  PQclear(Res);
	  PQfinish(Config->DBConnPgSQL);
	  return ERR_UNKNOWN;
	}
	PQclear(Res);

	sprintf(buf2,"Gammu %s",VERSION);
	if (strlen(GetOS()) != 0) {
		strcat(buf2+strlen(buf2),", ");
		strcat(buf2+strlen(buf2),GetOS());
	}
	if (strlen(GetCompiler()) != 0) {
		strcat(buf2+strlen(buf2),", ");
		strcat(buf2+strlen(buf2),GetCompiler());
	}

	sprintf(buf, "INSERT INTO phones (IMEI, ID, Send, Receive, InsertIntoDB, TimeOut, Client) VALUES ('%s', '%s', 'yes', 'yes', now(), now() + interval '10 seconds', '%s')", s.Phone.Data.IMEI,Config->PhoneID, buf2);
	dbgprintf("%s\n",buf);
	Res = PQexec(Config->DBConnPgSQL, buf);
	if ((!Res) || (PQresultStatus(Res) != PGRES_COMMAND_OK)) {
	  WriteSMSDLog(_("Error inserting into database (Init): %s\n"), PQresultErrorMessage(Res));
	  PQclear(Res);
	  PQfinish(Config->DBConnPgSQL);
	  return ERR_UNKNOWN;
	}
	PQclear(Res);

	return ERR_NONE;
}

/* Save SMS from phone (called Inbox sms - it's in phone Inbox) somewhere */
static GSM_Error SMSDPgSQL_SaveInboxSMS(GSM_MultiSMSMessage sms, GSM_SMSDConfig *Config)
{
	PGresult 		*Res = NULL;

	unsigned char		buffer[10000],buffer2[400],buffer3[50],buffer4[800];
	int 			i,j;
	int                     numb_rows;
	GSM_DateTime		DT;
	time_t     		t_time1,t_time2;
	bool			found;
	long 			diff;

	for (i=0; i<sms.Number; i++) {
		if (sms.SMS[i].PDU == SMS_Status_Report) {
			strcpy(buffer2, DecodeUnicodeString(sms.SMS[i].Number));
			if (strncasecmp(Config->deliveryreport, "log", 3) == 0) {
				WriteSMSDLog(_("Delivery report: %s to %s"), DecodeUnicodeString(sms.SMS[i].Text), buffer2);
			}

			sprintf(buffer, "SELECT ID, Status, SendingDateTime, DeliveryDateTime, SMSCNumber \
                                        FROM sentitems WHERE \
					DeliveryDateTime = 'epoch' AND \
					SenderID = '%s' AND TPMR = '%i' AND DestinationNumber = '%s'",
					Config->PhoneID, sms.SMS[i].MessageReference, buffer2);
			dbgprintf("%s\n",buffer);

			Res = PQexec(Config->DBConnPgSQL, buffer);
			if ((!Res) || (PQresultStatus(Res) != PGRES_TUPLES_OK)) {
			  WriteSMSDLog(_("Error reading from database (SaveInbox): %s %s\n"), \
				       buffer, PQresultErrorMessage(Res));
			  PQclear(Res);
			  PQfinish(Config->DBConnPgSQL);
			  return ERR_UNKNOWN;
			}

			found = false;
			numb_rows = PQntuples(Res);
			for(j = 0; j < numb_rows; j++) {

			  if (strcmp(PQgetvalue(Res, j, 4), DecodeUnicodeString(sms.SMS[i].SMSC.Number))) {
			    if (Config->skipsmscnumber[0] == 0) continue;
			    if (strcmp(Config->skipsmscnumber, PQgetvalue(Res, j, 4))) continue;
			  }

			  if (!strcmp(PQgetvalue(Res, j, 1), "SendingOK") || \
			      !strcmp(PQgetvalue(Res, j, 1), "DeliveryPending")) {
			    char *time_stamp = PQgetvalue(Res, j, 2);
			    sprintf(buffer,"%c%c%c%c",time_stamp[0],time_stamp[1],time_stamp[2],time_stamp[3]);
			    DT.Year = atoi(buffer);

			    sprintf(buffer,"%c%c",time_stamp[5],time_stamp[6]);
			    DT.Month = atoi(buffer);

			    sprintf(buffer,"%c%c",time_stamp[8],time_stamp[9]);
			    DT.Day = atoi(buffer);

			    sprintf(buffer,"%c%c",time_stamp[11],time_stamp[12]);
			    DT.Hour = atoi(buffer);

			    sprintf(buffer,"%c%c",time_stamp[14],time_stamp[15]);
			    DT.Minute = atoi(buffer);

			    sprintf(buffer,"%c%c",time_stamp[17],time_stamp[18]);
			    DT.Second = atoi(buffer);

			    t_time1 = Fill_Time_T(DT);
			    t_time2 = Fill_Time_T(sms.SMS[i].DateTime);
			    diff = t_time2 - t_time1;
			    /*fprintf(stderr,"diff is %i, %i-%i-%i-%i-%i and %i-%i-%i-%i-%i-%i\n",diff,	\
			    DT.Year,DT.Month,DT.Day,DT.Hour,DT.Minute,DT.Second,\
			    sms.SMS[i].DateTime.Year,sms.SMS[i].DateTime.Month,sms.SMS[i].DateTime.Day,\
			    sms.SMS[i].DateTime.Hour,sms.SMS[i].DateTime.Minute,sms.SMS[i].DateTime.Second); */

			    if (diff > -10 && diff < 10) {
			      found = true;
			      break;
			    }
			  }
			}

			if (found) {
			  sprintf(buffer,"UPDATE sentitems SET DeliveryDateTime = '%04i%02i%02i%02i%02i%02i', \
                                          Status = '",\
				  sms.SMS[i].SMSCTime.Year, sms.SMS[i].SMSCTime.Month, sms.SMS[i].SMSCTime.Day,\
				  sms.SMS[i].SMSCTime.Hour, sms.SMS[i].SMSCTime.Minute,\
				  sms.SMS[i].SMSCTime.Second);

			  sprintf(buffer3, "%s", DecodeUnicodeString(sms.SMS[i].Text));
			  if (!strcmp(buffer3, "Delivered")) {
			    sprintf(buffer+strlen(buffer),"DeliveryOK");
			  } else if (!strcmp(buffer3, "Failed")) {
			    sprintf(buffer+strlen(buffer), "DeliveryFailed");
			  } else if (!strcmp(buffer3, "Pending")) {
			    sprintf(buffer+strlen(buffer), "DeliveryPending");
			  } else if (!strcmp(buffer3, "Unknown")) {
			    sprintf(buffer+strlen(buffer), "DeliveryUnknown");
			  }

			  sprintf(buffer+strlen(buffer),"', StatusError = '%i'", sms.SMS[i].DeliveryStatus);
			  sprintf(buffer+strlen(buffer)," WHERE ID = '%s' AND `TPMR` = '%i'",\
				  PQgetvalue(Res, j, 0), sms.SMS[i].MessageReference);
			  dbgprintf("%s\n",buffer);

			  Res = PQexec(Config->DBConnPgSQL, buffer);
			  if ((!Res) || (PQresultStatus(Res) != PGRES_TUPLES_OK)) {
			    WriteSMSDLog(_("Error writing to database (SaveInboxSMS): %s\n"),\
					 PQresultErrorMessage(Res));
			    PQclear(Res);
			    PQfinish(Config->DBConnPgSQL);
			    return ERR_UNKNOWN;
			  }
			}
			PQclear(Res);
			continue;
		}

		if (sms.SMS[i].PDU != SMS_Deliver) continue;
		buffer[0]=0;
		sprintf(buffer+strlen(buffer),"INSERT INTO inbox \
			(ReceivingDateTime, Text, SenderNumber, Coding, SMSCNumber, UDH, \
			Class, TextDecoded, RecipientID) VALUES ('%04d-%02d-%02d %02d:%02d:%02d', '",\
			sms.SMS[i].DateTime.Year, sms.SMS[i].DateTime.Month, sms.SMS[i].DateTime.Day,\
			sms.SMS[i].DateTime.Hour, sms.SMS[i].DateTime.Minute, sms.SMS[i].DateTime.Second);

		switch (sms.SMS[i].Coding) {
		case SMS_Coding_Unicode_No_Compression:

		case SMS_Coding_Default_No_Compression:
			EncodeHexUnicode(buffer+strlen(buffer), sms.SMS[i].Text, UnicodeLength(sms.SMS[i].Text));
			break;

		case SMS_Coding_8bit:
			EncodeHexBin(buffer+strlen(buffer), sms.SMS[i].Text, sms.SMS[i].Length);

		default:
			break;
		}

		sprintf(buffer+strlen(buffer), "','%s','", DecodeUnicodeString(sms.SMS[i].Number));

		switch (sms.SMS[i].Coding) {
		case SMS_Coding_Unicode_No_Compression:
			sprintf(buffer+strlen(buffer), "Unicode_No_Compression");
			break;

		case SMS_Coding_Unicode_Compression:
			sprintf(buffer+strlen(buffer), "Unicode_Compression");
			break;

	    	case SMS_Coding_Default_No_Compression:
			sprintf(buffer+strlen(buffer), "Default_No_Compression");
			break;

	    	case SMS_Coding_Default_Compression:
			sprintf(buffer+strlen(buffer), "Default_Compression");
			break;

		case SMS_Coding_8bit:
			sprintf(buffer+strlen(buffer), "8bit");
			break;
		}

		sprintf(buffer+strlen(buffer), "','%s'", DecodeUnicodeString(sms.SMS[i].SMSC.Number));

		if (sms.SMS[i].UDH.Type == UDH_NoUDH) {
			sprintf(buffer+strlen(buffer), ",''");
		} else {
			sprintf(buffer+strlen(buffer), ",'");
			EncodeHexBin(buffer+strlen(buffer), sms.SMS[i].UDH.Text, sms.SMS[i].UDH.Length);
			sprintf(buffer+strlen(buffer), "'");
		}

		sprintf(buffer+strlen(buffer), ",'%i','", sms.SMS[i].Class);

		switch (sms.SMS[i].Coding) {
		  int error;

		case SMS_Coding_Unicode_No_Compression:

	    	case SMS_Coding_Default_No_Compression:
			EncodeUTF8(buffer2, sms.SMS[i].Text);
			PQescapeStringConn(Config->DBConnPgSQL, buffer4, buffer2, strlen(buffer2), &error);
			memcpy(buffer+strlen(buffer), buffer4, strlen(buffer4)+1);
			break;

		case SMS_Coding_8bit:
			break;

		default:
			break;
		}

		sprintf(buffer+strlen(buffer), "','%s')", Config->PhoneID);
		dbgprintf("%s\n",buffer);

		Res = PQexec(Config->DBConnPgSQL, buffer);
		if ((!Res) || (PQresultStatus(Res) != PGRES_COMMAND_OK)) {
		  WriteSMSDLog(_("Error writing to database (SaveInbox): %s\n"), PQresultErrorMessage(Res));
		  PQclear(Res);
		  PQfinish(Config->DBConnPgSQL);
		  return ERR_UNKNOWN;
		}
	}
	PQclear(Res);

	return ERR_NONE;
}

static GSM_Error SMSDPgSQL_RefreshSendStatus(GSM_SMSDConfig *Config, unsigned char *ID)
{
	unsigned char buffer[10000];
	PGresult *Res;

	sprintf(buffer,"UPDATE outbox SET SendingTimeOut = now() + INTERVAL '15 seconds' \
                        WHERE ID = '%s' AND SendingTimeOut < now()", ID);
	dbgprintf("%s\n",buffer);

	Res = PQexec(Config->DBConnPgSQL, buffer);
	if ((!Res) || (PQresultStatus(Res) != PGRES_COMMAND_OK)) {
	  WriteSMSDLog(_("Error writing to database (RefreshSendStatus): %s\n"), PQresultErrorMessage(Res));
	  PQclear(Res);
	  PQfinish(Config->DBConnPgSQL);
	  return ERR_UNKNOWN;
	}

	if (atoi(PQcmdTuples(Res)) == 0) {
	  PQclear(Res);
	  return ERR_UNKNOWN;
	}

	PQclear(Res);
  	return ERR_NONE;
}

/* Find one multi SMS to sending and return it (or return ERR_EMPTY)
 * There is also set ID for SMS
 */
static GSM_Error SMSDPgSQL_FindOutboxSMS(GSM_MultiSMSMessage *sms, GSM_SMSDConfig *Config, unsigned char *ID)
{
	unsigned char 		buf[400];
	PGresult 		*Res;
	int 			i,j;
	int                     numb_rows;
	bool			found = false;

	sprintf(buf, "SELECT ID, InsertIntoDB, SendingDateTime, SenderID FROM outbox \
                      WHERE SendingDateTime < now() AND SendingTimeOut < now()");
	dbgprintf("%s\n", buf);

	Res = PQexec(Config->DBConnPgSQL, buf);
	if ((!Res) || (PQresultStatus(Res) != PGRES_TUPLES_OK)) {
	  WriteSMSDLog(_("Error reading from database (FindOutbox): %s\n"), PQresultErrorMessage(Res));
	  PQclear(Res);
	  PQfinish(Config->DBConnPgSQL);
	  return ERR_UNKNOWN;
	}

	numb_rows = PQntuples(Res);
	for(j = 0; j < numb_rows; j++) {
	  sprintf(ID, "%s", PQgetvalue(Res, j, 0));
	  sprintf(Config->DT, "%s", PQgetvalue(Res, j, 1));

	  if (PQgetvalue(Res, j, 3) == NULL || strlen(PQgetvalue(Res, j, 3)) == 0 ||\
	      !strcmp(PQgetvalue(Res, j, 3), Config->PhoneID)) {

	    if (SMSDPgSQL_RefreshSendStatus(Config, ID) == ERR_NONE) {
	      found = true;
	      break;
	    }
	  }
	}

	if (!found) {
	  PQclear(Res);
	  return ERR_EMPTY;
	}
	PQclear(Res);

	sms->Number = 0;
	for (i = 0; i < MAX_MULTI_SMS; i++) {
	  GSM_SetDefaultSMSData(&sms->SMS[i]);
	  sms->SMS[i].SMSC.Number[0] = 0;
	  sms->SMS[i].SMSC.Number[1] = 0;
	}

	for (i = 1; i < MAX_MULTI_SMS+1; i++) {
	  if (i == 1) {
	    sprintf(buf, "SELECT Text, Coding, UDH, Class, TextDecoded, ID, DestinationNumber, MultiPart, \
                          RelativeValidity, DeliveryReport, CreatorID FROM outbox WHERE ID='%s'", ID);
	  } else {
	    sprintf(buf, "SELECT Text, Coding, UDH, Class, TextDecoded, ID, SequencePosition \
                          FROM outbox_multipart WHERE ID='%s' AND SequencePosition='%i'", ID, i);
	  }
	  dbgprintf("%s\n", buf);

	Res = PQexec(Config->DBConnPgSQL, buf);
	if ((!Res) || (PQresultStatus(Res) != PGRES_TUPLES_OK)) {
	  WriteSMSDLog(_("Error reading from database (FindOutbox): %s\n"),  PQresultErrorMessage(Res));
	  PQclear(Res);
	  PQfinish(Config->DBConnPgSQL);
	  return ERR_UNKNOWN;
	}

	numb_rows = PQntuples(Res);
	if (numb_rows == 0) {
	  PQclear(Res);
	  return ERR_NONE;
	}

	sms->SMS[sms->Number].Coding=SMS_Coding_8bit;
	if (!strcmp(PQgetvalue(Res, 0, 1), "Unicode_No_Compression")) {
	  sms->SMS[sms->Number].Coding = SMS_Coding_Unicode_No_Compression;
	}
	if (!strcmp(PQgetvalue(Res, 0, 1), "Default_No_Compression")) {
	  sms->SMS[sms->Number].Coding=SMS_Coding_Default_No_Compression;
	}

	if (PQgetvalue(Res, 0, 0) == NULL || strlen(PQgetvalue(Res, 0, 0)) == 0) {
	  dbgprintf("%s\n", PQgetvalue(Res, 0, 4));
	  DecodeUTF8(sms->SMS[sms->Number].Text, PQgetvalue(Res, 0, 4), strlen(PQgetvalue(Res, 0, 4)));
	} else {
	  switch (sms->SMS[sms->Number].Coding) {
	  case SMS_Coding_Unicode_No_Compression:

	  case SMS_Coding_Default_No_Compression:
	    DecodeHexUnicode(sms->SMS[sms->Number].Text, PQgetvalue(Res, 0, 0), strlen(PQgetvalue(Res, 0, 0)));
	    break;

	  case SMS_Coding_8bit:
	    DecodeHexBin(sms->SMS[sms->Number].Text, PQgetvalue(Res, 0, 0), strlen(PQgetvalue(Res, 0, 0)));
	    sms->SMS[sms->Number].Length = strlen(PQgetvalue(Res, 0, 0))/2;

	  default:
	    break;
	  }
	}

	if (i == 1) {
	  EncodeUnicode(sms->SMS[sms->Number].Number, PQgetvalue(Res, 0, 6), strlen(PQgetvalue(Res, 0, 6)));
	} else {
	  CopyUnicodeString(sms->SMS[sms->Number].Number, sms->SMS[0].Number);
	}

	sms->SMS[sms->Number].UDH.Type = UDH_NoUDH;
	if (PQgetvalue(Res, 0, 2) != NULL && strlen(PQgetvalue(Res, 0, 2)) != 0) {
	  sms->SMS[sms->Number].UDH.Type 		= UDH_UserUDH;
	  sms->SMS[sms->Number].UDH.Length 	= strlen(PQgetvalue(Res, 0, 2))/2;
	  DecodeHexBin(sms->SMS[sms->Number].UDH.Text, PQgetvalue(Res, 0, 2), strlen(PQgetvalue(Res, 0, 2)));
	}

	sms->SMS[sms->Number].Class		= atoi(PQgetvalue(Res, 0, 3));
	sms->SMS[sms->Number].PDU  		= SMS_Submit;
	sms->Number++;

	if (i==1) {
	  sprintf(Config->CreatorID, "%s", PQgetvalue(Res, 0, 10));

	  Config->relativevalidity = atoi(PQgetvalue(Res, 0, 8));

	  Config->currdeliveryreport = -1;
	  if (!strcmp(PQgetvalue(Res, 0, 9), "yes")) {
	    Config->currdeliveryreport = 1;
	  } else if (!strcmp(PQgetvalue(Res, 0, 9), "no")) {
	    Config->currdeliveryreport = 0;
	  }

	  if (!strcmp(PQgetvalue(Res, 0, 7), "f")) break;

	}
	}

	PQclear(Res);
  	return ERR_NONE;
}

/* After sending SMS is moved to Sent Items or Error Items. */
static GSM_Error SMSDPgSQL_MoveSMS(GSM_MultiSMSMessage *sms, GSM_SMSDConfig *Config, unsigned char *ID, \
				   bool alwaysDelete, bool sent)
{
	unsigned char buffer[10000];
	PGresult *Res;

	sprintf(buffer, "DELETE FROM outbox WHERE ID = '%s'", ID);
	dbgprintf("%s\n", buffer);

	Res = PQexec(Config->DBConnPgSQL, buffer);
	if ((!Res) || (PQresultStatus(Res) != PGRES_COMMAND_OK)) {
	  WriteSMSDLog(_("Error deleting from database (MoveSMS): %s\n"), PQresultErrorMessage(Res));
	  PQclear(Res);
	  PQfinish(Config->DBConnPgSQL);
	  return ERR_UNKNOWN;
	}
	PQclear(Res);

	sprintf(buffer, "DELETE FROM outbox_multipart WHERE ID = '%s'", ID);
	dbgprintf("%s\n", buffer);

	Res = PQexec(Config->DBConnPgSQL, buffer);
	if ((!Res) || (PQresultStatus(Res) != PGRES_COMMAND_OK)) {
	  WriteSMSDLog(_("Error deleting from database (MoveSMS): %s\n"), PQresultErrorMessage(Res));
	  PQclear(Res);
	  PQfinish(Config->DBConnPgSQL);
	  return ERR_UNKNOWN;
	}
	PQclear(Res);

  	return ERR_NONE;
}

/* Adds SMS to Outbox */
static GSM_Error SMSDPgSQL_CreateOutboxSMS(GSM_MultiSMSMessage *sms, GSM_SMSDConfig *Config)
{
	unsigned char		buffer[10000], buffer2[400], buffer4[10000], buffer5[400];
	int 			i, ID;
	int                     numb_rows, numb_tuples;
	int                     error;
	PGresult 		*Res;

	sprintf(buffer, "SELECT ID FROM outbox ORDER BY ID DESC LIMIT 1");
	dbgprintf("%s\n", buffer);

	Res = PQexec(Config->DBConnPgSQL, buffer);
	if ((!Res) || (PQresultStatus(Res) != PGRES_TUPLES_OK)) {
	  WriteSMSDLog(_("Error reading from database (CreateOutbox): %s\n"),  PQresultErrorMessage(Res));
	  PQclear(Res);
	  PQfinish(Config->DBConnPgSQL);
	  return ERR_UNKNOWN;
	}

	numb_rows = PQntuples(Res);
	if (numb_rows != 0) {
	  sprintf(buffer, "%s", PQgetvalue(Res, 0, 0));
	  ID = atoi(buffer);
	} else {
	  ID = 0;
	}
	PQclear(Res);

	for (i = 0; i < sms->Number; i++) {
	  buffer[0] = 0;
	  if (i == 0) {
	    sprintf(buffer+strlen(buffer), \
		    "INSERT INTO outbox (CreatorID, SenderID, DeliveryReport, MultiPart, InsertIntoDB");
	  } else {
	    sprintf(buffer+strlen(buffer), "INSERT INTO outbox_multipart (SequencePosition");
	  }

	  sprintf(buffer+strlen(buffer), ", Text, ");

	  if (i == 0) {
	    sprintf(buffer+strlen(buffer), "DestinationNumber, RelativeValidity, ");
	  }

	  sprintf(buffer+strlen(buffer), "Coding, UDH,  \
		  Class, TextDecoded, ID) VALUES (");

	  if (i == 0) {
	    sprintf(buffer+strlen(buffer), "'Gammu %s', ", VERSION);
	    sprintf(buffer+strlen(buffer), "'', '");

	    if (sms->SMS[i].PDU == SMS_Status_Report) {
	      sprintf(buffer+strlen(buffer), "yes', '");
	    } else {
	      sprintf(buffer+strlen(buffer), "default', '");
	    }

	    if (sms->Number == 1) {
	      sprintf(buffer+strlen(buffer), "false");
	    } else {
	      sprintf(buffer+strlen(buffer), "true");
	    }

	    sprintf(buffer+strlen(buffer), "', now()");
	  } else {
	    sprintf(buffer+strlen(buffer), "'%i'", i+1);
	  }
	  sprintf(buffer+strlen(buffer), ", '");

	  switch (sms->SMS[i].Coding) {
	  case SMS_Coding_Unicode_No_Compression:

	  case SMS_Coding_Default_No_Compression:
	    EncodeHexUnicode(buffer+strlen(buffer), sms->SMS[i].Text, UnicodeLength(sms->SMS[i].Text));
	    break;

	  case SMS_Coding_8bit:
	    EncodeHexBin(buffer+strlen(buffer), sms->SMS[i].Text, sms->SMS[i].Length);

	  default:
	    break;
	  }

	  sprintf(buffer+strlen(buffer), "', ");
	  if (i==0) {
	    sprintf(buffer+strlen(buffer), "'%s', ", DecodeUnicodeString(sms->SMS[i].Number));

	    if (sms->SMS[i].SMSC.Validity.Format == SMS_Validity_RelativeFormat) {
	      sprintf(buffer+strlen(buffer), "'%i', ", sms->SMS[i].SMSC.Validity.Relative);
	    } else {
	      sprintf(buffer+strlen(buffer), "'-1', ");
	    }
	  }

	  sprintf(buffer+strlen(buffer), "'");
	  switch (sms->SMS[i].Coding) {
	  case SMS_Coding_Unicode_No_Compression:
	    sprintf(buffer+strlen(buffer), "Unicode_No_Compression");
	    break;

	  case SMS_Coding_Unicode_Compression:
	    sprintf(buffer+strlen(buffer), "Unicode_Compression");
	    break;

	  case SMS_Coding_Default_No_Compression:
	    sprintf(buffer+strlen(buffer), "Default_No_Compression");
	    break;

	  case SMS_Coding_Default_Compression:
	    sprintf(buffer+strlen(buffer), "Default_Compression");
	    break;

	  case SMS_Coding_8bit:
	    sprintf(buffer+strlen(buffer), "8bit");
	    break;
	  }

	  sprintf(buffer+strlen(buffer), "', '");
	  if (sms->SMS[i].UDH.Type != UDH_NoUDH) {
	    EncodeHexBin(buffer+strlen(buffer), sms->SMS[i].UDH.Text, sms->SMS[i].UDH.Length);
	  }

	  sprintf(buffer+strlen(buffer), "', '%i', '", sms->SMS[i].Class);
	  switch (sms->SMS[i].Coding) {
	  case SMS_Coding_Unicode_No_Compression:

	  case SMS_Coding_Default_No_Compression:
	    EncodeUTF8(buffer2,  sms->SMS[i].Text);
	    PQescapeStringConn(Config->DBConnPgSQL, buffer5, buffer2, strlen(buffer2), &error);
	    memcpy(buffer+strlen(buffer), buffer5, strlen(buffer5)+1);
	    break;

	  default:
	    break;
	  }

	  sprintf(buffer+strlen(buffer), "', '");
	  if (i == 0) {
	    while (true) {
	      ID++;
	      sprintf(buffer4, "SELECT ID FROM sentitems WHERE ID='%i'", ID);
	      dbgprintf("%s\n", buffer4);

	      Res = PQexec(Config->DBConnPgSQL, buffer4);
	      if ((!Res) || (PQresultStatus(Res) != PGRES_TUPLES_OK)) {
		WriteSMSDLog(_("Error reading from database (CreateOutbox): %s\n"),  PQresultErrorMessage(Res));
		PQclear(Res);
		PQfinish(Config->DBConnPgSQL);
		return ERR_UNKNOWN;
	      }

	      numb_rows = PQntuples(Res);
	      PQclear(Res);
	      if (numb_rows == 0) {
		sprintf(buffer4, "SELECT ID FROM outbox WHERE ID='%i'", ID);
		dbgprintf("%s\n", buffer4);

		Res = PQexec(Config->DBConnPgSQL, buffer4);
		if ((!Res) || (PQresultStatus(Res) != PGRES_TUPLES_OK)) {
		  WriteSMSDLog(_("Error writing to database (CreateOutbox): %s\n"), PQresultErrorMessage(Res));
		  PQclear(Res);
		  PQfinish(Config->DBConnPgSQL);
		  return ERR_UNKNOWN;
		}

		numb_tuples = PQntuples(Res);
		PQclear(Res);

		if (numb_tuples > 0) {
		  WriteSMSDLog(_("Duplicated outgoing SMS ID\n"));
		  continue;
		} else {
		  buffer4[0] = 0;
		  strcpy(buffer4, buffer);
		  sprintf(buffer4+strlen(buffer4), "%i')", ID);

		  Res = PQexec(Config->DBConnPgSQL, buffer4);
		  if ((!Res) || (PQresultStatus(Res) != PGRES_COMMAND_OK)) {
		    WriteSMSDLog(_("Error reading from database (CreateOutbox): %s\n"),  PQresultErrorMessage(Res));
		    PQclear(Res);
		    PQfinish(Config->DBConnPgSQL);
		    return ERR_UNKNOWN;
		  }
		PQclear(Res);
		break;
		}
	      }
	    }
	  } else {
	    strcpy(buffer4, buffer);
	    sprintf(buffer4+strlen(buffer4), "%i')", ID);
	    dbgprintf("%s\n", buffer4);

	    Res = PQexec(Config->DBConnPgSQL, buffer4);
	    if ((!Res) || (PQresultStatus(Res) != PGRES_COMMAND_OK)) {
	      WriteSMSDLog(_("Error writing to database (CreateOutbox): %s\n"), PQresultErrorMessage(Res));
	      PQclear(Res);
	      PQfinish(Config->DBConnPgSQL);
	      return ERR_UNKNOWN;
	    }
	    PQclear(Res);
	  }
	}
  	return ERR_NONE;
}

static GSM_Error SMSDPgSQL_AddSentSMSInfo(GSM_MultiSMSMessage *sms, GSM_SMSDConfig *Config, unsigned char *ID, \
					  int Part, GSM_SMSDSendingError err, int TPMR)
{
  PGresult *Res;

  unsigned char	buffer[10000], buffer2[400], buff[50], buffer5[400];
  int escape_error;

  if (err == SMSD_SEND_OK) {
    WriteSMSDLog(_("Transmitted %s (%s: %i) to %s"), Config->SMSID, (Part == sms->Number?"total":"part"), \
		 Part, DecodeUnicodeString(sms->SMS[0].Number));
  }

  buff[0] = 0;
  if (err == SMSD_SEND_OK) {
    if (sms->SMS[Part-1].PDU == SMS_Status_Report) {
      sprintf(buff, "SendingOK");
    } else {
      sprintf(buff, "SendingOKNoReport");
    }
  }

  if (err == SMSD_SEND_SENDING_ERROR) 	sprintf(buff, "SendingError");
  if (err == SMSD_SEND_ERROR) 		sprintf(buff, "Error");

  buffer[0] = 0;
  sprintf(buffer+strlen(buffer), "INSERT INTO sentitems \
		(CreatorID, ID, SequencePosition, Status, SendingDateTime,  SMSCNumber,  TPMR, \
		SenderID, Text, DestinationNumber, Coding, UDH, Class, TextDecoded, InsertIntoDB, \
                RelativeValidity) VALUES (");
  sprintf(buffer+strlen(buffer), "'%s', '%s', '%i', '%s', now(), '%s', '%i', '%s', '", \
	  Config->CreatorID, ID, Part, buff, DecodeUnicodeString(sms->SMS[Part-1].SMSC.Number), \
	  TPMR, Config->PhoneID);

  switch (sms->SMS[Part-1].Coding) {
  case SMS_Coding_Unicode_No_Compression:

  case SMS_Coding_Default_No_Compression:
    EncodeHexUnicode(buffer+strlen(buffer), sms->SMS[Part-1].Text, UnicodeLength(sms->SMS[Part-1].Text));
    break;

  case SMS_Coding_8bit:
    EncodeHexBin(buffer+strlen(buffer), sms->SMS[Part-1].Text, sms->SMS[Part-1].Length);

  default:
    break;
  }

  sprintf(buffer+strlen(buffer), "', '%s', '", DecodeUnicodeString(sms->SMS[Part-1].Number));

  switch (sms->SMS[Part-1].Coding) {
  case SMS_Coding_Unicode_No_Compression:
    sprintf(buffer+strlen(buffer), "Unicode_No_Compression");
    break;

  case SMS_Coding_Unicode_Compression:
    sprintf(buffer+strlen(buffer), "Unicode_Compression");
    break;

  case SMS_Coding_Default_No_Compression:
    sprintf(buffer+strlen(buffer), "Default_No_Compression");
    break;

  case SMS_Coding_Default_Compression:
    sprintf(buffer+strlen(buffer), "Default_Compression");
    break;

  case SMS_Coding_8bit:
    sprintf(buffer+strlen(buffer), "8bit");
    break;
  }

  sprintf(buffer+strlen(buffer), "', '");

  if (sms->SMS[Part-1].UDH.Type != UDH_NoUDH) {
    EncodeHexBin(buffer+strlen(buffer), sms->SMS[Part-1].UDH.Text, sms->SMS[Part-1].UDH.Length);
  }

  sprintf(buffer+strlen(buffer), "', '%i', '", sms->SMS[Part-1].Class);

  switch (sms->SMS[Part-1].Coding) {
  case SMS_Coding_Unicode_No_Compression:

  case SMS_Coding_Default_No_Compression:
    EncodeUTF8(buffer2,  sms->SMS[Part-1].Text);
    PQescapeStringConn(Config->DBConnPgSQL, buffer5, buffer2, strlen(buffer2), &escape_error);
    memcpy(buffer+strlen(buffer), buffer5, strlen(buffer5)+1);
    break;

  default:
    break;
  }

  sprintf(buffer+strlen(buffer), "', '%s', '", Config->DT);

  if (sms->SMS[Part-1].SMSC.Validity.Format == SMS_Validity_RelativeFormat) {
    sprintf(buffer+strlen(buffer), "%i')", sms->SMS[Part-1].SMSC.Validity.Relative);
  } else {
    sprintf(buffer+strlen(buffer), "-1')");
  }
  dbgprintf("%s\n", buffer);

  Res = PQexec(Config->DBConnPgSQL, buffer);
  if ((!Res) || (PQresultStatus(Res) != PGRES_COMMAND_OK)) {
    WriteSMSDLog(_("Error writing to database (AddSent): %s\n"), PQresultErrorMessage(Res));
    PQclear(Res);
    PQfinish(Config->DBConnPgSQL);
    return ERR_UNKNOWN;
  }
  PQclear(Res);

  return ERR_NONE;
}

static GSM_Error SMSDPgSQL_RefreshPhoneStatus(GSM_SMSDConfig *Config)
{
  PGresult *Res;

  unsigned char buffer[500];

  sprintf(buffer, "UPDATE phones SET TimeOut= now() + INTERVAL '10 seconds'");
  sprintf(buffer+strlen(buffer), " WHERE IMEI = '%s'", s.Phone.Data.IMEI);
  dbgprintf("%s\n", buffer);

  Res = PQexec(Config->DBConnPgSQL, buffer);
  if ((!Res) || (PQresultStatus(Res) != PGRES_COMMAND_OK)) {
    WriteSMSDLog(_("Error writing to database (SaveInboxSMS): %s\n"), PQresultErrorMessage(Res));
    PQclear(Res);
    PQfinish(Config->DBConnPgSQL);
    return ERR_UNKNOWN;
  }
  PQclear(Res);

  return ERR_NONE;
}

GSM_SMSDService SMSDPgSQL = {
	SMSDPgSQL_Init,
	SMSDPgSQL_InitAfterConnect,
	SMSDPgSQL_SaveInboxSMS,
	SMSDPgSQL_FindOutboxSMS,
	SMSDPgSQL_MoveSMS,
	SMSDPgSQL_CreateOutboxSMS,
	SMSDPgSQL_AddSentSMSInfo,
	SMSDPgSQL_RefreshSendStatus,
	SMSDPgSQL_RefreshPhoneStatus
};

#endif

/* How should editor hadle tabs in this file? Add editor commands here.
 * vim: noexpandtab sw=8 ts=8 sts=8:
 */
