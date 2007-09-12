#include "../common/misc/locales.h"

#include <gammu.h>
#include <string.h>
#include <stdarg.h>

#ifdef HAVE_PTHREAD
#  include <pthread.h>
#endif

#include "search.h"
#include "common.h"
#include "formats.h"

#if defined(WIN32) || defined(HAVE_PTHREAD)
/**
 * Structure to hold information about connection for searching.
 */
typedef struct {
	/**
	 * Connection name.
	 */
	unsigned char Connection[50];
} OneConnectionInfo;

/**
 * Structure to hold device information for phone searching.
 */
typedef struct {
	/**
	 * Device name
	 */
	unsigned char Device[50];
	/**
	 * List of connections to try
	 */
	OneConnectionInfo Connections[5];
} OneDeviceInfo;

int num;
bool SearchOutput;

/**
 * Like printf, but only when output from searching is enabled.
 */
int SearchPrintf(const char *format, ...)
{
	va_list ap;
	int ret;

	if (!SearchOutput)
		return 0;

	va_start(ap, format);
	ret = vprintf(format, ap);
	va_end(ap);

	return ret;
}

void SearchPrintPhoneInfo(OneDeviceInfo * Info, int connection_index,
			  GSM_StateMachine * sm)
{
	GSM_Error error;
	char buffer[GSM_MAX_INFO_LENGTH];

	/* Try to get phone manufacturer */
	error = GSM_GetManufacturer(sm, buffer);

	/* Bail out if we failed */
	if (error != ERR_NONE) {
		SearchPrintf("\t%s\n", GSM_ErrorString(error));
		return;
	}

	/* Print basic information */
	printf("\t" LISTFORMAT "%s\n", _("Manufacturer"), buffer);

	/* Try to get phone model */
	error = GSM_GetModel(sm, buffer);

	/* Bail out if we failed */
	if (error != ERR_NONE) {
		SearchPrintf("\t%s\n", GSM_ErrorString(error));
		return;
	}

	/* Print model information */
	printf("\t" LISTFORMAT "%s (%s)\n", _("Model"),
	       GSM_GetModelInfo(sm)->model, buffer);
}

void SearchPhoneThread(OneDeviceInfo * Info)
{
	int j;
	GSM_Error error;
	GSM_StateMachine *ss;
	GSM_Config *cfg;
	GSM_Config *globalcfg;

	/* Iterate over all connections */
	for (j = 0; strlen(Info->Connections[j].Connection) != 0; j++) {

		/* Allocate state machine */
		ss = GSM_AllocStateMachine();
		if (ss == NULL)
			return;

		/* Get configuration pointers */
		cfg = GSM_GetConfig(ss, 0);
		globalcfg = GSM_GetConfig(s, 0);

		/* We share some configuration with global one */
		cfg->UseGlobalDebugFile = globalcfg->UseGlobalDebugFile;
		cfg->DebugFile = strdup(globalcfg->DebugFile);
		strcpy(cfg->DebugLevel, globalcfg->DebugLevel);
		if (globalcfg->Localize == NULL) {
			cfg->Localize = NULL;
		} else {
			cfg->Localize = strdup(globalcfg->Localize);
		}

		/* Configure the tested state machine */
		cfg->Device = strdup(Info->Device);
		cfg->Connection = strdup(Info->Connections[j].Connection);
		cfg->SyncTime = strdup("no");
		cfg->Model[0] = 0;
		cfg->LockDevice = strdup("no");
		cfg->StartInfo = strdup("no");

		/* We have only one configured connection */
		GSM_SetConfigNum(ss, 1);

		/* Let's connect */
		error = GSM_InitConnection(ss, 1);

		printf(_("Connection \"%s\" on device \"%s\"\n"),
		       Info->Connections[j].Connection, Info->Device);

		/* Did we succeed? Show info */
		if (error == ERR_NONE) {
			SearchPrintPhoneInfo(Info, j, ss);
		} else {
			SearchPrintf("\t%s\n", GSM_ErrorString(error));
		}

		if (error != ERR_DEVICEOPENERROR) {
			GSM_TerminateConnection(ss);
			dbgprintf("Closing done\n");
		}

		if (error == ERR_DEVICEOPENERROR)
			break;

		/* Free allocated buffer */
		GSM_FreeStateMachine(ss);
	}
	num--;
}

#ifdef HAVE_PTHREAD
pthread_t Thread[100];
#endif

OneDeviceInfo SearchDevices[60];

void MakeSearchThread(int i)
{
	num++;
#ifdef HAVE_PTHREAD
	if (pthread_create
	    (&Thread[i], NULL, (void *)SearchPhoneThread,
	     &SearchDevices[i]) != 0) {
		dbgprintf("Error creating thread\n");
	}
#else
	if (CreateThread((LPSECURITY_ATTRIBUTES) NULL, 0,
			 (LPTHREAD_START_ROUTINE) SearchPhoneThread,
			 &SearchDevices[i], 0, NULL) == NULL) {
		dbgprintf("Error creating thread\n");
	}
#endif
}

void SearchPhone(int argc, char *argv[])
{
	int i, dev = 0, dev2 = 0;

	SearchOutput = false;
	if (argc == 3 && strcasecmp(argv[2], "-debug") == 0)
		SearchOutput = true;

	num = 0;
#ifdef WIN32
	SearchDevices[dev].Device[0] = 0;
	sprintf(SearchDevices[dev].Connections[0].Connection, "irdaphonet");
	sprintf(SearchDevices[dev].Connections[1].Connection, "irdaat");
	SearchDevices[dev].Connections[2].Connection[0] = 0;
	dev++;
	dev2 = dev;
	for (i = 0; i < 20; i++) {
		sprintf(SearchDevices[dev2].Device, "com%i:", i + 1);
		sprintf(SearchDevices[dev2].Connections[0].Connection,
			"fbusdlr3");
		sprintf(SearchDevices[dev2].Connections[1].Connection, "fbus");
		sprintf(SearchDevices[dev2].Connections[2].Connection,
			"at19200");
		sprintf(SearchDevices[dev2].Connections[3].Connection, "mbus");
		SearchDevices[dev2].Connections[4].Connection[0] = 0;
		dev2++;
	}
#endif
#ifdef __linux__
	for (i = 0; i < 6; i++) {
		sprintf(SearchDevices[dev].Device, "/dev/ircomm%i", i);
		sprintf(SearchDevices[dev].Connections[0].Connection,
			"irdaphonet");
		sprintf(SearchDevices[dev].Connections[1].Connection,
			"at19200");
		SearchDevices[dev].Connections[2].Connection[0] = 0;
		dev++;
	}
	dev2 = dev;
	for (i = 0; i < 10; i++) {
		sprintf(SearchDevices[dev2].Device, "/dev/ttyS%i", i);
		sprintf(SearchDevices[dev2].Connections[0].Connection,
			"fbusdlr3");
		sprintf(SearchDevices[dev2].Connections[1].Connection, "fbus");
		sprintf(SearchDevices[dev2].Connections[2].Connection,
			"at19200");
		sprintf(SearchDevices[dev2].Connections[3].Connection, "mbus");
		SearchDevices[dev2].Connections[4].Connection[0] = 0;
		dev2++;
	}
	for (i = 0; i < 8; i++) {
		sprintf(SearchDevices[dev2].Device, "/dev/ttyD00%i", i);
		sprintf(SearchDevices[dev2].Connections[0].Connection,
			"fbusdlr3");
		sprintf(SearchDevices[dev2].Connections[1].Connection, "fbus");
		sprintf(SearchDevices[dev2].Connections[2].Connection,
			"at19200");
		sprintf(SearchDevices[dev2].Connections[3].Connection, "mbus");
		SearchDevices[dev2].Connections[4].Connection[0] = 0;
		dev2++;
	}
	for (i = 0; i < 4; i++) {
		sprintf(SearchDevices[dev2].Device, "/dev/usb/tts/%i", i);
		sprintf(SearchDevices[dev2].Connections[0].Connection,
			"fbusdlr3");
		sprintf(SearchDevices[dev2].Connections[1].Connection, "fbus");
		sprintf(SearchDevices[dev2].Connections[2].Connection,
			"at19200");
		sprintf(SearchDevices[dev2].Connections[3].Connection, "mbus");
		SearchDevices[dev2].Connections[4].Connection[0] = 0;
		dev2++;
	}
#endif
	for (i = 0; i < dev; i++)
		MakeSearchThread(i);
	while (num != 0)
		my_sleep(5);
	for (i = dev; i < dev2; i++)
		MakeSearchThread(i);
	while (num != 0)
		my_sleep(5);
}
#endif				/*Support for threads */

/* How should editor hadle tabs in this file? Add editor commands here.
 * vim: noexpandtab sw=8 ts=8 sts=8:
 */
