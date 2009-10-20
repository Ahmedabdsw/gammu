/* (c) 2002-2004 by Marcin Wiacek and Joergen Thomsen */
/* (c) 2009 Michal Cihar */

#include <string.h>
#include <signal.h>
#include <time.h>
#include <assert.h>
#ifndef WIN32
#include <sys/types.h>
#include <sys/wait.h>
#endif
#include <gammu-config.h>
#ifdef HAVE_SYSLOG
#include <syslog.h>
#endif
#include <stdarg.h>
#include <stdlib.h>

#include <gammu-smsd.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_DUP_IO_H
#include <io.h>
#endif

#ifdef HAVE_SHM
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdlib.h>
#endif

#include <ctype.h>
#include <errno.h>

/* Some systems let waitpid(2) tell callers about stopped children. */
#if !defined (WCONTINUED)
#  define WCONTINUED 0
#endif
#if !defined (WIFCONTINUED)
#  define WIFCONTINUED(s)	(0)
#endif

#include "core.h"
#include "services/files.h"
#ifdef HAVE_MYSQL_MYSQL_H
#  include "services/mysql.h"
#endif
#ifdef HAVE_POSTGRESQL_LIBPQ_FE_H
#  include "services/pgsql.h"
#endif
#ifdef LIBDBI_FOUND
#  include "services/dbi.h"
#endif

#ifdef HAVE_WINDOWS_EVENT_LOG
#include "log-event.h"
#endif

#include "../helper/string.h"

const char smsd_name[] = "gammu-smsd";

GSM_SMSDConfig		SMSDaemon_Config;

GSM_Error SMSD_CheckDBVersion(GSM_SMSDConfig *Config, int version)
{
	SMSD_Log(DEBUG_NOTICE, Config, "Database structures version: %d, SMSD current version: %d", version, SMSD_DB_VERSION);

	if (version < SMSD_DB_VERSION) {
		SMSD_Log(DEBUG_ERROR, Config, "Database structures are from older Gammu version");
		SMSD_Log(DEBUG_INFO, Config, "Please update DataBase, if you want to use this client application");
		return ERR_UNKNOWN;
	}
	if (version > SMSD_DB_VERSION) {
		SMSD_Log(DEBUG_ERROR, Config, "DataBase structures are from higher Gammu version");
		SMSD_Log(DEBUG_INFO, Config, "Please update this client application");
		return ERR_UNKNOWN;
	}
	return ERR_NONE;
}


GSM_Error SMSD_Shutdown(GSM_SMSDConfig *Config)
{
	if (!Config->running) return ERR_NOTRUNNING;
	Config->shutdown = TRUE;
	return ERR_NONE;
}

void SMSSendingSMSStatus (GSM_StateMachine *sm, int status, int mr, void *user_data)
{
	GSM_SMSDConfig *Config = (GSM_SMSDConfig *)user_data;

	SMSD_Log(DEBUG_NOTICE, Config, "SMS sent on device: \"%s\" status=%d, reference=%d",
			GSM_GetConfig(sm, -1)->Device,
			status,
			mr);
	Config->TPMR = mr;
	if (status==0) {
		Config->SendingSMSStatus = ERR_NONE;
	} else {
		Config->SendingSMSStatus = ERR_UNKNOWN;
	}
}

void SMSD_CloseLog(GSM_SMSDConfig *Config)
{
	switch (Config->log_type) {
#ifdef HAVE_WINDOWS_EVENT_LOG
		case SMSD_LOG_EVENTLOG:
			eventlog_close(Config->log_handle);
			Config->log_handle = NULL;
			break;
#endif
		case SMSD_LOG_FILE:
			if (Config->log_handle != NULL) {
				fclose(Config->log_handle);
				Config->log_handle = NULL;
			}
			break;
		default:
			break;
	}
	Config->log_type = SMSD_LOG_NONE;
}

void SMSD_LogErrno(GSM_SMSDConfig *Config, const char *message)
{
#ifdef WIN32
	char *lpMsgBuf;

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		GetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), /* Default language */
		(LPTSTR) &lpMsgBuf,
		0,
		NULL
	);
	SMSD_Log(DEBUG_ERROR, Config, "%s, Error %d: %s\n", message, (int)GetLastError(), lpMsgBuf);

	LocalFree(lpMsgBuf);
#else
	SMSD_Log(DEBUG_ERROR, Config, "%s, Error %d: %s\n", message, errno, strerror(errno));
#endif
}

void SMSD_Terminate(GSM_SMSDConfig *Config, const char *msg, GSM_Error error, gboolean exitprogram, int rc)
{
	int ret = ERR_NONE;

	if (error != ERR_NONE && error != 0) {
		SMSD_Log(DEBUG_ERROR, Config, "%s (%s:%i)", msg, GSM_ErrorString(error), error);
	} else if (rc != 0) {
		SMSD_LogErrno(Config, msg);
	}
	if (GSM_IsConnected(Config->gsm)) {
		SMSD_Log(DEBUG_INFO, Config, "Terminating communication...");
		ret=GSM_TerminateConnection(Config->gsm);
		if (ret!=ERR_NONE) {
			printf("%s\n",GSM_ErrorString(error));
			if (GSM_IsConnected(Config->gsm)) {
				GSM_TerminateConnection(Config->gsm);
			}
		}
	}
	if (exitprogram) {
		if (rc == 0) {
			Config->running = FALSE;
			SMSD_CloseLog(Config);
		}
		if (Config->exit_on_failure) {
			exit(rc);
		} else if (error != ERR_NONE) {
			Config->failure = error;
		}
	}
}

GSM_Error SMSD_Init(GSM_SMSDConfig *Config, GSM_SMSDService *Service) {
	GSM_Error error;

	if (Config->connected) return ERR_NONE;

	error = Service->Init(Config);
	if (error == ERR_NONE) {
		Config->connected = TRUE;
	}

	return error;
}

PRINTF_STYLE(3, 4)
void SMSD_Log(SMSD_DebugLevel level, GSM_SMSDConfig *Config, const char *format, ...)
{
	GSM_DateTime 	date_time;
	char 		Buffer[2000];
	va_list		argp;
#ifdef HAVE_SYSLOG
        int priority;
#endif

	va_start(argp, format);
	vsprintf(Buffer,format, argp);
	va_end(argp);

	switch (Config->log_type) {
		case SMSD_LOG_EVENTLOG:
#ifdef HAVE_WINDOWS_EVENT_LOG
			eventlog_log(Config->log_handle, level, Buffer);
#endif
			break;
		case SMSD_LOG_SYSLOG:
#ifdef HAVE_SYSLOG
			switch (level) {
				case -1:
					priority = LOG_ERR;
					break;
				case 0:
					priority = LOG_NOTICE;
					break;
				case 1:
					priority = LOG_INFO;
					break;
				default:
					priority = LOG_DEBUG;
					break;
			}
			syslog(priority, "%s", Buffer);
#endif
			break;
		case SMSD_LOG_FILE:
			if (level != DEBUG_ERROR &&
					level != DEBUG_INFO &&
					(level & Config->debug_level) == 0) {
				return;
			}

			GSM_GetCurrentDateTime(&date_time);

			if (Config->use_timestamps) {
				fprintf(Config->log_handle,"%s %4d/%02d/%02d %02d:%02d:%02d ",
					DayOfWeek(date_time.Year, date_time.Month, date_time.Day),
					date_time.Year, date_time.Month, date_time.Day,
					date_time.Hour, date_time.Minute, date_time.Second);
			}
#ifdef HAVE_GETPID
			fprintf(Config->log_handle, "%s[%lld]: ", Config->program_name, (long long)getpid());
#else
			fprintf(Config->log_handle, "%s: ", Config->program_name);
#endif
			fprintf(Config->log_handle,"%s\n",Buffer);
			fflush(Config->log_handle);
			break;
		case SMSD_LOG_NONE:
			break;
	}

	if (Config->use_stderr && level == -1) {
#ifdef HAVE_GETPID
		fprintf(stderr, "%s[%lld]: ", Config->program_name, (long long)getpid());
#else
		fprintf(stderr, "%s: ", Config->program_name);
#endif
		fprintf(stderr, "%s\n", Buffer);
	}
}

void SMSD_Log_Function(const char *text, void *data)
{
	GSM_SMSDConfig *Config = (GSM_SMSDConfig *)data;
	size_t pos;
	size_t newsize;

	if (strcmp("\n", text) == 0) {
		SMSD_Log(DEBUG_GAMMU, Config, "gammu: %s", Config->gammu_log_buffer);
		Config->gammu_log_buffer[0] = 0;
		return;
	}

	if (Config->gammu_log_buffer == NULL) {
		pos = 0;
	} else {
		pos = strlen(Config->gammu_log_buffer);
	}
	newsize = pos + strlen(text) + 1;
	if (newsize > Config->gammu_log_buffer_size) {
		newsize += 50;
		Config->gammu_log_buffer = realloc(Config->gammu_log_buffer, newsize);
		assert(Config->gammu_log_buffer != NULL);
		Config->gammu_log_buffer_size = newsize;
	}

	strcpy(Config->gammu_log_buffer + pos, text);
}

GSM_SMSDConfig *SMSD_NewConfig(const char *name)
{
	GSM_SMSDConfig *Config;
	Config = (GSM_SMSDConfig *)malloc(sizeof(GSM_SMSDConfig));
	if (Config == NULL) return Config;

	Config->running = FALSE;
	Config->failure = ERR_NONE;
	Config->exit_on_failure = TRUE;
	Config->shutdown = FALSE;
	Config->gsm = NULL;
	Config->gammu_log_buffer = NULL;
	Config->gammu_log_buffer_size = 0;
	Config->logfilename = NULL;
	Config->smsdcfgfile = NULL;
	Config->log_handle = NULL;
	Config->log_type = SMSD_LOG_NONE;
	Config->debug_level = 0;
	Config->Service = NULL;
	if (name == NULL) {
		Config->program_name = smsd_name;
	} else {
		Config->program_name = name;
	}

	return Config;
}

/**
 * Returns SMSD service based on configuration.
 */
GSM_Error SMSGetService(GSM_SMSDConfig *Config, GSM_SMSDService **Service)
{
	if (Config->Service == NULL) {
		return ERR_UNCONFIGURED;
	}
	if (strcasecmp(Config->Service, "FILES") == 0) {
		SMSD_Log(DEBUG_NOTICE, Config, "Using FILES service");
		*Service = &SMSDFiles;
	} else if (strcasecmp(Config->Service, "DBI") == 0) {
#ifdef LIBDBI_FOUND
		SMSD_Log(DEBUG_NOTICE, Config, "Using DBI service");
		*Service = &SMSDDBI;
#else
		SMSD_Log(DEBUG_ERROR, Config, "DBI service was not compiled in!");
		return ERR_DISABLED;
#endif
	} else if (strcasecmp(Config->Service, "MYSQL") == 0) {
#ifdef HAVE_MYSQL_MYSQL_H
		SMSD_Log(DEBUG_NOTICE, Config, "Using MYSQL service");
		*Service = &SMSDMySQL;
#else
		SMSD_Log(DEBUG_ERROR, Config, "MYSQL service was not compiled in!");
		return ERR_DISABLED;
#endif
	} else if (strcasecmp(Config->Service, "PGSQL") == 0) {
#ifdef HAVE_POSTGRESQL_LIBPQ_FE_H
		SMSD_Log(DEBUG_NOTICE, Config, "Using PGSQL service");
		*Service = &SMSDPgSQL;
#else
		SMSD_Log(DEBUG_ERROR, Config, "PGSQL service was not compiled in!");
		return ERR_DISABLED;
#endif
	} else {
		SMSD_Log(DEBUG_ERROR, Config, "Unknown SMSD service type: \"%s\"", Config->Service);
		return ERR_UNCONFIGURED;
	}
	return ERR_NONE;
}


void SMSD_FreeConfig(GSM_SMSDConfig *Config)
{
	GSM_SMSDService		*Service;
	GSM_Error error;

	error = SMSGetService(Config, &Service);
	if (error == ERR_NONE && Config->connected) {
		Service->Free(Config);
		Config->connected = FALSE;
	}
	SMSD_CloseLog(Config);
	free(Config->gammu_log_buffer);
	INI_Free(Config->smsdcfgfile);
	GSM_FreeStateMachine(Config->gsm);
	free(Config);
}


GSM_Error SMSD_ReadConfig(const char *filename, GSM_SMSDConfig *Config, gboolean uselog)
{
	GSM_Config 		smsdcfg;
	GSM_Config 		*gammucfg;
	unsigned char		*str;
	static unsigned char	emptyPath[1] = "\0";
	GSM_Error		error;
	int			fd;
#ifdef HAVE_SHM
	char			fullpath[PATH_MAX + 1];
#endif
#ifdef WIN32
	size_t i, len;
#endif

	memset(&smsdcfg, 0, sizeof(smsdcfg));

	Config->shutdown = FALSE;
	Config->running = FALSE;
	Config->connected = FALSE;
	Config->failure = ERR_NONE;
	Config->exit_on_failure = TRUE;
	Config->gsm = GSM_AllocStateMachine();
	if (Config->gsm == NULL) {
		fprintf(stderr, "Failed to allocate memory for state machine!\n");
		return ERR_MOREMEMORY;
	}
	Config->gammu_log_buffer = NULL;
	Config->gammu_log_buffer_size = 0;
	Config->logfilename = NULL;
	Config->smsdcfgfile = NULL;
	Config->use_timestamps = TRUE;
	Config->log_type = SMSD_LOG_NONE;
	Config->log_handle = NULL;
	Config->use_stderr = TRUE;

#ifdef HAVE_SHM
	/* Calculate key for shared memory */
	if (realpath(filename, fullpath) == NULL) {
		strncpy(fullpath, filename, PATH_MAX);
		fullpath[PATH_MAX] = 0;
	}
	Config->shm_key = ftok(fullpath, SMSD_SHM_KEY);
#endif
#ifdef WIN32
	len = sprintf(Config->map_key, "Gammu-smsd-%s", filename);
	/* Replace some possibly dangerous chars */
	for (i = 0; i < len; i++) {
		if (!isalpha(Config->map_key[i]) && !isdigit(Config->map_key[i])) {
			Config->map_key[i] = '_';
		}
	}
#endif

	error = INI_ReadFile(filename, FALSE, &Config->smsdcfgfile);
	if (Config->smsdcfgfile == NULL || error != ERR_NONE) {
		if (error == ERR_FILENOTSUPPORTED) {
			fprintf(stderr, "Could not parse config file \"%s\"\n",filename);
		} else {
			fprintf(stderr, "Can't find file \"%s\"\n",filename);
		}
		return ERR_CANTOPENFILE;
	}

	str = INI_GetValue(Config->smsdcfgfile, "smsd", "debuglevel", FALSE);
	if (str)
		Config->debug_level = atoi(str);
	else
		Config->debug_level = 0;

	Config->logfilename=INI_GetValue(Config->smsdcfgfile, "smsd", "logfile", FALSE);
	if (Config->logfilename != NULL) {
		if (!uselog) {
			Config->log_type = SMSD_LOG_FILE;
			Config->use_stderr = FALSE;
			fd = dup(1);
			if (fd < 0) return ERR_CANTOPENFILE;
			Config->log_handle = fdopen(fd, "a");
			Config->use_timestamps = FALSE;
#ifdef HAVE_WINDOWS_EVENT_LOG
		} else if (strcmp(Config->logfilename, "eventlog") == 0) {
			Config->log_type = SMSD_LOG_EVENTLOG;
			Config->log_handle = eventlog_init();
			Config->use_stderr = TRUE;
#endif
#ifdef HAVE_SYSLOG
		} else if (strcmp(Config->logfilename, "syslog") == 0) {
			Config->log_type = SMSD_LOG_SYSLOG;
			openlog(Config->program_name, LOG_PID, LOG_DAEMON);
			Config->use_stderr = TRUE;
#endif
		} else {
			Config->log_type = SMSD_LOG_FILE;
			if (strcmp(Config->logfilename, "stderr") == 0) {
				fd = dup(2);
				if (fd < 0) return ERR_CANTOPENFILE;
				Config->log_handle = fdopen(fd, "a");
				Config->use_stderr = FALSE;
			} else if (strcmp(Config->logfilename, "stdout") == 0) {
				fd = dup(1);
				if (fd < 0) return ERR_CANTOPENFILE;
				Config->log_handle = fdopen(fd, "a");
				Config->use_stderr = FALSE;
			} else {
				Config->log_handle = fopen(Config->logfilename, "a");
				Config->use_stderr = TRUE;
			}
			if (Config->log_handle == NULL) {
				fprintf(stderr, "Can't open log file \"%s\"\n", Config->logfilename);
				return ERR_CANTOPENFILE;
			}
			fprintf(stderr, "Log filename is \"%s\"\n",Config->logfilename);
		}
	}

	Config->Service = INI_GetValue(Config->smsdcfgfile, "smsd", "service", FALSE);
	if (Config->Service == NULL) {
		SMSD_Log(DEBUG_ERROR, Config, "No SMSD service configured, please set service to use in configuration file!");
		return ERR_NOSERVICE;
	}

	SMSD_Log(DEBUG_NOTICE, Config, "Configuring Gammu SMSD...");
#ifdef HAVE_SHM
	SMSD_Log(DEBUG_NOTICE, Config, "SHM token: 0x%llx (%lld)", (long long)Config->shm_key, (long long)Config->shm_key);
#endif

	/* Does our config file contain gammu section? */
	if (INI_FindLastSectionEntry(Config->smsdcfgfile, "gammu", FALSE) == NULL) {
 		SMSD_Log(DEBUG_ERROR, Config, "No gammu configuration found!");
		return ERR_UNCONFIGURED;
	}

	gammucfg = GSM_GetConfig(Config->gsm, 0);
	GSM_ReadConfig(Config->smsdcfgfile, gammucfg, 0);
	GSM_SetConfigNum(Config->gsm, 1);
	gammucfg->UseGlobalDebugFile = FALSE;

	/* Force debug level in Gammu */
	if ((DEBUG_GAMMU & Config->debug_level) != 0) {
		strcpy(gammucfg->DebugLevel, "textall");
		GSM_SetDebugLevel("textall", GSM_GetGlobalDebug());
	}

	Config->PINCode=INI_GetValue(Config->smsdcfgfile, "smsd", "PIN", FALSE);
	if (Config->PINCode == NULL) {
 		SMSD_Log(DEBUG_INFO, Config, "Warning: No PIN code in %s file",filename);
	} else {
		SMSD_Log(DEBUG_NOTICE, Config, "PIN code is \"%s\"",Config->PINCode);
	}

	Config->NetworkCode = INI_GetValue(Config->smsdcfgfile, "smsd", "NetworkCode", FALSE);
	if (Config->NetworkCode != NULL) {
		SMSD_Log(DEBUG_NOTICE, Config, "Network code is \"%s\"",Config->NetworkCode);
	}

	Config->PhoneCode = INI_GetValue(Config->smsdcfgfile, "smsd", "PhoneCode", FALSE);
	if (Config->PhoneCode != NULL) {
		SMSD_Log(DEBUG_NOTICE, Config, "Phone code is \"%s\"",Config->PhoneCode);
	}

	str = INI_GetValue(Config->smsdcfgfile, "smsd", "commtimeout", FALSE);
	if (str) Config->commtimeout=atoi(str); else Config->commtimeout = 30;
	str = INI_GetValue(Config->smsdcfgfile, "smsd", "deliveryreportdelay", FALSE);
	if (str) Config->deliveryreportdelay=atoi(str); else Config->deliveryreportdelay = 600;
	str = INI_GetValue(Config->smsdcfgfile, "smsd", "sendtimeout", FALSE);
	if (str) Config->sendtimeout=atoi(str); else Config->sendtimeout = 30;
	str = INI_GetValue(Config->smsdcfgfile, "smsd", "receivefrequency", FALSE);
	if (str) Config->receivefrequency=atoi(str); else Config->receivefrequency = 0;
	str = INI_GetValue(Config->smsdcfgfile, "smsd", "checksecurity", FALSE);
	if (str) Config->checksecurity = INI_IsTrue(str); else Config->checksecurity = TRUE;
	str = INI_GetValue(Config->smsdcfgfile, "smsd", "checksignal", FALSE);
	if (str) Config->checksignal = INI_IsTrue(str); else Config->checksignal = TRUE;
	str = INI_GetValue(Config->smsdcfgfile, "smsd", "checkbattery", FALSE);
	if (str) Config->checkbattery = INI_IsTrue(str); else Config->checkbattery = TRUE;
	str = INI_GetValue(Config->smsdcfgfile, "smsd", "resetfrequency", FALSE);
	if (str) Config->resetfrequency=atoi(str); else Config->resetfrequency = 0;
	str = INI_GetValue(Config->smsdcfgfile, "smsd", "maxretries", FALSE);
	if (str) Config->maxretries=atoi(str); else Config->maxretries = 1;
	SMSD_Log(DEBUG_NOTICE, Config, "commtimeout=%i, sendtimeout=%i, receivefrequency=%i, resetfrequency=%i",
			Config->commtimeout, Config->sendtimeout, Config->receivefrequency, Config->resetfrequency);
	SMSD_Log(DEBUG_NOTICE, Config, "checks: security=%d, battery=%d, signal=%d",
			Config->checksecurity, Config->checkbattery, Config->checksignal);

	Config->deliveryreport = INI_GetValue(Config->smsdcfgfile, "smsd", "deliveryreport", FALSE);
	if (Config->deliveryreport == NULL || (strcasecmp(Config->deliveryreport, "log") != 0 && strcasecmp(Config->deliveryreport, "sms") != 0)) {
		Config->deliveryreport = "no";
	}
	SMSD_Log(DEBUG_NOTICE, Config, "deliveryreport = %s", Config->deliveryreport);

	Config->PhoneID = INI_GetValue(Config->smsdcfgfile, "smsd", "phoneid", FALSE);
	if (Config->PhoneID == NULL) Config->PhoneID = "";
	SMSD_Log(DEBUG_NOTICE, Config, "phoneid = %s", Config->PhoneID);

	Config->RunOnReceive = INI_GetValue(Config->smsdcfgfile, "smsd", "runonreceive", FALSE);

	str = INI_GetValue(Config->smsdcfgfile, "smsd", "smsc", FALSE);
	if (str) {
		Config->SMSC.Location		= 1;
		Config->SMSC.DefaultNumber[0]	= 0;
		Config->SMSC.DefaultNumber[1]	= 0;
		Config->SMSC.Name[0]		= 0;
		Config->SMSC.Name[1]		= 0;
		Config->SMSC.Validity.Format	= SMS_Validity_NotAvailable;
		Config->SMSC.Format		= SMS_FORMAT_Text;
		EncodeUnicode(Config->SMSC.Number, str, strlen(str));
	} else {
		Config->SMSC.Location     = 0;
	}

	if (!strcasecmp(Config->Service,"FILES")) {
		Config->inboxpath=INI_GetValue(Config->smsdcfgfile, "smsd", "inboxpath", FALSE);
		if (Config->inboxpath == NULL) Config->inboxpath = emptyPath;

		Config->inboxformat=INI_GetValue(Config->smsdcfgfile, "smsd", "inboxformat", FALSE);
		if (Config->inboxformat == NULL || (strcasecmp(Config->inboxformat, "detail") != 0 && strcasecmp(Config->inboxformat, "unicode") != 0)) {
			Config->inboxformat = "standard";
		}
		SMSD_Log(DEBUG_NOTICE, Config, "Inbox is \"%s\" with format \"%s\"", Config->inboxpath, Config->inboxformat);

		Config->outboxpath=INI_GetValue(Config->smsdcfgfile, "smsd", "outboxpath", FALSE);
		if (Config->outboxpath == NULL) Config->outboxpath = emptyPath;

		Config->transmitformat=INI_GetValue(Config->smsdcfgfile, "smsd", "transmitformat", FALSE);
		if (Config->transmitformat == NULL || (strcasecmp(Config->transmitformat, "auto") != 0 && strcasecmp(Config->transmitformat, "unicode") != 0)) {
			Config->transmitformat = "7bit";
		}
		SMSD_Log(DEBUG_NOTICE, Config, "Outbox is \"%s\" with transmission format \"%s\"", Config->outboxpath, Config->transmitformat);

		Config->sentsmspath=INI_GetValue(Config->smsdcfgfile, "smsd", "sentsmspath", FALSE);
		if (Config->sentsmspath == NULL) Config->sentsmspath = Config->outboxpath;
		SMSD_Log(DEBUG_NOTICE, Config, "Sent SMS moved to \"%s\"",Config->sentsmspath);

		Config->errorsmspath=INI_GetValue(Config->smsdcfgfile, "smsd", "errorsmspath", FALSE);
		if (Config->errorsmspath == NULL) Config->errorsmspath = Config->sentsmspath;
		SMSD_Log(DEBUG_NOTICE, Config, "SMS with errors moved to \"%s\"",Config->errorsmspath);
	}

#ifdef LIBDBI_FOUND
	if (!strcasecmp(Config->Service,"DBI")) {
		Config->skipsmscnumber = INI_GetValue(Config->smsdcfgfile, "smsd", "skipsmscnumber", FALSE);
		if (Config->skipsmscnumber == NULL) Config->skipsmscnumber="";
		Config->user = INI_GetValue(Config->smsdcfgfile, "smsd", "user", FALSE);
		if (Config->user == NULL) Config->user="root";
		Config->password = INI_GetValue(Config->smsdcfgfile, "smsd", "password", FALSE);
		if (Config->password == NULL) Config->password="";
		Config->PC = INI_GetValue(Config->smsdcfgfile, "smsd", "pc", FALSE);
		if (Config->PC == NULL) Config->PC="localhost";
		Config->database = INI_GetValue(Config->smsdcfgfile, "smsd", "database", FALSE);
		if (Config->database == NULL) Config->database="sms";
		Config->driver = INI_GetValue(Config->smsdcfgfile, "smsd", "driver", FALSE);
		if (Config->driver == NULL) Config->driver="mysql";
		Config->dbdir = INI_GetValue(Config->smsdcfgfile, "smsd", "dbdir", FALSE);
		if (Config->dbdir == NULL) Config->dbdir="./";
		Config->driverspath = INI_GetValue(Config->smsdcfgfile, "smsd", "driverspath", FALSE);
		/* This one can be NULL */
	}
#endif

#ifdef HAVE_MYSQL_MYSQL_H
	if (!strcasecmp(Config->Service,"MYSQL")) {
		Config->skipsmscnumber = INI_GetValue(Config->smsdcfgfile, "smsd", "skipsmscnumber", FALSE);
		if (Config->skipsmscnumber == NULL) Config->skipsmscnumber="";
		Config->user = INI_GetValue(Config->smsdcfgfile, "smsd", "user", FALSE);
		if (Config->user == NULL) Config->user="root";
		Config->password = INI_GetValue(Config->smsdcfgfile, "smsd", "password", FALSE);
		if (Config->password == NULL) Config->password="";
		Config->PC = INI_GetValue(Config->smsdcfgfile, "smsd", "pc", FALSE);
		if (Config->PC == NULL) Config->PC="localhost";
		Config->database = INI_GetValue(Config->smsdcfgfile, "smsd", "database", FALSE);
		if (Config->database == NULL) Config->database="sms";
	}
#endif

#ifdef HAVE_POSTGRESQL_LIBPQ_FE_H
	if (!strcasecmp(Config->Service,"PGSQL")) {
		Config->skipsmscnumber = INI_GetValue(Config->smsdcfgfile, "smsd", "skipsmscnumber", FALSE);
		if (Config->skipsmscnumber == NULL) Config->skipsmscnumber="";
		Config->user = INI_GetValue(Config->smsdcfgfile, "smsd", "user", FALSE);
		if (Config->user == NULL) Config->user="root";
		Config->password = INI_GetValue(Config->smsdcfgfile, "smsd", "password", FALSE);
		if (Config->password == NULL) Config->password="";
		Config->PC = INI_GetValue(Config->smsdcfgfile, "smsd", "pc", FALSE);
		if (Config->PC == NULL) Config->PC="localhost";
		Config->database = INI_GetValue(Config->smsdcfgfile, "smsd", "database", FALSE);
		if (Config->database == NULL) Config->database="sms";
	}
#endif

	Config->IncludeNumbers=INI_FindLastSectionEntry(Config->smsdcfgfile, "include_numbers", FALSE);
	Config->ExcludeNumbers=INI_FindLastSectionEntry(Config->smsdcfgfile, "exclude_numbers", FALSE);
	if (Config->IncludeNumbers != NULL) {
		SMSD_Log(DEBUG_NOTICE, Config, "Include numbers available");
	}
	if (Config->ExcludeNumbers != NULL) {
		if (Config->IncludeNumbers == NULL) {
			SMSD_Log(DEBUG_NOTICE, Config, "Exclude numbers available");
		} else {
			SMSD_Log(DEBUG_INFO, Config, "Exclude numbers available, but IGNORED");
		}
	}

	Config->retries 	  = 0;
	Config->prevSMSID[0] 	  = 0;
	Config->relativevalidity  = -1;
	Config->Status = NULL;

	return ERR_NONE;
}

gboolean SMSD_CheckSecurity(GSM_SMSDConfig *Config)
{
	GSM_SecurityCode 	SecurityCode;
	GSM_Error		error;
	const char *code = NULL;

	/* Need PIN ? */
	error=GSM_GetSecurityStatus(Config->gsm,&SecurityCode.Type);
	/* Unknown error */
	if (error != ERR_NOTSUPPORTED && error != ERR_NONE) {
		SMSD_Log(DEBUG_ERROR, Config, "Error getting security status (%s:%i)", GSM_ErrorString(error), error);
		return FALSE;
	}
	/* No supported - do not check more */
	if (error == ERR_NOTSUPPORTED) return TRUE;

	/* If PIN, try to enter */
	switch (SecurityCode.Type) {
		case SEC_Pin:
			code = Config->PINCode;
			break;
		case SEC_Phone:
			code = Config->PhoneCode;
			break;
		case SEC_Network:
			code = Config->NetworkCode;
			break;
		case SEC_SecurityCode:
		case SEC_Pin2:
		case SEC_Puk:
		case SEC_Puk2:
			SMSD_Terminate(Config, "ERROR: phone requires not supported code type", ERR_UNKNOWN, TRUE, -1);
			return FALSE;
		case SEC_None:
			return TRUE;
	}
	if (code == NULL) {
		SMSD_Log(DEBUG_INFO, Config, "Warning: no code in config when phone might want one!");
		return FALSE;
	}
	SMSD_Log(DEBUG_NOTICE, Config, "Trying to enter code");
	strcpy(SecurityCode.Code, code);
	error = GSM_EnterSecurityCode(Config->gsm, SecurityCode);
	if (error == ERR_SECURITYERROR) {
		SMSD_Terminate(Config, "ERROR: incorrect PIN", error, TRUE, -1);
		return FALSE;
	}
	if (error != ERR_NONE) {
		SMSD_Log(DEBUG_ERROR, Config, "Error entering PIN (%s:%i)", GSM_ErrorString(error), error);
		return FALSE;
	}
	return TRUE;
}

/**
 * Prepares a command line for RunOnReceive command.
 */
char *SMSD_RunOnReceiveCommand(GSM_SMSDConfig *Config, const char *locations)
{
	char *result;

	assert(Config->RunOnReceive != NULL);

	if (locations == NULL) return strdup(Config->RunOnReceive);

	result = (char *)malloc(strlen(locations) + strlen(Config->RunOnReceive) + 20);
	assert(result != NULL);

	result[0] = 0;
	strcat(result, "\"");
	strcat(result, Config->RunOnReceive);
	strcat(result, "\" ");
	strcat(result, locations);
	return result;
}

#ifdef WIN32
gboolean SMSD_RunOnReceive(GSM_MultiSMSMessage sms UNUSED, GSM_SMSDConfig *Config, char *locations)
{
	BOOL ret;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	char *cmdline;

	cmdline = SMSD_RunOnReceiveCommand(Config, locations);

	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));

	SMSD_Log(DEBUG_INFO, Config, "Starting run on receive: %s", cmdline);

	ret = CreateProcess(NULL,     /* No module name (use command line) */
			cmdline,	/* Command line */
			NULL,           /* Process handle not inheritable*/
			NULL,           /* Thread handle not inheritable*/
			FALSE,          /* Set handle inheritance to FALSE*/
			0,              /* No creation flags*/
			NULL,           /* Use parent's environment block*/
			NULL,           /* Use parent's starting directory */
			&si,            /* Pointer to STARTUPINFO structure*/
			&pi );           /* Pointer to PROCESS_INFORMATION structure*/
	free(cmdline);
	if (! ret) {
		SMSD_LogErrno(Config, "CreateProcess failed");
	} else {
		/* We don't need handles at all */
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
	}
	return ret;
}
#else

gboolean SMSD_RunOnReceive(GSM_MultiSMSMessage sms UNUSED, GSM_SMSDConfig *Config, char *locations)
{
	int pid;
	int i;
	pid_t w;
	int status;
	char *cmdline;

	pid = fork();

	if (pid == -1) {
		SMSD_LogErrno(Config, "Error spawning new process");
		return FALSE;
	}

	if (pid != 0) {
		/* We are the parent, wait for child */
		i = 0;
		do {
			w = waitpid(pid, &status, WUNTRACED | WCONTINUED);
			if (w == -1) {
				SMSD_Log(DEBUG_INFO, Config, "Failed to wait for process");
				return FALSE;
			}

			if (WIFEXITED(status)) {
				if (WEXITSTATUS(status) == 0) {
					SMSD_Log(DEBUG_INFO, Config, "Process finished successfully");
				} else {
					SMSD_Log(DEBUG_ERROR, Config, "Process failed with exit status %d", WEXITSTATUS(status));
				}
				return (WEXITSTATUS(status) == 0);
			} else if (WIFSIGNALED(status)) {
				SMSD_Log(DEBUG_ERROR, Config, "Process killed by signal %d", WTERMSIG(status));
				return FALSE;
			} else if (WIFSTOPPED(status)) {
				SMSD_Log(DEBUG_INFO, Config, "Process stopped by signal %d", WSTOPSIG(status));
			} else if (WIFCONTINUED(status)) {
				SMSD_Log(DEBUG_INFO, Config, "Process continued");
			}
			usleep(100000);
			if (i++ > 1200) {
				SMSD_Log(DEBUG_INFO, Config, "Waited two minutes for child process, giving up");
				return TRUE;
			}
		} while (!WIFEXITED(status) && !WIFSIGNALED(status));

		return TRUE;
	}

	/* we are the child */

	/* Calculate command line */
	cmdline = SMSD_RunOnReceiveCommand(Config, locations);
	SMSD_Log(DEBUG_INFO, Config, "Starting run on receive: %s", cmdline);

	/* Close all file descriptors */
	for (i = 0; i < 255; i++) {
		close(i);
	}

	/* Run the program */
	status = system(cmdline);

	/* Propagate error code */
	if (WIFEXITED(status)) {
		exit(WEXITSTATUS(status));
	} else {
		exit(127);
	}
}
#endif

/**
 * Checks whether we are allowed to accept a message from number.
 */
gboolean SMSD_CheckRemoteNumber(GSM_SMSDConfig *Config, GSM_SMSDService *Service, const char *number)
{
	gboolean process = TRUE;
	INI_Entry		*e;

	if (Config->IncludeNumbers != NULL) {
		process = FALSE;
		for (e = Config->IncludeNumbers; e != NULL; e = e->Prev) {
			if (strcmp(number,e->EntryValue) == 0) {
				SMSD_Log(DEBUG_NOTICE, Config, "Number %s matched IncludeNumbers", number);
				return TRUE;
			}
		}
	} else if (Config->ExcludeNumbers != NULL) {
		process = TRUE;
		for (e = Config->ExcludeNumbers; e != NULL; e = e->Prev) {
			if (strcmp(number, e->EntryValue) == 0) {
				SMSD_Log(DEBUG_NOTICE, Config, "Number %s matched ExcludeNumbers", number);
				return FALSE;
			}
		}
	}
	return process;
}

gboolean SMSD_ReadDeleteSMS(GSM_SMSDConfig *Config, GSM_SMSDService *Service)
{
	gboolean			start;
	GSM_MultiSMSMessage 	sms;
	char 		buffer[100];
	GSM_Error		error=ERR_NONE;
	int			i;
	char			*locations;

	start=TRUE;
	sms.Number = 0;
	sms.SMS[0].Location = 0;
	while (error == ERR_NONE && !Config->shutdown) {
		sms.SMS[0].Folder = 0;
		error=GSM_GetNextSMS(Config->gsm, &sms, start);
		switch (error) {
		case ERR_EMPTY:
			break;
		case ERR_NONE:
			/* Not Inbox SMS - exit */
			if (!sms.SMS[0].InboxFolder) break;
			DecodeUnicode(sms.SMS[0].Number,buffer);
			if (SMSD_CheckRemoteNumber(Config, Service, buffer)) {
				SMSD_Log(DEBUG_NOTICE, Config, "Received message from %s", buffer);
				Config->Status->Received += sms.Number;
	 			error = Service->SaveInboxSMS(&sms, Config, &locations);
				if (Config->RunOnReceive != NULL && error == ERR_NONE) {
					SMSD_RunOnReceive(sms, Config, locations);
				}
				free(locations);
			} else {
				SMSD_Log(DEBUG_NOTICE, Config, "Excluded %s", buffer);
			}
			break;
		default:
	 		SMSD_Log(DEBUG_INFO, Config, "Error getting SMS (%s:%i)", GSM_ErrorString(error), error);
			return FALSE;
		}
		if (error == ERR_NONE && sms.SMS[0].InboxFolder) {
			for (i=0;i<sms.Number;i++) {
				sms.SMS[i].Folder=0;
				error=GSM_DeleteSMS(Config->gsm,&sms.SMS[i]);
				switch (error) {
				case ERR_NONE:
				case ERR_EMPTY:
					break;
				default:
					SMSD_Log(DEBUG_INFO, Config, "Error deleting SMS (%s:%i)", GSM_ErrorString(error), error);
					return FALSE;
				}
			}
		}
		start=FALSE;
	}
	return TRUE;
}

gboolean SMSD_CheckSMSStatus(GSM_SMSDConfig *Config,GSM_SMSDService *Service)
{
	GSM_SMSMemoryStatus	SMSStatus;
	GSM_Error		error;

	/* Do we have any SMS in phone ? */
	error=GSM_GetSMSStatus(Config->gsm,&SMSStatus);
	if (error != ERR_NONE) {
		SMSD_Log(DEBUG_INFO, Config, "Error getting SMS status (%s:%i)", GSM_ErrorString(error), error);
		return FALSE;
	}
	/* Yes. We have SMS in phone */
	if (SMSStatus.SIMUsed+SMSStatus.PhoneUsed != 0) {
		return SMSD_ReadDeleteSMS(Config,Service);
	}
	return TRUE;
}

void SMSD_PhoneStatus(GSM_SMSDConfig *Config) {
	GSM_Error error;

	if (Config->checkbattery) {
		error = GSM_GetBatteryCharge(Config->gsm, &Config->Status->Charge);
	} else {
		error = ERR_UNKNOWN;
	}
	if (error != ERR_NONE) {
		memset(&(Config->Status->Charge), 0, sizeof(Config->Status->Charge));
	}
	if (Config->checksignal) {
		error = GSM_GetSignalQuality(Config->gsm, &Config->Status->Network);
	} else {
		error = ERR_UNKNOWN;
	}
	if (error != ERR_NONE) {
		memset(&(Config->Status->Network), 0, sizeof(Config->Status->Network));
	}
}


gboolean SMSD_SendSMS(GSM_SMSDConfig *Config,GSM_SMSDService *Service)
{
	GSM_MultiSMSMessage  	sms;
	GSM_DateTime         	Date;
	GSM_Error            	error;
	unsigned int         	j;
	int			i, z;

	/* Clean structure before use */
	for (i = 0; i < GSM_MAX_MULTI_SMS; i++) {
		GSM_SetDefaultSMSData(&sms.SMS[i]);
	}

	error = Service->FindOutboxSMS(&sms, Config, Config->SMSID);

	if (error == ERR_EMPTY || error == ERR_NOTSUPPORTED) {
		/* No outbox sms - wait few seconds and escape */
		for (j = 0; j < Config->commtimeout && !Config->shutdown; j++) {
			sleep(1);
			if (j % 15 == 0) {
				SMSD_PhoneStatus(Config);
				Service->RefreshPhoneStatus(Config);
			}
		}
		return TRUE;
	}
	if (error != ERR_NONE) {
		/* Unknown error - escape */
		SMSD_Log(DEBUG_INFO, Config, "Error in outbox on '%s'", Config->SMSID);
		for (i=0;i<sms.Number;i++) {
			Config->Status->Failed++;
			Service->AddSentSMSInfo(&sms, Config, Config->SMSID, i+1, SMSD_SEND_ERROR, -1);
		}
		Service->MoveSMS(&sms,Config, Config->SMSID, TRUE,FALSE);
		return FALSE;
	}

	if (Config->shutdown) return TRUE;

	if (strcmp(Config->prevSMSID, Config->SMSID) == 0) {
		SMSD_Log(DEBUG_NOTICE, Config, "Same message as previous one: %s", Config->SMSID);
		Config->retries++;
		if (Config->retries > Config->maxretries) {
			Config->retries = 0;
			strcpy(Config->prevSMSID, "");
			SMSD_Log(DEBUG_INFO, Config, "Moved to errorbox: %s", Config->SMSID);
			for (i=0;i<sms.Number;i++) {
				Config->Status->Failed++;
				Service->AddSentSMSInfo(&sms, Config, Config->SMSID, i+1, SMSD_SEND_ERROR, -1);
			}
			Service->MoveSMS(&sms,Config, Config->SMSID, TRUE,FALSE);
			return FALSE;
		}
	} else {
		SMSD_Log(DEBUG_NOTICE, Config, "New messsage to send: %s", Config->SMSID);
		Config->retries = 0;
		strcpy(Config->prevSMSID, Config->SMSID);
	}
	for (i=0;i<sms.Number;i++) {
		if (sms.SMS[i].SMSC.Location == 1) {
			if (Config->SMSC.Location == 0) {
				Config->SMSC.Location = 1;
				error = GSM_GetSMSC(Config->gsm,&Config->SMSC);
				if (error!=ERR_NONE) {
					SMSD_Log(DEBUG_ERROR, Config, "Error getting SMSC from phone");
					return FALSE;
				}

			}
			memcpy(&sms.SMS[i].SMSC,&Config->SMSC,sizeof(GSM_SMSC));
			sms.SMS[i].SMSC.Location = 0;
			if (Config->relativevalidity != -1) {
				sms.SMS[i].SMSC.Validity.Format	  = SMS_Validity_RelativeFormat;
				sms.SMS[i].SMSC.Validity.Relative = Config->relativevalidity;
			}
		}

		if (Config->currdeliveryreport == 1) {
			sms.SMS[i].PDU = SMS_Status_Report;
		} else {
			if ((strcmp(Config->deliveryreport, "no") != 0 && (Config->currdeliveryreport == -1))) sms.SMS[i].PDU = SMS_Status_Report;
		}

		SMSD_PhoneStatus(Config);
		Config->TPMR = -1;
		Config->SendingSMSStatus = ERR_TIMEOUT;
		error = GSM_SendSMS(Config->gsm, &sms.SMS[i]);
		if (error != ERR_NONE) {
			SMSD_Log(DEBUG_INFO, Config, "Error sending SMS %s (%i): %s",
				Config->SMSID, error, GSM_ErrorString(error));
			Config->TPMR = -1;
			goto failure_unsent;
		}
		Service->RefreshPhoneStatus(Config);
		j    = 0;
		while (!Config->shutdown) {
			GSM_GetCurrentDateTime (&Date);
			z=Date.Second;
			while (z==Date.Second) {
				usleep(10000);
				GSM_GetCurrentDateTime(&Date);
				GSM_ReadDevice(Config->gsm,TRUE);
				if (Config->SendingSMSStatus != ERR_TIMEOUT) break;
			}
			Service->RefreshSendStatus(Config, Config->SMSID);
			Service->RefreshPhoneStatus(Config);
			if (Config->SendingSMSStatus != ERR_TIMEOUT) break;
			j++;
			if (j>Config->sendtimeout) break;
		}
		if (Config->SendingSMSStatus != ERR_NONE) {
			SMSD_Log(DEBUG_INFO, Config, "Error getting send status of %s (%i): %s",
				Config->SMSID,
				Config->SendingSMSStatus, GSM_ErrorString(Config->SendingSMSStatus));
			goto failure_unsent;
		}
		Config->Status->Sent++;
		error = Service->AddSentSMSInfo(&sms, Config, Config->SMSID, i+1, SMSD_SEND_OK, Config->TPMR);
		if (error!=ERR_NONE) {
			goto failure_sent;
		}
	}
	strcpy(Config->prevSMSID, "");
	if (Service->MoveSMS(&sms,Config, Config->SMSID, FALSE, TRUE) != ERR_NONE) {
		Service->MoveSMS(&sms,Config, Config->SMSID, TRUE, FALSE);
	}
	return TRUE;
failure_unsent:
	Config->Status->Failed++;
	Service->AddSentSMSInfo(&sms, Config, Config->SMSID, i + 1, SMSD_SEND_SENDING_ERROR, Config->TPMR);
	Service->MoveSMS(&sms,Config, Config->SMSID, TRUE, FALSE);
	return FALSE;
failure_sent:
	if (Service->MoveSMS(&sms,Config, Config->SMSID, FALSE, TRUE) != ERR_NONE) {
		Service->MoveSMS(&sms,Config, Config->SMSID, TRUE, FALSE);
	}
	return FALSE;
}

GSM_Error SMSD_MainLoop(GSM_SMSDConfig *Config, gboolean exit_on_failure)
{
	GSM_SMSDService		*Service;
	GSM_Error		error;
	int                     errors = -1, initerrors=0;
 	time_t			lastreceive, lastreset = 0;
	int i;

	Config->failure = ERR_NONE;
	Config->exit_on_failure = exit_on_failure;
	error = SMSGetService(Config, &Service);
	if (error!=ERR_NONE) {
		SMSD_Terminate(Config, "Failed to setup SMSD service", error, TRUE, -1);
		goto done;
	}

	error = SMSD_Init(Config, Service);
	if (error!=ERR_NONE) {
		SMSD_Terminate(Config, "Initialisation failed, stopping Gammu smsd", error, TRUE, -1);
		goto done;
	}

#ifdef HAVE_SHM
	/* Allocate world redable SHM segment */
	Config->shm_handle = shmget(Config->shm_key, sizeof(GSM_SMSDStatus), IPC_CREAT | S_IRWXU | S_IRGRP | S_IROTH);
	if (Config->shm_handle == -1) {
		SMSD_Terminate(Config, "Failed to allocate shared memory segment!", ERR_NONE, TRUE, -1);
		goto done;
	}
	Config->Status = shmat(Config->shm_handle, NULL, 0);
	if (Config->Status == (void *) -1) {
		SMSD_Terminate(Config, "Failed to map shared memory segment!", ERR_NONE, TRUE, -1);
		goto done;
	}
#elif defined(WIN32)
	Config->map_handle = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(GSM_SMSDStatus), Config->map_key);
	if (Config->map_handle == NULL) {
		SMSD_Terminate(Config, "Failed to allocate shared memory segment!", ERR_NONE, TRUE, -1);
		goto done;
	}
	Config->Status = MapViewOfFile(Config->map_handle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(GSM_SMSDStatus));
#else
	Config->Status = malloc(sizeof(GSM_SMSDStatus));
	if (Config->Status == NULL) {
		SMSD_Terminate(Config, "Failed to map shared memory segment!", ERR_NONE, TRUE, -1);
		goto done;
	}
#endif
	Config->Status->Version = SMSD_SHM_VERSION;
	Config->running = TRUE;
	strcpy(Config->Status->PhoneID, Config->PhoneID);
	sprintf(Config->Status->Client, "Gammu %s on %s compiler %s",
		VERSION,
		GetOS(),
		GetCompiler());
	memset(&Config->Status->Charge, 0, sizeof(GSM_BatteryCharge));
	memset(&Config->Status->Network, 0, sizeof(GSM_SignalQuality));
	Config->Status->Received = 0;
	Config->Status->Failed = 0;
	Config->Status->Sent = 0;
	Config->Status->IMEI[0] = 0;

	lastreceive		= time(NULL);
	lastreset		= time(NULL);
	Config->SendingSMSStatus 	= ERR_UNKNOWN;

	while (!Config->shutdown) {
		/* There were errors in communication - try to recover */
		if (errors > 2 || errors == -1) {
			if (errors != -1) {
				SMSD_Log(DEBUG_INFO, Config, "Terminating communication %s, (%i, %i times)",
						GSM_ErrorString(error), error, errors);
				error=GSM_TerminateConnection(Config->gsm);
			}
			if (initerrors++ > 3) {
				SMSD_Log(DEBUG_INFO, Config, "Going to 30 seconds sleep because of too much connection errors");
				for (i = 0; i < 30; i++) {
					if (Config->shutdown)
						break;
					sleep(1);
				}
			}
			SMSD_Log(DEBUG_INFO, Config, "Starting phone communication...");
			error=GSM_InitConnection_Log(Config->gsm, 2, SMSD_Log_Function, Config);
			switch (error) {
			case ERR_NONE:
				GSM_SetSendSMSStatusCallback(Config->gsm, SMSSendingSMSStatus, Config);
				if (errors == -1) {
					errors = 0;
					if (GSM_GetIMEI(Config->gsm, Config->Status->IMEI) != ERR_NONE) {
						errors++;
					} else {
						error = Service->InitAfterConnect(Config);
						if (error!=ERR_NONE) {
							SMSD_Terminate(Config, "Post initialisation failed, stopping Gammu smsd", error, TRUE, -1);
							goto done_connected;
						}
						GSM_SetFastSMSSending(Config->gsm, TRUE);
					}
				} else {
					errors = 0;
				}
				if (initerrors > 3 || initerrors < 0) {
					error = GSM_Reset(Config->gsm, FALSE); /* soft reset */
					SMSD_Log(DEBUG_INFO, Config, "Reset return code: %s (%i) ",
							GSM_ErrorString(error),
							error);
					lastreset = time(NULL);
					sleep(5);
				}
				break;
			case ERR_DEVICEOPENERROR:
				SMSD_Terminate(Config, "Can't open device",
						error, TRUE, -1);
				goto done;
			default:
				SMSD_Log(DEBUG_INFO, Config, "Error at init connection %s (%i)",
						GSM_ErrorString(error), error);
				errors = 250;
				break;
			}
			continue;
		}
		if ((difftime(time(NULL), lastreceive) >= Config->receivefrequency) || (Config->SendingSMSStatus != ERR_NONE)) {
	 		lastreceive = time(NULL);


			if (Config->checksecurity && !SMSD_CheckSecurity(Config)) {
				errors++;
				initerrors++;
				continue;
			} else {
				errors=0;
			}

			initerrors = 0;

			/* read all incoming SMS */
			if (!SMSD_CheckSMSStatus(Config,Service)) {
				errors++;
				continue;
			} else {
				errors=0;
			}

			/* time for preventive reset */
			if (Config->resetfrequency > 0 && difftime(time(NULL), lastreset) >= Config->resetfrequency) {
				errors = 254;
				initerrors = -2;
				continue;
			}
		}
		if (!SMSD_SendSMS(Config, Service)) {
			continue;
		}
	}
	Service->Free(Config);

#ifdef HAVE_SHM
	shmdt(Config->Status);
	shmctl(Config->shm_handle, IPC_RMID, NULL);
#elif defined(WIN32)
	UnmapViewOfFile(Config->Status);
	CloseHandle(Config->map_handle);
#else
	free(Config->Status);
#endif

done_connected:
	GSM_SetFastSMSSending(Config->gsm,FALSE);
done:
	SMSD_Terminate(Config, "Stopping Gammu smsd", ERR_NONE, FALSE, 0);
	return Config->failure;
}

GSM_Error SMSD_InjectSMS(GSM_SMSDConfig		*Config, GSM_MultiSMSMessage *sms, char *NewID)
{
	GSM_SMSDService		*Service;
	GSM_Error error;

	error = SMSGetService(Config, &Service);
	if (error != ERR_NONE) return ERR_UNKNOWN;

	error = SMSD_Init(Config, Service);
	if (error != ERR_NONE) return ERR_UNKNOWN;

	error = Service->CreateOutboxSMS(sms, Config, NewID);
	return error;
}

GSM_Error SMSD_GetStatus(GSM_SMSDConfig *Config, GSM_SMSDStatus *status)
{
	if (Config->running) {
		memcpy(status, Config->Status, sizeof(GSM_SMSDStatus));
		return ERR_NONE;
	}
#if defined(HAVE_SHM) || defined(WIN32)
	/* Get SHM segment */
#ifdef HAVE_SHM
	Config->shm_handle = shmget(Config->shm_key, sizeof(GSM_SMSDStatus), 0);
	if (Config->shm_handle == -1) {
		SMSD_LogErrno(Config, "Can not shmget");
		return ERR_NOTRUNNING;
	}
	Config->Status = shmat(Config->shm_handle, NULL, 0);
	if (Config->Status == (void *) -1) {
		SMSD_LogErrno(Config, "Can not shmat");
		return ERR_UNKNOWN;
	}
	if (Config->Status->Version != SMSD_SHM_VERSION) {
		shmdt(Config->Status);
		return ERR_WRONGCRC;
	}
#else
	Config->map_handle = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READONLY, 0, sizeof(GSM_SMSDStatus), Config->map_key);
	if (Config->map_handle == NULL) {
		SMSD_LogErrno(Config, "Can not CreateFileMapping");
		return ERR_NOTRUNNING;
	}
	Config->Status = MapViewOfFile(Config->map_handle, FILE_MAP_READ, 0, 0, sizeof(GSM_SMSDStatus));
	if (Config->Status == NULL) {
		SMSD_LogErrno(Config, "Can not CreateFileMapping");
		return ERR_UNKNOWN;
	}
#endif
	memcpy(status, Config->Status, sizeof(GSM_SMSDStatus));

#ifdef HAVE_SHM
	shmdt(Config->Status);
#else
	UnmapViewOfFile(Config->Status);
	CloseHandle(Config->map_handle);
#endif
	return ERR_NONE;
#else
	return ERR_NOTSUPPORTED;
#endif
}

GSM_Error SMSD_NoneFunction(void)
{
	return ERR_NONE;
}

GSM_Error SMSD_NotImplementedFunction(void)
{
	return ERR_NOTIMPLEMENTED;
}

GSM_Error SMSD_NotSupportedFunction(void)
{
	return ERR_NOTSUPPORTED;
}
/* How should editor hadle tabs in this file? Add editor commands here.
 * vim: noexpandtab sw=8 ts=8 sts=8:
 */
