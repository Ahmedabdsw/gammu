/* (c) 2002-2005 by Marcin Wiacek and Michal Cihar */
/* Phones ID (c) partially by Walek */

#include <stdarg.h>
#define _GNU_SOURCE /* For strcasestr */
#include <string.h>
#include <errno.h>

#include <gammu-call.h>
#include <gammu-settings.h>
#include <gammu-unicode.h>

#include "gsmcomon.h"
#include "gsmphones.h"
#include "gsmstate.h"
#include "misc/coding/coding.h"
#include "device/devfunc.h"

#if defined(WIN32) || defined(DJGPP)
#include <shlobj.h>

#define FALLBACK_GAMMURC "gammurc"
#else
#define FALLBACK_GAMMURC "/etc/gammurc"
#endif

static void GSM_RegisterConnection(GSM_StateMachine *s, unsigned int connection,
		GSM_Device_Functions *device, GSM_Protocol_Functions *protocol)
{
	if ((unsigned int)s->ConnectionType == connection) {
		s->Device.Functions	= device;
		s->Protocol.Functions	= protocol;
	}
}

typedef struct {
	const char *Name;
	const GSM_ConnectionType Connection;
	bool SkipDtrRts;
} GSM_ConnectionInfo;

/**
 * Mapping of connection names to internal identifications.
 */
static const GSM_ConnectionInfo GSM_Connections[] = {
	{"at", GCT_AT, false},

	// cables
	{"mbus", GCT_MBUS2, false},
	{"fbus", GCT_FBUS2, false},
	{"fbuspl2303", GCT_FBUS2PL2303, false},
	{"dlr3", GCT_FBUS2DLR3, false},
	{"fbusdlr3", GCT_FBUS2DLR3, false},
	{"dku5", GCT_DKU5FBUS2, false},
	{"dku5fbus", GCT_DKU5FBUS2, false},
	{"ark3116fbus", GCT_DKU5FBUS2, true},
	{"dku2", GCT_DKU2PHONET, false},
	{"dku2phonet", GCT_DKU2PHONET, false},
	{"dku2at", GCT_DKU2AT, false},

        // for serial ports assigned by bt stack
	{"fbusblue", GCT_FBUS2BLUE, false},
	{"phonetblue", GCT_PHONETBLUE, false},

	// bt
	{"blueobex", GCT_BLUEOBEX, false},
	{"bluephonet", GCT_BLUEPHONET, false},
	{"blueat", GCT_BLUEAT, false},
	{"bluerfobex", GCT_BLUEOBEX, false},
	{"bluerffbus", GCT_BLUEFBUS2, false},
	{"bluerfphonet", GCT_BLUEPHONET, false},
	{"bluerfat", GCT_BLUEAT, false},
	{"bluerfgnapbus", GCT_BLUEGNAPBUS, false},

	// old "serial" irda
	{"infrared", GCT_FBUS2IRDA, false},
	{"fbusirda", GCT_FBUS2IRDA, false},

	// socket irda
	{"irda", GCT_IRDAPHONET, false},
	{"irdaphonet", GCT_IRDAPHONET, false},
	{"irdaat", GCT_IRDAAT, false},
	{"irdaobex", GCT_IRDAOBEX, false},
	{"irdagnapbus", GCT_IRDAGNAPBUS, false},
};

static GSM_Error GSM_RegisterAllConnections(GSM_StateMachine *s, const char *connection)
{
	size_t i;
	char *buff, *nodtr_pos, *nopower_pos;

	/* Copy connection name, so that we can play with it */
	buff = strdup(connection);
	if (buff == NULL) {
		return ERR_MOREMEMORY;
	}

	/* We check here is used connection string type is correct for ANY
	 * OS. If not, we return with error, that string is incorrect at all
	 */
	s->ConnectionType = 0;
	s->SkipDtrRts = false;
	s->NoPowerCable = false;

	/* Are we asked for connection using stupid cable? */
	nodtr_pos = strcasestr(buff, "-nodtr");
	if (nodtr_pos != NULL) {
		*nodtr_pos = 0;

	}

	/* Are we asked for connection using cable which does not
	 * use DTR/RTS as power supply? */
	nopower_pos = strcasestr(buff, "-nopower");
	if (nopower_pos != NULL) {
		*nopower_pos = 0;
		s->NoPowerCable = true;
	}

	for (i = 0; i < sizeof(GSM_Connections) / sizeof(GSM_Connections[0]); i++) {
		/* Check connection name */
		if (strcasecmp(GSM_Connections[i].Name, buff) == 0) {
			s->ConnectionType = GSM_Connections[i].Connection;
			s->SkipDtrRts = GSM_Connections[i].SkipDtrRts;
			break;
		}
	}


	/* If we were forced, set this flag */
	if (nodtr_pos != NULL) {
		s->SkipDtrRts = true;
	}

	/* Special case - at can contains speed */
	if (s->ConnectionType == 0 && strncasecmp("at", buff, 2) == 0) {
		/* Use some resonable default, when no speed defined */
		if (strlen(buff) == 2) {
			s->Speed = 19200;
		} else {
			s->Speed = FindSerialSpeed(buff + 2);
		}
		if (s->Speed != 0) {
			s->ConnectionType = GCT_AT;
		}
	}

	/* Free allocated memory */
	free(buff);

	if (s->ConnectionType==0) {
		return ERR_UNKNOWNCONNECTIONTYPESTRING;
	}

	/* We check now if user gave connection type compiled & available
	 * for used OS (if not, we return, that source not available)
	 */
	s->Device.Functions	= NULL;
	s->Protocol.Functions	= NULL;
#ifdef GSM_ENABLE_MBUS2
	GSM_RegisterConnection(s, GCT_MBUS2, 	  &SerialDevice,  &MBUS2Protocol);
#endif
#ifdef GSM_ENABLE_FBUS2
	GSM_RegisterConnection(s, GCT_FBUS2,	  &SerialDevice,  &FBUS2Protocol);
#endif
#ifdef GSM_ENABLE_FBUS2DLR3
	GSM_RegisterConnection(s, GCT_FBUS2DLR3,  &SerialDevice,  &FBUS2Protocol);
#endif
#ifdef GSM_ENABLE_DKU5FBUS2
	GSM_RegisterConnection(s, GCT_DKU5FBUS2,  &SerialDevice,  &FBUS2Protocol);
#endif
#ifdef GSM_ENABLE_FBUS2PL2303
	GSM_RegisterConnection(s, GCT_FBUS2PL2303,&SerialDevice,  &FBUS2Protocol);
#endif
#ifdef GSM_ENABLE_FBUS2BLUE
	GSM_RegisterConnection(s, GCT_FBUS2BLUE,  &SerialDevice,  &FBUS2Protocol);
#endif
#ifdef GSM_ENABLE_FBUS2IRDA
	GSM_RegisterConnection(s, GCT_FBUS2IRDA,  &SerialDevice,  &FBUS2Protocol);
#endif
#ifdef GSM_ENABLE_DKU2PHONET
	GSM_RegisterConnection(s, GCT_DKU2PHONET, &SerialDevice,  &PHONETProtocol);
#endif
#ifdef GSM_ENABLE_DKU2AT
	GSM_RegisterConnection(s, GCT_DKU2AT,     &SerialDevice,  &ATProtocol);
#endif
#ifdef GSM_ENABLE_AT
	GSM_RegisterConnection(s, GCT_AT, 	  &SerialDevice,  &ATProtocol);
#endif
#ifdef GSM_ENABLE_PHONETBLUE
	GSM_RegisterConnection(s, GCT_PHONETBLUE, &SerialDevice,  &PHONETProtocol);
#endif
#ifdef GSM_ENABLE_IRDAGNAPBUS
	GSM_RegisterConnection(s, GCT_IRDAGNAPBUS,&IrdaDevice,    &GNAPBUSProtocol);
#endif
#ifdef GSM_ENABLE_IRDAPHONET
	GSM_RegisterConnection(s, GCT_IRDAPHONET, &IrdaDevice, 	  &PHONETProtocol);
#endif
#ifdef GSM_ENABLE_IRDAAT
	GSM_RegisterConnection(s, GCT_IRDAAT, 	  &IrdaDevice,    &ATProtocol);
#endif
#ifdef GSM_ENABLE_IRDAOBEX
	GSM_RegisterConnection(s, GCT_IRDAOBEX,   &IrdaDevice,    &OBEXProtocol);
#endif
#ifdef GSM_ENABLE_BLUEGNAPBUS
	GSM_RegisterConnection(s, GCT_BLUEGNAPBUS,&BlueToothDevice,&GNAPBUSProtocol);
#endif
#ifdef GSM_ENABLE_BLUEFBUS2
	GSM_RegisterConnection(s, GCT_BLUEFBUS2,  &BlueToothDevice,&FBUS2Protocol);
#endif
#ifdef GSM_ENABLE_BLUEPHONET
	GSM_RegisterConnection(s, GCT_BLUEPHONET, &BlueToothDevice,&PHONETProtocol);
#endif
#ifdef GSM_ENABLE_BLUEAT
	GSM_RegisterConnection(s, GCT_BLUEAT, 	  &BlueToothDevice,&ATProtocol);
#endif
#ifdef GSM_ENABLE_BLUEOBEX
	GSM_RegisterConnection(s, GCT_BLUEOBEX,   &BlueToothDevice,&OBEXProtocol);
#endif
	if (s->Device.Functions==NULL || s->Protocol.Functions==NULL)
			return ERR_SOURCENOTAVAILABLE;
	return ERR_NONE;
}

static void GSM_RegisterModule(GSM_StateMachine *s,GSM_Phone_Functions *phone)
{
	/* Auto model */
	if (s->CurrentConfig->Model[0] == 0) {
		if (strstr(phone->models,GetModelData(NULL,s->Phone.Data.Model,NULL)->model) != NULL) {
			smprintf(s,"[Module           - \"%s\"]\n",phone->models);
			s->Phone.Functions = phone;
		}
	} else {
		if (strstr(phone->models,s->CurrentConfig->Model) != NULL) {
			smprintf(s,"[Module           - \"%s\"]\n",phone->models);
			s->Phone.Functions = phone;
		}
	}
}

GSM_Error GSM_RegisterAllPhoneModules(GSM_StateMachine *s)
{
	OnePhoneModel *model;

	/* Auto model */
	if (s->CurrentConfig->Model[0] == 0) {
		model = GetModelData(NULL,s->Phone.Data.Model,NULL);
#ifdef GSM_ENABLE_ATGEN
		/* With ATgen and auto model we can work with unknown models too */
		if (s->ConnectionType==GCT_AT || s->ConnectionType==GCT_BLUEAT || s->ConnectionType==GCT_IRDAAT || s->ConnectionType==GCT_DKU2AT) {
#ifdef GSM_ENABLE_ALCATEL
			/* If phone provides Alcatel specific functions, enable them */
			if (model->model[0] != 0 && GSM_IsPhoneFeatureAvailable(model, F_ALCATEL)) {
				smprintf(s,"[Module           - \"%s\"]\n",ALCATELPhone.models);
				s->Phone.Functions = &ALCATELPhone;
				return ERR_NONE;
			}
#endif
#ifdef GSM_ENABLE_ATOBEX
			/* If phone provides Sony-Ericsson specific functions, enable them */
			if (model->model[0] != 0 && GSM_IsPhoneFeatureAvailable(model, F_OBEX)) {
				smprintf(s,"[Module           - \"%s\"]\n",ATOBEXPhone.models);
				s->Phone.Functions = &ATOBEXPhone;
				return ERR_NONE;
			}
#endif
			smprintf(s,"[Module           - \"%s\"]\n",ATGENPhone.models);
			s->Phone.Functions = &ATGENPhone;
			return ERR_NONE;
		}
#endif
		/* With OBEXgen and auto model we can work with unknown models too */
#ifdef GSM_ENABLE_OBEXGEN
		if (s->ConnectionType==GCT_BLUEOBEX || s->ConnectionType==GCT_IRDAOBEX) {
			smprintf(s,"[Module           - \"%s\"]\n",OBEXGENPhone.models);
			s->Phone.Functions = &OBEXGENPhone;
			return ERR_NONE;
		}
#endif
		if (model->model[0] == 0) return ERR_UNKNOWNMODELSTRING;
	}
	s->Phone.Functions=NULL;
#ifdef GSM_ENABLE_ATGEN
	/* AT module can have the same models ID to "normal" Nokia modules */
	if (s->ConnectionType==GCT_AT || s->ConnectionType==GCT_BLUEAT || s->ConnectionType==GCT_IRDAAT || s->ConnectionType==GCT_DKU2AT) {
		GSM_RegisterModule(s,&ATGENPhone);
		if (s->Phone.Functions!=NULL) return ERR_NONE;
	}
#endif
#ifdef GSM_ENABLE_OBEXGEN
	GSM_RegisterModule(s,&OBEXGENPhone);
#endif
#ifdef GSM_ENABLE_GNAPGEN
	GSM_RegisterModule(s,&GNAPGENPhone);
#endif
#ifdef GSM_ENABLE_NOKIA3320
	GSM_RegisterModule(s,&N3320Phone);
#endif
#ifdef GSM_ENABLE_NOKIA3650
	GSM_RegisterModule(s,&N3650Phone);
#endif
#ifdef GSM_ENABLE_NOKIA650
	GSM_RegisterModule(s,&N650Phone);
#endif
#ifdef GSM_ENABLE_NOKIA6110
	GSM_RegisterModule(s,&N6110Phone);
#endif
#ifdef GSM_ENABLE_NOKIA6510
	GSM_RegisterModule(s,&N6510Phone);
#endif
#ifdef GSM_ENABLE_NOKIA7110
	GSM_RegisterModule(s,&N7110Phone);
#endif
#ifdef GSM_ENABLE_NOKIA9210
	GSM_RegisterModule(s,&N9210Phone);
#endif
#ifdef GSM_ENABLE_ALCATEL
	GSM_RegisterModule(s,&ALCATELPhone);
#endif
#ifdef GSM_ENABLE_ATOBEX
	GSM_RegisterModule(s,&ATOBEXPhone);
#endif
	if (s->Phone.Functions==NULL) return ERR_UNKNOWNMODELSTRING;
	return ERR_NONE;
}

GSM_Error GSM_InitConnection(GSM_StateMachine *s, int ReplyNum)
{
	GSM_Error	error;
	GSM_DateTime	current_time;
	int		i;

	for (i=0;i<s->ConfigNum;i++) {
		s->CurrentConfig		  = &s->Config[i];

		/* Skip non configured sections */
		if (s->CurrentConfig->Connection == NULL) {
			smprintf_level(s, D_ERROR, "[Empty section    - %d]\n", i);
			continue;
		}

		s->Speed			  = 0;
		s->ReplyNum			  = ReplyNum;
		s->Phone.Data.ModelInfo		  = GetModelData("unknown",NULL,NULL);
		s->Phone.Data.Manufacturer[0]	  = 0;
		s->Phone.Data.Model[0]		  = 0;
		s->Phone.Data.Version[0]	  = 0;
		s->Phone.Data.VerDate[0]	  = 0;
		s->Phone.Data.VerNum		  = 0;
		s->Phone.Data.StartInfoCounter	  = 0;
		s->Phone.Data.SentMsg		  = NULL;

		s->Phone.Data.HardwareCache[0]	  = 0;
		s->Phone.Data.ProductCodeCache[0] = 0;
		s->Phone.Data.EnableIncomingCall  = false;
		s->Phone.Data.EnableIncomingSMS	  = false;
		s->Phone.Data.EnableIncomingCB	  = false;
		s->Phone.Data.EnableIncomingUSSD  = false;
		s->User.UserReplyFunctions	  = NULL;
		s->User.IncomingCall		  = NULL;
		s->User.IncomingSMS		  = NULL;
		s->User.IncomingCB		  = NULL;
		s->User.IncomingUSSD		  = NULL;
		s->User.SendSMSStatus		  = NULL;
		s->LockFile			  = NULL;
		s->opened			  = false;
		s->Phone.Functions		  = NULL;

		s->di 				  = di;
		s->di.use_global 		  = s->CurrentConfig->UseGlobalDebugFile;
		GSM_SetDebugLevel(s->CurrentConfig->DebugLevel, &s->di);
		error=GSM_SetDebugFile(s->CurrentConfig->DebugFile, &s->di);
		if (error != ERR_NONE) return error;

		smprintf_level(s, D_ERROR, "[Gammu            - %s built %s %s using %s]\n",
				VERSION,
				__TIME__,
				__DATE__,
				GetCompiler()
				);
		smprintf_level(s, D_ERROR, "[Connection       - \"%s\"]\n",
				s->CurrentConfig->Connection);
		smprintf_level(s, D_ERROR, "[Connection index - %d]\n", i);
		smprintf_level(s, D_ERROR, "[Model type       - \"%s\"]\n",
				s->CurrentConfig->Model);
		smprintf_level(s, D_ERROR, "[Device           - \"%s\"]\n",
				s->CurrentConfig->Device);
		if (strlen(GetOS()) != 0) {
			smprintf_level(s, D_ERROR, "[Runing on        - %s]\n",
					GetOS());
		}

		if (s->di.dl==DL_BINARY) {
			smprintf(s,"%c",((unsigned char)strlen(VERSION)));
			smprintf(s,"%s",VERSION);
		}

		error=GSM_RegisterAllConnections(s, s->CurrentConfig->Connection);
		if (error!=ERR_NONE) return error;

		/* Model auto */
		if (s->CurrentConfig->Model[0]==0) {
			if (strcasecmp(s->CurrentConfig->LockDevice,"yes") == 0) {
				error = lock_device(s->CurrentConfig->Device, &(s->LockFile));
				if (error != ERR_NONE) return error;
			}

			/* Irda devices can set now model to some specific and
			 * we don't have to make auto detection later */
			error=s->Device.Functions->OpenDevice(s);
			if (i != s->ConfigNum - 1) {
				if (error == ERR_DEVICEOPENERROR) 	continue;
				if (error == ERR_DEVICELOCKED) 	 	continue;
				if (error == ERR_DEVICENOTEXIST)  	continue;
				if (error == ERR_DEVICEBUSY) 		continue;
				if (error == ERR_DEVICENOPERMISSION) 	continue;
				if (error == ERR_DEVICENODRIVER) 	continue;
				if (error == ERR_DEVICENOTWORK) 	continue;
			}
 			if (error!=ERR_NONE) {
 				if (s->LockFile!=NULL) unlock_device(&(s->LockFile));
 				return error;
 			}

			s->opened = true;

			error=s->Protocol.Functions->Initialise(s);
			if (error!=ERR_NONE) return error;

			/* If still auto model, try to get model by asking phone for it */
			if (s->Phone.Data.Model[0]==0) {
				smprintf(s,"[Module           - \"auto\"]\n");
				switch (s->ConnectionType) {
#ifdef GSM_ENABLE_ATGEN
					case GCT_AT:
					case GCT_BLUEAT:
					case GCT_IRDAAT:
					case GCT_DKU2AT:
						s->Phone.Functions = &ATGENPhone;
						break;
#endif
#ifdef GSM_ENABLE_OBEXGEN
					case GCT_IRDAOBEX:
					case GCT_BLUEOBEX:
						s->Phone.Functions = &OBEXGENPhone;
						break;
#endif
#ifdef GSM_ENABLE_GNAPGEN
					case GCT_BLUEGNAPBUS:
					case GCT_IRDAGNAPBUS:
						s->Phone.Functions = &GNAPGENPhone;
						break;
#endif
#if defined(GSM_ENABLE_NOKIA_DCT3) || defined(GSM_ENABLE_NOKIA_DCT4)
					case GCT_MBUS2:
					case GCT_FBUS2:
					case GCT_FBUS2DLR3:
					case GCT_FBUS2PL2303:
					case GCT_FBUS2BLUE:
					case GCT_FBUS2IRDA:
					case GCT_DKU5FBUS2:
					case GCT_DKU2PHONET:
					case GCT_PHONETBLUE:
					case GCT_IRDAPHONET:
					case GCT_BLUEFBUS2:
					case GCT_BLUEPHONET:
						s->Phone.Functions = &NAUTOPhone;
						break;
#endif
					default:
						s->Phone.Functions = NULL;
				}
				if (s->Phone.Functions == NULL) return ERR_UNKNOWN;

				/* Please note, that AT module need to send first
				 * command for enabling echo
				 */
				error=s->Phone.Functions->Initialise(s);
				if (error == ERR_TIMEOUT && i != s->ConfigNum - 1) continue;
				if (error != ERR_NONE) return error;

				error=s->Phone.Functions->GetModel(s);
				if (error == ERR_TIMEOUT && i != s->ConfigNum - 1) continue;
				if (error != ERR_NONE) return error;
			}
		}

		/* Switching to "correct" module */
		error=GSM_RegisterAllPhoneModules(s);
		if (error!=ERR_NONE) return error;

		/* We didn't open device earlier ? Make it now */
		if (!s->opened) {
			if (strcasecmp(s->CurrentConfig->LockDevice,"yes") == 0) {
				error = lock_device(s->CurrentConfig->Device, &(s->LockFile));
				if (error != ERR_NONE) return error;
			}

			error=s->Device.Functions->OpenDevice(s);
			if (i != s->ConfigNum - 1) {
				if (error == ERR_DEVICEOPENERROR) 	continue;
				if (error == ERR_DEVICELOCKED) 	 	continue;
				if (error == ERR_DEVICENOTEXIST)  	continue;
				if (error == ERR_DEVICEBUSY) 		continue;
				if (error == ERR_DEVICENOPERMISSION) 	continue;
				if (error == ERR_DEVICENODRIVER) 	continue;
				if (error == ERR_DEVICENOTWORK) 	continue;
			}
			if (error!=ERR_NONE) {
				if (s->LockFile!=NULL) unlock_device(&(s->LockFile));
				return error;
			}

			s->opened = true;

			error=s->Protocol.Functions->Initialise(s);
			if (error!=ERR_NONE) return error;
		}

		error=s->Phone.Functions->Initialise(s);
		if (error == ERR_TIMEOUT && i != s->ConfigNum - 1) continue;
		if (error != ERR_NONE) return error;

		if (strcasecmp(s->CurrentConfig->StartInfo,"yes") == 0) {
			s->Phone.Functions->ShowStartInfo(s,true);
			s->Phone.Data.StartInfoCounter = 30;
		}

		if (strcasecmp(s->CurrentConfig->SyncTime,"yes") == 0) {
			GSM_GetCurrentDateTime (&current_time);
			s->Phone.Functions->SetDateTime(s,&current_time);
		}

		/* For debug it's good to have firmware and real model version and manufacturer */
		error=s->Phone.Functions->GetManufacturer(s);
		if (error == ERR_TIMEOUT && i != s->ConfigNum - 1) continue;
		if (error != ERR_NONE) return error;
		error=s->Phone.Functions->GetModel(s);
		if (error != ERR_NONE) return error;
		error=s->Phone.Functions->GetFirmware(s);
		if (error != ERR_NONE) return error;
		return ERR_NONE;
	}
	return ERR_UNCONFIGURED;
}

int GSM_ReadDevice (GSM_StateMachine *s, bool wait)
{
	unsigned char	buff[255];
	int		res = 0, count;

	unsigned int	i;
	GSM_DateTime	Date;

	GSM_GetCurrentDateTime (&Date);
	i=Date.Second;
	while (i==Date.Second) {
		res = s->Device.Functions->ReadDevice(s, buff, 255);
		if (!wait) break;
		if (res > 0) break;
		my_sleep(5);
		GSM_GetCurrentDateTime(&Date);
	}

	for (count = 0; count < res; count++)
		s->Protocol.Functions->StateMachine(s,buff[count]);

	return res;
}

GSM_Error GSM_TerminateConnection(GSM_StateMachine *s)
{
	GSM_Error error;

	if (!s->opened) return ERR_UNKNOWN;

	smprintf(s,"[Closing]\n");

	if (strcasecmp(s->CurrentConfig->StartInfo,"yes") == 0) {
		if (s->Phone.Data.StartInfoCounter > 0) s->Phone.Functions->ShowStartInfo(s,false);
	}

	if (s->Phone.Functions != NULL) {
		error=s->Phone.Functions->Terminate(s);
		if (error!=ERR_NONE) return error;
	}

	error=s->Protocol.Functions->Terminate(s);
	if (error!=ERR_NONE) return error;

	error = s->Device.Functions->CloseDevice(s);
	if (error!=ERR_NONE) return error;

	s->Phone.Data.ModelInfo		  = NULL;
	s->Phone.Data.Manufacturer[0]	  = 0;
	s->Phone.Data.Model[0]		  = 0;
	s->Phone.Data.Version[0]	  = 0;
	s->Phone.Data.VerDate[0]	  = 0;
	s->Phone.Data.VerNum		  = 0;

	if (s->LockFile!=NULL) unlock_device(&(s->LockFile));

	if (!s->di.use_global && s->di.dl!=0 && fileno(s->di.df) != 1 && fileno(s->di.df) != 2) fclose(s->di.df);

	s->opened = false;

	return ERR_NONE;
}

bool GSM_IsConnected(GSM_StateMachine *s) {
	return s->opened;
}

GSM_Error GSM_WaitForOnce(GSM_StateMachine *s, unsigned const char *buffer,
			  int length, unsigned char type, int timeout)
{
	GSM_Phone_Data			*Phone = &s->Phone.Data;
	GSM_Protocol_Message 		sentmsg;
	int				i;

	i=0;
	do {
		if (length != 0) {
			sentmsg.Length 	= length;
			sentmsg.Type	= type;
			sentmsg.Buffer 	= (unsigned char *)malloc(length);
			memcpy(sentmsg.Buffer,buffer,length);
			Phone->SentMsg  = &sentmsg;
		}

		/* Some data received. Reset timer */
		if (GSM_ReadDevice(s,true)!=0) i=0;

		if (length != 0) {
			free (sentmsg.Buffer);
			Phone->SentMsg  = NULL;
		}

		/* Request completed */
		if (Phone->RequestID==ID_None) return Phone->DispatchError;

		i++;
	} while (i<timeout);

	return ERR_TIMEOUT;
}

GSM_Error GSM_WaitFor (GSM_StateMachine *s, unsigned const char *buffer,
		       int length, unsigned char type, int timeout,
		       GSM_Phone_RequestID request)
{
	GSM_Phone_Data		*Phone = &s->Phone.Data;
	GSM_Error		error;
	int			reply;

	if (strcasecmp(s->CurrentConfig->StartInfo,"yes") == 0) {
		if (Phone->StartInfoCounter > 0) {
			Phone->StartInfoCounter--;
			if (Phone->StartInfoCounter == 0) s->Phone.Functions->ShowStartInfo(s,false);
		}
	}

	Phone->RequestID	= request;
	Phone->DispatchError	= ERR_TIMEOUT;

	for (reply=0;reply<s->ReplyNum;reply++) {
		if (reply!=0) {
			smprintf_level(s, D_ERROR, "[Retrying %i type 0x%02X]\n", reply, type);
		}
		error = s->Protocol.Functions->WriteMessage(s, buffer, length, type);
		if (error!=ERR_NONE) return error;

		error = GSM_WaitForOnce(s, buffer, length, type, timeout);
		if (error != ERR_TIMEOUT) return error;
        }

//	return Phone->DispatchError;
	return ERR_TIMEOUT;
}

static GSM_Error CheckReplyFunctions(GSM_StateMachine *s, GSM_Reply_Function *Reply, int *reply)
{
	GSM_Phone_Data			*Data	  = &s->Phone.Data;
	GSM_Protocol_Message		*msg	  = s->Phone.Data.RequestMsg;
	bool				execute;
	bool				available = false;
	int				i	  = 0;

	while (Reply[i].requestID!=ID_None) {
		execute=false;
		/* Binary frames like in Nokia */
		if (strlen(Reply[i].msgtype) < 2) {
			if (Reply[i].msgtype[0]==msg->Type) {
				if (Reply[i].subtypechar!=0) {
					if (Reply[i].subtypechar<=msg->Length) {
						if (msg->Buffer[Reply[i].subtypechar]==Reply[i].subtype)
							execute=true;
					}
				} else execute=true;
			}
		} else {
			if (strlen(Reply[i].msgtype) < msg->Length) {
				if (strncmp(Reply[i].msgtype,msg->Buffer,strlen(Reply[i].msgtype))==0) {
					execute=true;
				}
			}
		}

		if (execute) {
			*reply=i;
			if (Reply[i].requestID == ID_IncomingFrame ||
			    Reply[i].requestID == Data->RequestID ||
			    Data->RequestID    == ID_EachFrame) {
				return ERR_NONE;
			}
			available=true;
		}
		i++;
	}

	if (available) {
		return ERR_FRAMENOTREQUESTED;
	} else {
		return ERR_UNKNOWNFRAME;
	}
}

GSM_Error GSM_DispatchMessage(GSM_StateMachine *s)
{
	GSM_Error		error	= ERR_UNKNOWNFRAME;
	GSM_Protocol_Message	*msg 	= s->Phone.Data.RequestMsg;
	GSM_Phone_Data 		*Phone	= &s->Phone.Data;
	bool			disp    = false;
	GSM_Reply_Function	*Reply;
	int			reply;

	GSM_DumpMessageLevel2Recv(s, msg->Buffer, msg->Length, msg->Type);
	GSM_DumpMessageLevel3Recv(s, msg->Buffer, msg->Length, msg->Type);

	Reply=s->User.UserReplyFunctions;
	if (Reply!=NULL) error=CheckReplyFunctions(s,Reply,&reply);

	if (error==ERR_UNKNOWNFRAME) {
		Reply=s->Phone.Functions->ReplyFunctions;
		error=CheckReplyFunctions(s,Reply,&reply);
	}

	if (error==ERR_NONE) {
		error=Reply[reply].Function(*msg, s);
		if (Reply[reply].requestID==Phone->RequestID) {
			if (error == ERR_NEEDANOTHERANSWER) {
				error = ERR_NONE;
			} else {
				Phone->RequestID=ID_None;
			}
		}
	}

	if (strcmp(s->Phone.Functions->models,"NAUTO")) {
		disp = true;
		switch (error) {
		case ERR_UNKNOWNRESPONSE:
			smprintf_level(s, D_ERROR, "\nUNKNOWN response");
			break;
		case ERR_UNKNOWNFRAME:
			smprintf_level(s, D_ERROR, "\nUNKNOWN frame");
			break;
		case ERR_FRAMENOTREQUESTED:
			smprintf_level(s, D_ERROR, "\nFrame not request now");
			break;
		default:
			disp = false;
		}

		if (error == ERR_UNKNOWNFRAME || error == ERR_FRAMENOTREQUESTED) {
			error = ERR_TIMEOUT;
		}
	}

	if (disp) {
		smprintf(s,". If you can, please report it (see <http://cihar.com/gammu/report>). Thank you\n");
		if (Phone->SentMsg != NULL) {
			smprintf(s,"LAST SENT frame ");
			smprintf(s, "type 0x%02X/length %zd", Phone->SentMsg->Type, Phone->SentMsg->Length);
			DumpMessage(&s->di, Phone->SentMsg->Buffer, Phone->SentMsg->Length);
		}
		smprintf(s, "RECEIVED frame ");
		smprintf(s, "type 0x%02X/length 0x%02zX/%zd", msg->Type, msg->Length, msg->Length);
		DumpMessage(&s->di, msg->Buffer, msg->Length);
		smprintf(s, "\n");
	}

	return error;
}

GSM_Error GSM_FindGammuRC (INI_Section **result)
{
        char		*HomeDrive,*HomePath,*FileName=malloc(1);
	int		FileNameUsed=1;
	GSM_Error	error;
	GSM_Error	error2;

	*result = NULL;

	FileName[0] = 0;
#if defined(WIN32) || defined(DJGPP)
	FileName = (char *)realloc(FileName, MAX_PATH);

	if (!SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, FileName))) {
		HomeDrive = getenv("HOMEDRIVE");
		if (HomeDrive) {
			FileName 	=  realloc(FileName,FileNameUsed+strlen(HomeDrive)+1);
			FileName 	=  strcat(FileName, HomeDrive);
			FileNameUsed	+= strlen(HomeDrive)+1;
		}
		HomePath = getenv("HOMEPATH");
		if (HomePath) {
			FileName 	=  realloc(FileName,FileNameUsed+strlen(HomePath)+1);
			FileName 	=  strcat(FileName, HomePath);
			FileNameUsed	+= strlen(HomePath)+1;
		}
		FileName = (char *)realloc(FileName,FileNameUsed+8+1);
	}
        strcat(FileName, "\\gammurc");
#else
	HomeDrive = NULL;
        HomePath  = getenv("HOME");
        if (HomePath) {
		FileName 	=  realloc(FileName,FileNameUsed+strlen(HomePath)+1);
		FileName 	=  strcat(FileName, HomePath);
		FileNameUsed	+= strlen(HomePath)+1;
	}
	FileName = realloc(FileName,FileNameUsed+9+1);
        strcat(FileName, "/.gammurc");
#endif
	dbgprintf("Open config: \"%s\"\n", FileName);

	error = INI_ReadFile(FileName, false, result);
	free(FileName);
        if (error != ERR_NONE) {
		dbgprintf("Open fallback config: \"%s\"\n", FALLBACK_GAMMURC);
		error2 = INI_ReadFile(FALLBACK_GAMMURC, false, result);
		if (error2 == ERR_NONE) error = ERR_NONE;
        }

	return error;
}

GSM_Config *GSM_GetConfig(GSM_StateMachine *s, int num)
{
	if (num == -1) {
		return s->CurrentConfig;
	} else {
		if (num > MAX_CONFIG_NUM) return NULL;
		return &(s->Config[num]);
	}
}


int GSM_GetConfigNum(const GSM_StateMachine *s)
{
	return s->ConfigNum;
}

void GSM_SetConfigNum(GSM_StateMachine *s, int sections)
{
	if (sections > MAX_CONFIG_NUM) return;
	s->ConfigNum = sections;
}

/**
 * Expand path to user home.
 */
void GSM_ExpandUserPath(char **string)
{
	char *tmp, *home;

	/* Is there something to expand */
	if (*string[0] != '~') return;

	/* Grab home */
	home = getenv("HOME");
	if (home == NULL) return;

	/* Allocate memory */
	tmp = (char *)malloc(strlen(home) + strlen(*string));
	if (tmp == NULL) return;

	/* Create final path */
	strcpy(tmp, home);
	strcat(tmp, *string + 1);

	/* Free old storage and replace it */
	free(*string);
	*string = tmp;
}

bool GSM_FallbackConfig = false;

bool GSM_ReadConfig(INI_Section *cfg_info, GSM_Config *cfg, int num)
{
	INI_Section 	*h;
	unsigned char 	section[50];
	bool		found = false;
	char *Temp;

#if defined(WIN32) || defined(DJGPP)
        static const char *DefaultPort		= "com2:";
#else
        static const char *DefaultPort		= "/dev/ttyS1";
#endif
        static const char *DefaultModel		= "";
        static const char *DefaultConnection		= "fbus";
	static const char *DefaultSynchronizeTime	= "no";
	static const char *DefaultDebugFile		= "";
	static const char *DefaultDebugLevel		= "";
	static const char *DefaultLockDevice		= "no";
	static const char *DefaultStartInfo		= "no";

	/* By default all debug output will go to one filedescriptor */
	static const bool DefaultUseGlobalDebugFile 	= true;

	cfg->UseGlobalDebugFile	 = DefaultUseGlobalDebugFile;

	/* If we don't have valid config, bail out */
	if (cfg_info==NULL) goto fail;

	/* Which section should we read? */
	if (num == 0) {
		sprintf(section,"gammu");
	} else {
		sprintf(section,"gammu%i",num);
	}

	/* Scan for section */
        for (h = cfg_info; h != NULL; h = h->Next) {
                if (strncasecmp(section, h->SectionName, strlen(section)) == 0) {
			found = true;
			break;
		}
        }
	if (!found) goto fail;

	/* Set device name */
	free(cfg->Device);
	cfg->Device 	 = INI_GetValue(cfg_info, section, "port", 		false);
	if (!cfg->Device) {
		cfg->Device		 	 = strdup(DefaultPort);
	} else {
		cfg->Device			 = strdup(cfg->Device);
	}

	/* Set connection type */
	free(cfg->Connection);
	cfg->Connection  = INI_GetValue(cfg_info, section, "connection", 	false);
	if (cfg->Connection == NULL) {
		cfg->Connection	 		 = strdup(DefaultConnection);
	} else {
		cfg->Connection			 = strdup(cfg->Connection);
	}

	/* Set time sync */
	free(cfg->SyncTime);
	cfg->SyncTime 	 = INI_GetValue(cfg_info, section, "synchronizetime",	false);
	if (!cfg->SyncTime) {
		cfg->SyncTime		 	 = strdup(DefaultSynchronizeTime);
	} else {
		cfg->SyncTime			 = strdup(cfg->SyncTime);
	}

	/* Set debug file */
	free(cfg->DebugFile);
	cfg->DebugFile   = INI_GetValue(cfg_info, section, "logfile", 		false);
	if (!cfg->DebugFile) {
		cfg->DebugFile		 	 = strdup(DefaultDebugFile);
	} else {
		cfg->DebugFile			 = strdup(cfg->DebugFile);
		GSM_ExpandUserPath(&cfg->DebugFile);
	}

	/* Set file locking */
	free(cfg->LockDevice);
	cfg->LockDevice  = INI_GetValue(cfg_info, section, "use_locking", 	false);
	if (!cfg->LockDevice) {
		cfg->LockDevice	 		 = strdup(DefaultLockDevice);
	} else {
		cfg->LockDevice			 = strdup(cfg->LockDevice);
	}

	/* Set model */
	Temp		 = INI_GetValue(cfg_info, section, "model", 		false);
	if (!Temp) {
		strcpy(cfg->Model,DefaultModel);
	} else {
		strcpy(cfg->Model,Temp);
	}

	/* Set Log format */
	Temp		 = INI_GetValue(cfg_info, section, "logformat", 	false);
	if (!Temp) {
		strcpy(cfg->DebugLevel,DefaultDebugLevel);
	} else {
		strcpy(cfg->DebugLevel,Temp);
	}

	/* Set startup info */
	free(cfg->StartInfo);
	cfg->StartInfo   = INI_GetValue(cfg_info, section, "startinfo", 	false);
	if (!cfg->StartInfo) {
		cfg->StartInfo	 		 = strdup(DefaultStartInfo);
	} else {
		cfg->StartInfo			 = strdup(cfg->StartInfo);
	}

	/* Read localised strings for some phones */

	Temp		 = INI_GetValue(cfg_info, section, "reminder", 		false);
	if (!Temp) {
		strcpy(cfg->TextReminder,"Reminder");
	} else {
		strcpy(cfg->TextReminder,Temp);
	}

	Temp		 = INI_GetValue(cfg_info, section, "meeting", 		false);
	if (!Temp) {
		strcpy(cfg->TextMeeting,"Meeting");
	} else {
		strcpy(cfg->TextMeeting,Temp);
	}

	Temp		 = INI_GetValue(cfg_info, section, "call", 		false);
	if (!Temp) {
		strcpy(cfg->TextCall,"Call");
	} else {
		strcpy(cfg->TextCall,Temp);
	}

	Temp		 = INI_GetValue(cfg_info, section, "birthday", 		false);
	if (!Temp) {
		strcpy(cfg->TextBirthday,"Birthday");
	} else {
		strcpy(cfg->TextBirthday,Temp);
	}

	Temp		 = INI_GetValue(cfg_info, section, "memo", 		false);
	if (!Temp) {
		strcpy(cfg->TextMemo,"Memo");
	} else {
		strcpy(cfg->TextMemo,Temp);
	}

	return true;

fail:
	/* Special case, this config needs to be somehow valid */
	if (num == 0) {
		cfg->Device		 	 = strdup(DefaultPort);
		cfg->Connection	 		 = strdup(DefaultConnection);
		cfg->SyncTime		 	 = strdup(DefaultSynchronizeTime);
		cfg->DebugFile		 	 = strdup(DefaultDebugFile);
		cfg->LockDevice	 		 = strdup(DefaultLockDevice);
		strcpy(cfg->Model,DefaultModel);
		strcpy(cfg->DebugLevel,DefaultDebugLevel);
		cfg->StartInfo	 		 = strdup(DefaultStartInfo);
		strcpy(cfg->TextReminder,"Reminder");
		strcpy(cfg->TextMeeting,"Meeting");
		strcpy(cfg->TextCall,"Call");
		strcpy(cfg->TextBirthday,"Birthday");
		strcpy(cfg->TextMemo,"Memo");
		/* Indicate that we used defaults */
		GSM_FallbackConfig = true;
	}
	return false;
}

void GSM_DumpMessageLevel2_Text(GSM_StateMachine *s, unsigned const char *message, int messagesize, int type, const char *text)
{
	if (s->di.dl==DL_TEXT || s->di.dl==DL_TEXTALL ||
	    s->di.dl==DL_TEXTDATE || s->di.dl==DL_TEXTALLDATE) {
		smprintf(s, "%s", text);
		smprintf(s, "type 0x%02X/length 0x%02X/%i",
				type, messagesize, messagesize);
		DumpMessage(&s->di, message, messagesize);
		if (messagesize == 0)
			smprintf(s,"\n");
	}
}

void GSM_DumpMessageLevel2(GSM_StateMachine *s, unsigned const char *message, int messagesize, int type)
{
	return GSM_DumpMessageLevel2_Text(s, message, messagesize, type, "SENDING frame");
}

void GSM_DumpMessageLevel2Recv(GSM_StateMachine *s, unsigned const char *message, int messagesize, int type)
{
	return GSM_DumpMessageLevel2_Text(s, message, messagesize, type, "RECEIVED frame");
}

void GSM_DumpMessageLevel3_Custom(GSM_StateMachine *s, unsigned const char *message, int messagesize, int type, int direction)
{
	int i;

	if (s->di.dl==DL_BINARY) {
		smprintf(s,"%c", direction);
		smprintf(s,"%c",type);
		smprintf(s,"%c",messagesize/256);
		smprintf(s,"%c",messagesize%256);
		for (i=0;i<messagesize;i++) smprintf(s,"%c",message[i]);
	}
}
void GSM_DumpMessageLevel3(GSM_StateMachine *s, unsigned const char *message, int messagesize, int type)
{
	return GSM_DumpMessageLevel3_Custom(s, message, messagesize, type, 0x01);
}

void GSM_DumpMessageLevel3Recv(GSM_StateMachine *s, unsigned const char *message, int messagesize, int type)
{
	return GSM_DumpMessageLevel3_Custom(s, message, messagesize, type, 0x02);
}

PRINTF_STYLE(2, 3)
int smprintf(GSM_StateMachine *s, const char *format, ...)
{
	va_list		argp;
	int 		result=0;
	char		buffer[2000];

	va_start(argp, format);

	if ((s != NULL && s->di.df != 0) || (s == NULL && di.df != 0)) {
		result = vsprintf(buffer, format, argp);
		result = smfprintf((s == NULL) ? &di : &(s->di), "%s", buffer);
	}

	va_end(argp);
	return result;
}

PRINTF_STYLE(3, 4)
int smprintf_level(GSM_StateMachine * s, GSM_DebugSeverity severity, const char *format, ...)
{
	va_list		argp;
	int 		result=0;
	char		buffer[2000];

	if (severity == D_TEXT) {
		if (s->di.dl != DL_TEXT &&
				s->di.dl != DL_TEXTALL &&
				s->di.dl != DL_TEXTDATE &&
				s->di.dl != DL_TEXTALLDATE) {
			return 0;
		}
	} else if (severity == D_ERROR) {
		if (s->di.dl != DL_TEXT &&
				s->di.dl != DL_TEXTALL &&
				s->di.dl != DL_TEXTDATE &&
				s->di.dl != DL_TEXTALLDATE &&
				s->di.dl != DL_TEXTERROR &&
				s->di.dl != DL_TEXTERRORDATE) {
			return 0;
		}
	}
	va_start(argp, format);

	if ((s != NULL && s->di.df != 0) || (s == NULL && di.df != 0)) {
		result = vsprintf(buffer, format, argp);
		result = smfprintf((s == NULL) ? &di : &(s->di), "%s", buffer);
	}

	va_end(argp);
	return result;
}

void GSM_OSErrorInfo(GSM_StateMachine *s, char *description)
{
#ifdef WIN32
	int 		i;
	unsigned char 	*lpMsgBuf;

	/* We don't use errno in win32 - GetLastError gives better info */
	if (GetLastError() != 0) {
		if (s->di.dl == DL_TEXTERROR || s->di.dl == DL_TEXT || s->di.dl == DL_TEXTALL ||
		    s->di.dl == DL_TEXTERRORDATE || s->di.dl == DL_TEXTDATE || s->di.dl == DL_TEXTALLDATE) {
			FormatMessage(
				FORMAT_MESSAGE_ALLOCATE_BUFFER |
				FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL,
				GetLastError(),
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
				(LPTSTR) &lpMsgBuf,
				0,
				NULL
			);
			for (i=0;i<(int)strlen(lpMsgBuf);i++) {
				if (lpMsgBuf[i] == 13 || lpMsgBuf[i] == 10) {
					lpMsgBuf[i] = ' ';
				}
			}
			smprintf(s,"[System error     - %s, %i, \"%s\"]\n", description, (int)GetLastError(), (LPCTSTR)lpMsgBuf);
			LocalFree(lpMsgBuf);
		}
	}
	return;
#endif

	if (errno!=-1) {
		if (s->di.dl == DL_TEXTERROR || s->di.dl == DL_TEXT || s->di.dl == DL_TEXTALL ||
		    s->di.dl == DL_TEXTERRORDATE || s->di.dl == DL_TEXTDATE || s->di.dl == DL_TEXTALLDATE) {
			smprintf(s,"[System error     - %s, %i, \"%s\"]\n",description,errno,strerror(errno));
		}
	}
}

#ifdef GSM_ENABLE_BACKUP

void GSM_GetPhoneFeaturesForBackup(GSM_StateMachine *s, GSM_Backup_Info *info)
{
	GSM_Error 		error;
	GSM_MemoryStatus	MemStatus;
	GSM_ToDoStatus		ToDoStatus;
	GSM_CalendarEntry       Note;
	GSM_WAPBookmark		Bookmark;
	GSM_MultiWAPSettings	WAPSettings;
 	GSM_FMStation		FMStation;
 	GSM_GPRSAccessPoint	GPRSPoint;
//	GSM_Profile		Profile;

	if (info->PhonePhonebook) {
		MemStatus.MemoryType = MEM_ME;
                error=s->Phone.Functions->GetMemoryStatus(s, &MemStatus);
		if (error==ERR_NONE && MemStatus.MemoryUsed != 0) {
		} else {
			info->PhonePhonebook = false;
		}
	}
	if (info->SIMPhonebook) {
		MemStatus.MemoryType = MEM_SM;
                error=s->Phone.Functions->GetMemoryStatus(s, &MemStatus);
		if (error==ERR_NONE && MemStatus.MemoryUsed != 0) {
		} else {
			info->SIMPhonebook = false;
		}
	}
	if (info->Calendar) {
		error=s->Phone.Functions->GetNextCalendar(s,&Note,true);
		if (error!=ERR_NONE) info->Calendar = false;
	}
	if (info->ToDo) {
		error=s->Phone.Functions->GetToDoStatus(s,&ToDoStatus);
		if (error == ERR_NONE && ToDoStatus.Used != 0) {
		} else {
			info->ToDo = false;
		}
	}
	if (info->WAPBookmark) {
		Bookmark.Location = 1;
		error=s->Phone.Functions->GetWAPBookmark(s,&Bookmark);
		if (error == ERR_NONE) {
		} else {
			info->WAPBookmark = false;
		}
	}
	if (info->WAPSettings) {
		WAPSettings.Location = 1;
		error=s->Phone.Functions->GetWAPSettings(s,&WAPSettings);
		if (error == ERR_NONE) {
		} else {
			info->WAPSettings = false;
		}
	}
	if (info->MMSSettings) {
		WAPSettings.Location = 1;
		error=s->Phone.Functions->GetMMSSettings(s,&WAPSettings);
		if (error == ERR_NONE) {
		} else {
			info->WAPSettings = false;
		}
	}
	if (info->FMStation) {
 		FMStation.Location = 1;
 		error = s->Phone.Functions->GetFMStation(s,&FMStation);
 	        if (error == ERR_NONE || error == ERR_EMPTY) {
		} else {
			info->FMStation = false;
		}
	}
	if (info->GPRSPoint) {
 		GPRSPoint.Location = 1;
 		error = s->Phone.Functions->GetGPRSAccessPoint(s,&GPRSPoint);
 	        if (error == ERR_NONE || error == ERR_EMPTY) {
		} else {
			info->GPRSPoint = false;
		}
	}
}
#endif

void GSM_SetIncomingCallCallback(GSM_StateMachine *s, IncomingCallCallback callback)
{
	s->User.IncomingCall = callback;
}

void GSM_SetIncomingSMSCallback(GSM_StateMachine *s, IncomingSMSCallback callback)
{
	s->User.IncomingSMS = callback;
}

void GSM_SetIncomingCBCallback(GSM_StateMachine *s, IncomingCBCallback callback)
{
	s->User.IncomingCB = callback;
}

void GSM_SetIncomingUSSDCallback(GSM_StateMachine *s, IncomingUSSDCallback callback)
{
	s->User.IncomingUSSD = callback;
}

void GSM_SetSendSMSStatusCallback(GSM_StateMachine *s, SendSMSStatusCallback callback)
{
	s->User.SendSMSStatus = callback;
}

GSM_StateMachine *GSM_AllocStateMachine(void)
{
	return (GSM_StateMachine *)calloc(1, sizeof(GSM_StateMachine));
}

void GSM_FreeStateMachine(GSM_StateMachine *s)
{
	int i;

	/* Free allocated memory */
	for (i = 0; i <= MAX_CONFIG_NUM; i++) {
		free(s->Config[i].Device);
		free(s->Config[i].Connection);
		free(s->Config[i].SyncTime);
		free(s->Config[i].DebugFile);
		free(s->Config[i].LockDevice);
		free(s->Config[i].StartInfo);
	}

	free(s);
}


GSM_ConnectionType GSM_GetUsedConnection(GSM_StateMachine *s)
{
	return s->ConnectionType;
}

OnePhoneModel *GSM_GetModelInfo(GSM_StateMachine *s)
{
	return s->Phone.Data.ModelInfo;
}

GSM_Debug_Info *GSM_GetDebug(GSM_StateMachine *s)
{
	return &(s->di);
}


/* How should editor hadle tabs in this file? Add editor commands here.
 * vim: noexpandtab sw=8 ts=8 sts=8:
 */
