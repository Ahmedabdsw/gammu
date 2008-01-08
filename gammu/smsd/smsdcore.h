/* (c) 2002-2004 by Marcin Wiacek and Joergen Thomsen */

#include <gammu.h>

#ifdef HAVE_MYSQL_MYSQL_H
#ifdef WIN32
#  include <winsock2.h>
#endif
#include <mysql.h>
#include <mysqld_error.h>
#endif

#ifdef HAVE_POSTGRESQL_LIBPQ_FE_H
#  include <libpq-fe.h>
#endif

#define MAX_RETRIES 1

void      SMSDaemon		(int argc, char *argv[]);
GSM_Error SMSDaemonSendSMS	(char *service, char *filename, GSM_MultiSMSMessage *sms);

typedef struct {
	/* general options */
	INI_Entry       *IncludeNumbers, *ExcludeNumbers;
	unsigned int    commtimeout, 	 sendtimeout,   receivefrequency;
	int deliveryreportdelay;
	unsigned int	resetfrequency;
	unsigned char   *deliveryreport, *logfilename,  *PINCode;
	unsigned char	*PhoneID;
	unsigned char   *RunOnReceive;
	bool checksecurity;

	/* options for FILES */
	unsigned char   *inboxpath, 	 *outboxpath, 	*sentsmspath;
	unsigned char   *errorsmspath, 	 *inboxformat,  *transmitformat;

	/* private variables required for work */
	int		relativevalidity;
	unsigned int 	retries;
	int		currdeliveryreport;
	unsigned char 	SMSID[200],	 prevSMSID[200];
	GSM_SMSC	SMSC;
	char		IMEI[GSM_MAX_IMEI_LENGTH];

#if defined(HAVE_MYSQL_MYSQL_H) || defined(HAVE_POSTGRESQL_LIBPQ_FE_H)
	/* options for SQL database */
	unsigned char	*database,	 *user,		*password;
	unsigned char	*PC,		 *skipsmscnumber;
        char 		DT[20];
	char		CreatorID[200];
#endif

#ifdef HAVE_MYSQL_MYSQL_H
       /* MySQL db connection */
       MYSQL DBConnMySQL;
#endif

#ifdef HAVE_POSTGRESQL_LIBPQ_FE_H
       /* PostgreSQL db connection */
       PGconn *DBConnPgSQL;
#endif
} GSM_SMSDConfig;

typedef enum {
	SMSD_SEND_OK = 1,
	SMSD_SEND_SENDING_ERROR,
	SMSD_SEND_DELIVERY_PENDING,
	SMSD_SEND_DELIVERY_FAILED,
	SMSD_SEND_DELIVERY_OK,
	SMSD_SEND_DELIVERY_UNKNOWN,
	SMSD_SEND_ERROR
} GSM_SMSDSendingError;

typedef struct {
	GSM_Error	(*Init) 	      (GSM_SMSDConfig *Config);
	GSM_Error	(*InitAfterConnect)   (GSM_SMSDConfig *Config);
	GSM_Error	(*SaveInboxSMS)       (GSM_MultiSMSMessage  sms, GSM_SMSDConfig *Config);
	GSM_Error	(*FindOutboxSMS)      (GSM_MultiSMSMessage *sms, GSM_SMSDConfig *Config, unsigned char *ID);
	GSM_Error	(*MoveSMS)  	      (GSM_MultiSMSMessage *sms, GSM_SMSDConfig *Config, unsigned char *ID, bool alwaysDelete, bool sent);
	GSM_Error	(*CreateOutboxSMS)    (GSM_MultiSMSMessage *sms, GSM_SMSDConfig *Config);
	GSM_Error	(*AddSentSMSInfo)     (GSM_MultiSMSMessage *sms, GSM_SMSDConfig *Config, unsigned char *ID, int Part, GSM_SMSDSendingError err, int TPMR);
	GSM_Error	(*RefreshSendStatus)  (GSM_SMSDConfig *Config, unsigned char *ID);
	GSM_Error	(*RefreshPhoneStatus) (GSM_SMSDConfig *Config);
} GSM_SMSDService;

#if defined(__GNUC__) && !defined(printf)
__attribute__((format(printf, 1, 2)))
#endif
void WriteSMSDLog(char *format, ...);

extern GSM_Error SMSD_NoneFunction		(void);
extern GSM_Error SMSD_NotImplementedFunction	(void);
extern GSM_Error SMSD_NotSupportedFunction	(void);

#define NONEFUNCTION 	(void *) SMSD_NoneFunction
#define NOTIMPLEMENTED 	(void *) SMSD_NotImplementedFunction
#define NOTSUPPORTED 	(void *) SMSD_NotSupportedFunction

/* How should editor hadle tabs in this file? Add editor commands here.
 * vim: noexpandtab sw=8 ts=8 sts=8:
 */
