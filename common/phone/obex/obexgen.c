/* (c) 2003 by Marcin Wiacek */
/* (c) 2006 by Michal Cihar */

/** \file obexgen.c
 * \defgroup OBEXPhone OBEX phones communication
 * Generic OBEX access to phones, made addording to OBEX specification
 * version 1.3 and IrMC specification version 1.1 as available from IrDA
 * <http://www.irda.org>.
 *
 * @author Michal Cihar
 * @author Marcin Wiacek
 * @{
 */

#include <string.h>
#include <time.h>

#include "../../misc/coding/coding.h"
#include "../../gsmcomon.h"
#include "../../gsmstate.h"
#include "../../service/gsmmisc.h"
#include "../../protocol/obex/obex.h"
#include "obexfunc.h"

#ifdef GSM_ENABLE_OBEXGEN

/* Forward definitions */
GSM_Error OBEXGEN_GetNextFileFolder(GSM_StateMachine *s, GSM_File *File, bool start);
GSM_Error OBEXGEN_GetTextFile(GSM_StateMachine *s, const char *FileName, char ** Buffer);
GSM_Error OBEXGEN_GetModel(GSM_StateMachine *s);


/**
 * \defgroup OBEXinit OBEX initialisation and terminating
 * \ingroup OBEXPhone
 * @{
 */

static GSM_Error OBEXGEN_ReplyConnect(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	switch (msg.Type) {
	case 0xA0:
		smprintf(s,"Connected/disconnected OK\n");
		if (msg.Length != 0) {
			s->Phone.Data.Priv.OBEXGEN.FrameSize = msg.Buffer[2]*256+msg.Buffer[3];
			smprintf(s,"Maximal size of frame is %i 0x%x\n",s->Phone.Data.Priv.OBEXGEN.FrameSize,s->Phone.Data.Priv.OBEXGEN.FrameSize);
		}
		return ERR_NONE;
	case 0xC0:
		smprintf(s, "Wrong request sent to phone!\n");
		return ERR_BUG;
	}
	return ERR_UNKNOWNRESPONSE;
}

/**
 * Disconnects from OBEX service
 */
GSM_Error OBEXGEN_Disconnect(GSM_StateMachine *s)
{
	smprintf(s, "Disconnecting\n");
	return GSM_WaitFor (s, NULL, 0, 0x81, 2, ID_Initialise);
}

/**
 * Connects to OBEX using selected service
 */
GSM_Error OBEXGEN_Connect(GSM_StateMachine *s, OBEX_Service service)
{
	GSM_Error	error = ERR_NONE;
	int		Current=4;
	unsigned char 	req2[200];
	unsigned char 	req[200] = {
		0x10,			/* Version 1.0 			*/
		0x00,			/* no flags 			*/
		0x04,0x00};		/* 0x2000 max size of packet 	*/

	/* Are we requsted for initial service? */
	if (service == 0) {
		/* If not set, stay as we are configured now */
		if (s->Phone.Data.Priv.OBEXGEN.InitialService == 0) {
			return ERR_NONE;
		}
		service = s->Phone.Data.Priv.OBEXGEN.InitialService;
	}

	/* Don't we already have correct service? */
	if (service == s->Phone.Data.Priv.OBEXGEN.Service) return ERR_NONE;

	/* Disconnect from current service, if we were connected */
	if (s->Phone.Data.Priv.OBEXGEN.Service != 0) {
		error = OBEXGEN_Disconnect(s);
		if (error != ERR_NONE) return error;
	}

	switch (service) {
	case OBEX_None:
		smprintf(s, "No service requested\n");
		break;
	case OBEX_IRMC:
		smprintf(s, "IrMC service requested\n");
		/* IrMC Service UUID */
		req2[0] = 'I'; req2[1] = 'R'; req2[2] = 'M';
		req2[3] = 'C'; req2[4] = '-'; req2[5] = 'S';
		req2[6] = 'Y'; req2[7] = 'N'; req2[8] = 'C';

		/* Target block */
		OBEXAddBlock(req, &Current, 0x46, req2, 9);
		break;
	case OBEX_BrowsingFolders:
		smprintf(s, "Folder Browsing service requested\n");
		/* Folder Browsing Service UUID */
		req2[0] = 0xF9; req2[1] = 0xEC; req2[2] = 0x7B;
		req2[3] = 0xC4; req2[4] = 0x95; req2[5] = 0x3C;
		req2[6] = 0x11; req2[7] = 0xD2; req2[8] = 0x98;
		req2[9] = 0x4E; req2[10]= 0x52; req2[11]= 0x54;
		req2[12]= 0x00; req2[13]= 0xDC; req2[14]= 0x9E;
		req2[15]= 0x09;

		/* Target block */
		OBEXAddBlock(req, &Current, 0x46, req2, 16);
		break;
	}

	/* Remember current service */
	s->Phone.Data.Priv.OBEXGEN.Service = service;

	smprintf(s, "Connecting\n");
	return GSM_WaitFor (s, req, Current, 0x80, 2, ID_Initialise);
}

/**
 * Initializes OBEX internal variables. To be used by other who need
 * OBEX protocol, but don't need it's init.
 */
GSM_Error OBEXGEN_InitialiseVars(GSM_StateMachine *s)
{
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;

	Priv->Service = 0;
	Priv->InitialService = 0;
	Priv->PbLUID = NULL;
	Priv->PbData = NULL;
	Priv->PbLUIDCount = 0;
	Priv->PbCount = -1;
	Priv->PbIEL = -1;
	Priv->CalLUID = NULL;
	Priv->CalData = NULL;
	Priv->CalLUIDCount = 0;
	Priv->TodoLUID = NULL;
	Priv->TodoLUIDCount = 0;
	Priv->CalIEL = -1;
	Priv->CalCount = -1;
	Priv->TodoCount = -1;
	Priv->PbOffsets = NULL;
	Priv->CalOffsets = NULL;
	Priv->TodoOffsets = NULL;
	Priv->UpdateCalLUID = false;
	Priv->UpdatePbLUID = false;
	Priv->UpdateTodoLUID = false;
	Priv->OBEXCapability = NULL;
	Priv->OBEXDevinfo = NULL;

	return ERR_NONE;
}


/**
 * Initializes OBEX connection in desired mode as defined by config.
 */
GSM_Error OBEXGEN_Initialise(GSM_StateMachine *s)
{
	GSM_Error	error = ERR_NONE;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;

	/* Init variables */
	error = OBEXGEN_InitialiseVars(s);
	if (error != ERR_NONE) return error;


	s->Phone.Data.VerNum 		= 0;
	s->Phone.Data.Version[0] 	= 0;
	s->Phone.Data.Manufacturer[0] 	= 0;

	/* Detect connection for desired type */
	Priv->InitialService = OBEX_BrowsingFolders;
	smprintf(s, "Connected using model %s\n", s->CurrentConfig->Model);
	if (strcmp(s->CurrentConfig->Model, "obex") == 0) {
		Priv->InitialService = OBEX_BrowsingFolders;
	} else if (strcmp(s->CurrentConfig->Model, "obexirmc") == 0) {
		Priv->InitialService = OBEX_IRMC;
	} else if (strcmp(s->CurrentConfig->Model, "seobex") == 0) {
		Priv->InitialService = OBEX_IRMC;
	} else if (strcmp(s->CurrentConfig->Model, "obexnone") == 0) {
		Priv->InitialService = OBEX_None;
	}

	/* Grab OBEX capability */
	error = OBEXGEN_Connect(s, OBEX_BrowsingFolders);
	if (error == ERR_NONE) {
		error = OBEXGEN_GetTextFile(s, "", &(Priv->OBEXCapability));
	}

	/* Grab IrMC devinfo */
	error = OBEXGEN_Connect(s, OBEX_IRMC);
	if (error == ERR_NONE) {
		error = OBEXGEN_GetTextFile(s, "", &(Priv->OBEXDevinfo));
	}

	/* Initialise connection */
	error = OBEXGEN_Connect(s, Priv->InitialService);
	if (error != ERR_NONE) return error;

	return ERR_NONE;
}

/**
 * Terminates OBEX connection.
 */
GSM_Error OBEXGEN_Terminate(GSM_StateMachine *s)
{
	return OBEXGEN_Disconnect(s);
}

/*@}*/

/**
 * \defgroup OBEXfiles OBEX transfer implementation
 * \ingroup OBEXPhone
 * @{
 */

/**
 * Merges filename from path and file
 */
void OBEXGEN_CreateFileName(unsigned char *Dest, unsigned char *Path, unsigned char *Name)
{
	size_t len;

	/* Folder name */
	CopyUnicodeString(Dest, Path);
	len = UnicodeLength(Dest);
	/* Append slash */
	if (len > 0) {
		Dest[2*len + 0] = 0;
		Dest[2*len + 1] = '/';
		len++;
	}
	/* And add filename */
	CopyUnicodeString(Dest + 2*len, Name);
}

/**
 * Grabs path part from complete path
 */
static void OBEXGEN_FindNextDir(unsigned char *Path, int *Pos, unsigned char *Return)
{
	int Retlen = 0;

	while(1) {
		if (Path[(*Pos)*2] == 0 && Path[(*Pos)*2+1] == 0) break;
		if (Path[(*Pos)*2] == 0 && Path[(*Pos)*2+1] == '/') {
			(*Pos)++;
			break;
		}
		Return[Retlen*2]     = Path[(*Pos)*2];
		Return[Retlen*2+1]   = Path[(*Pos)*2+1];
		(*Pos)++;
		Retlen++;
	}
	Return[Retlen*2]     = 0;
	Return[Retlen*2+1]   = 0;
}

/**
 * Reply handler for changing path
 */
static GSM_Error OBEXGEN_ReplyChangePath(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	switch (msg.Type) {
	case 0xA0:
		smprintf(s,"Path set OK\n");
		return ERR_NONE;
	case 0xA1:
		smprintf(s,"Folder created\n");
		return ERR_NONE;
	case 0xC1:
	case 0xC3:
		smprintf(s,"Security error\n");
		return ERR_PERMISSION;
	case 0xC4:
		smprintf(s,"File not found\n");
		return ERR_FILENOTEXIST;
	}
	return ERR_UNKNOWNRESPONSE;
}

/**
 * Changes current path on OBEX device.
 */
static GSM_Error OBEXGEN_ChangePath(GSM_StateMachine *s, char *Name, unsigned char Flag)
{
	unsigned char 	req[400];
	int		Current = 2;

	/* Flags */
	req[0] = Flag;
	req[1] = 0x00;

	/* Name block */
	if (Name != NULL && UnicodeLength(Name) != 0) {
		OBEXAddBlock(req, &Current, 0x01, Name, UnicodeLength(Name)*2+2);
	} else {
		OBEXAddBlock(req, &Current, 0x01, NULL, 0);
	}

	/* connection ID block */
	req[Current++] = 0xCB; // ID
	req[Current++] = 0x00; req[Current++] = 0x00;
	req[Current++] = 0x00; req[Current++] = 0x01;

	return GSM_WaitFor (s, req, Current, 0x85, 4, ID_SetPath);
}

/**
 * Changes current path on OBEX device to match GSM_File one.
 *
 * @param s State machine
 * @param File File which path we want to set
 * @param DirOnly Whether to descend only do directory name of path or full path (/foo or /foo/bar.png)
 * @param Buffer Optional buffer for storing last path part. Not used if NULL.
 */
static GSM_Error OBEXGEN_ChangeToFilePath(GSM_StateMachine *s, char *File, bool DirOnly, unsigned char *Buffer)
{
	GSM_Error		error;
	unsigned int		Pos;
	unsigned char		*req, req2[200];

	if (Buffer == NULL) {
		req = req2;
	} else {
		req = Buffer;
	}

	smprintf(s,"Changing to root\n");
	error = OBEXGEN_ChangePath(s, NULL, 2);
	if (error != ERR_NONE) return error;

	Pos = 0;
	do {
		OBEXGEN_FindNextDir(File, &Pos, req);
		if (DirOnly && Pos == UnicodeLength(File)) break;
		smprintf(s,"Changing path down to %s (%d, %d)\n", DecodeUnicodeString(req), Pos, UnicodeLength(File));
		error=OBEXGEN_ChangePath(s, req, 2);
		if (error != ERR_NONE) return error;
		if (Pos == UnicodeLength(File)) break;
	} while (1);

	return ERR_NONE;
}

/**
 * Reply handler for most file write operations.
 */
static GSM_Error OBEXGEN_ReplyAddFilePart(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	int Pos=0, pos2, len2;
	char *LUID = NULL;
	char *timestamp = NULL;
	char *CC = NULL;
	bool UpdatePbLUID, UpdateCalLUID, UpdateTodoLUID;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;

	UpdatePbLUID = Priv->UpdatePbLUID;
	Priv->UpdatePbLUID = false;
	UpdateCalLUID = Priv->UpdateCalLUID;
	Priv->UpdateCalLUID = false;
	UpdateTodoLUID = Priv->UpdateTodoLUID;
	Priv->UpdateTodoLUID = false;
	switch (msg.Type) {
	case 0x90:
		smprintf(s,"Last part of file added OK\n");
		return ERR_NONE;
	case 0xA0:
		smprintf(s,"Part of file added OK\n");
		while(1) {
			if (Pos >= msg.Length) break;
			switch (msg.Buffer[Pos]) {
			case 0x4C:
				smprintf(s, "Application data received:");
				len2 =  msg.Buffer[Pos+1] * 256 + msg.Buffer[Pos+2];
				pos2 = 0;
				while(1) {
					if (pos2 >= len2) break;
					switch (msg.Buffer[Pos + 3 + pos2]) {
						case 0x01:
							LUID = malloc(msg.Buffer[Pos + 3 + pos2 + 1]+1);
							memcpy(LUID,msg.Buffer + Pos + 3 + pos2 + 2, msg.Buffer[Pos + 3 + pos2 + 1]);
							LUID[msg.Buffer[Pos + 3 + pos2 + 1]]=0;
							smprintf(s, " LUID=\"%s\"", LUID);
							break;
						case 0x02:
							CC = malloc(msg.Buffer[Pos + 3 + pos2 + 1]+1);
							memcpy(CC,msg.Buffer + Pos + 3 + pos2 + 2, msg.Buffer[Pos + 3 + pos2 + 1]);
							CC[msg.Buffer[Pos + 3 + pos2 + 1]]=0;
							smprintf(s, " CC=\"%s\"", CC);
							break;
						case 0x03:
							timestamp = malloc(msg.Buffer[Pos + 3 + pos2 + 1]+1);
							memcpy(timestamp,msg.Buffer + Pos + 3 + pos2 + 2, msg.Buffer[Pos + 3 + pos2 + 1]);
							timestamp[msg.Buffer[Pos + 3 + pos2 + 1]] = 0;
							smprintf(s, " Timestamp=\"%s\"", timestamp);
							break;
					}
					pos2 += 2 + msg.Buffer[Pos + 3 + pos2 + 1];
				}
				smprintf(s, "\n");
				if (timestamp != NULL) {
					free(timestamp);
				}
				if (CC != NULL) {
					free(CC);
				}
				if (LUID != NULL) {
					if (UpdatePbLUID) {
						Priv->PbLUIDCount++;
						Priv->PbLUID = realloc(Priv->PbLUID, (Priv->PbLUIDCount + 1) * sizeof(char *));
						if (Priv->PbLUID == NULL) {
							return ERR_MOREMEMORY;
						}
						Priv->PbLUID[Priv->PbLUIDCount] = LUID;
					} else if (UpdateTodoLUID) {
						Priv->TodoLUIDCount++;
						Priv->TodoLUID = realloc(Priv->TodoLUID, (Priv->TodoLUIDCount + 1) * sizeof(char *));
						if (Priv->TodoLUID == NULL) {
							return ERR_MOREMEMORY;
						}
						Priv->TodoLUID[Priv->TodoLUIDCount] = LUID;
					} else if (UpdateCalLUID) {
						Priv->CalLUIDCount++;
						Priv->CalLUID = realloc(Priv->CalLUID, (Priv->CalLUIDCount + 1) * sizeof(char *));
						if (Priv->CalLUID == NULL) {
							return ERR_MOREMEMORY;
						}
						Priv->CalLUID[Priv->CalLUIDCount] = LUID;
					} else {
						free(LUID);
					}
				}
				Pos += len2;
				break;
			case 0xc3:
				/* Length */
				/**
				 * @todo Lenght is ignored now
				 */
				Pos += 5;
				break;
			case 0xcb:
				/* Skip Connection ID (we ignore this for now) */
				Pos += 5;
				break;
			default:
				Pos+=msg.Buffer[Pos+1]*256+msg.Buffer[Pos+2];
				break;
			}
		}
		return ERR_NONE;
	case 0xC0:
		smprintf(s,"Not understand. Probably not supported\n");
		return ERR_NOTSUPPORTED;
	case 0xC1:
	case 0xC3:
		smprintf(s,"Security error\n");
		return ERR_PERMISSION;
	case 0xC4:
		smprintf(s,"File not found\n");
		return ERR_FILENOTEXIST;
	}
	return ERR_UNKNOWNRESPONSE;
}

GSM_Error OBEXGEN_PrivAddFilePart(GSM_StateMachine *s, GSM_File *File, int *Pos, int *Handle)
{
	GSM_Error		error;
	int			j;
	unsigned int		Current = 0;
	unsigned char 		req[2000];

	s->Phone.Data.File = File;

	if (*Pos == 0) {
		if (!strcmp(DecodeUnicodeString(File->ID_FullName),"")) {
			error = OBEXGEN_Connect(s,OBEX_None);
			if (error != ERR_NONE) return error;
		} else {
			if (s->Phone.Data.Priv.OBEXGEN.Service == OBEX_BrowsingFolders) {
				error = OBEXGEN_ChangeToFilePath(s, File->ID_FullName, false, NULL);
				if (error != ERR_NONE) return error;
			}
		}

		/* Name block */
		OBEXAddBlock(req, &Current, 0x01, File->Name, UnicodeLength(File->Name)*2+2);

		/* File size block */
		req[Current++] = 0xC3; // ID
		req[Current++] = 0;
		req[Current++] = 0;
		req[Current++] = File->Used / 256;
		req[Current++] = File->Used % 256;
	}

	if (s->Phone.Data.Priv.OBEXGEN.Service == OBEX_BrowsingFolders) {
		/* connection ID block */
		req[Current++] = 0xCB; // ID
		req[Current++] = 0x00; req[Current++] = 0x00;
		req[Current++] = 0x00; req[Current++] = 0x01;
	}

	j = s->Phone.Data.Priv.OBEXGEN.FrameSize - Current - 20;
	if (j > 1000) j = 1000;

	if (File->Used - *Pos < j) {
		j = File->Used - *Pos;
		/* End of file body block */
		OBEXAddBlock(req, &Current, 0x49, File->Buffer+(*Pos), j);
		smprintf(s, "Adding file part %i %i\n",*Pos,j);
		*Pos = *Pos + j;
		error = GSM_WaitFor (s, req, Current, 0x82, 4, ID_AddFile);
		if (error != ERR_NONE) return error;
		return ERR_EMPTY;
	} else {
		/* File body block */
		OBEXAddBlock(req, &Current, 0x48, File->Buffer+(*Pos), j);
		smprintf(s, "Adding file part %i %i\n",*Pos,j);
		*Pos = *Pos + j;
		error=GSM_WaitFor (s, req, Current, 0x02, 4, ID_AddFile);
	}
	return error;
}

GSM_Error OBEXGEN_AddFilePart(GSM_StateMachine *s, GSM_File *File, int *Pos, int *Handle)
{
	GSM_Error		error;

	/* Go to default service */
	error = OBEXGEN_Connect(s, 0);
	if (error != ERR_NONE) return error;

	/* Add file */
	smprintf(s,"Adding file\n");
	error = OBEXGEN_PrivAddFilePart(s, File, Pos, Handle);
	if (error != ERR_NONE) return error;

	/* Calculate path of added file */
	OBEXGEN_CreateFileName(File->ID_FullName, File->ID_FullName, File->Name);
	return ERR_NONE;
}

/**
 * Reply handler for file reading operations.
 */
static GSM_Error OBEXGEN_ReplyGetFilePart(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	int old,Pos=0;

	switch (msg.Type) {
	case 0xA0:
		smprintf(s,"Last file part received\n");
		s->Phone.Data.Priv.OBEXGEN.FileLastPart = true;
	case 0x90:
		while(1) {
			if (Pos >= msg.Length) break;
			switch (msg.Buffer[Pos]) {
			case 0x48:
			case 0x49:
				smprintf(s,"File part received\n");
				old = s->Phone.Data.File->Used;
				s->Phone.Data.File->Used += msg.Buffer[Pos+1]*256+msg.Buffer[Pos+2]-3;
				smprintf(s,"Length of file part: %i\n",
						msg.Buffer[Pos+1]*256+msg.Buffer[Pos+2]-3);
				s->Phone.Data.File->Buffer = (unsigned char *)realloc(s->Phone.Data.File->Buffer,s->Phone.Data.File->Used);
				memcpy(s->Phone.Data.File->Buffer+old,msg.Buffer+Pos+3,s->Phone.Data.File->Used-old);
				return ERR_NONE;
			case 0xc3:
				/* Length */
				/**
				 * @todo ignored now
				 */
				Pos += 5;
				break;
			case 0xcb:
				/* Skip Connection ID (we ignore this for now) */
				Pos += 5;
				break;
			default:
				Pos+=msg.Buffer[Pos+1]*256+msg.Buffer[Pos+2];
				break;
			}
		}
		return ERR_UNKNOWNRESPONSE;
	case 0xC1:
	case 0xC3:
		smprintf(s,"Security error\n");
		return ERR_PERMISSION;
	case 0xC0:
		smprintf(s,"Invalid request\n");
		return ERR_BUG;
	case 0xC4:
		smprintf(s,"File not found\n");
		return ERR_FILENOTEXIST;
	}
	return ERR_UNKNOWNRESPONSE;
}

static GSM_Error OBEXGEN_PrivGetFilePart(GSM_StateMachine *s, GSM_File *File, bool FolderList)
{
	unsigned int 		Current = 0;
	GSM_Error		error;
	unsigned char 		req[2000], req2[200];

	s->Phone.Data.File 	= File;
	File->ReadOnly 		= false;
	File->Protected 	= false;
	File->Hidden		= false;
	File->System		= false;
	File->ModifiedEmpty	= true;

	if (File->Used == 0x00) {
		if (FolderList) {
			/* Type block */
			strcpy(req2,"x-obex/folder-listing");
			OBEXAddBlock(req, &Current, 0x42, req2, strlen(req2)+1);

			/* Name block */
			if (UnicodeLength(File->Name) == 0x00) {
				OBEXAddBlock(req, &Current, 0x01, NULL, 0);
			} else {
				CopyUnicodeString(req2,File->Name);
				OBEXAddBlock(req, &Current, 0x01, req2, UnicodeLength(req2)*2+2);
			}
		} else {
			File->Folder = false;

			if (File->ID_FullName[0] == 0x00 && File->ID_FullName[1] == 0x00) {
				if (s->Phone.Data.Priv.OBEXGEN.Service == OBEX_BrowsingFolders) {
					/* No file name? Grab OBEX capabilities in browse mode */
					smprintf(s, "No filename requested, grabbing OBEX capabilities as obex-capability.xml\n");
					EncodeUnicode(File->Name, "obex-capability.xml", 19);
					strcpy(req2,"x-obex/capability");

					/* Type block */
					OBEXAddBlock(req, &Current, 0x42, req2, strlen(req2));
				} else if (s->Phone.Data.Priv.OBEXGEN.Service == OBEX_IRMC) {
					/* No file name? Grab devinfo in IrMC mode */
					smprintf(s, "No filename requested, grabbing device information as devinfo.txt\n");
					EncodeUnicode(File->Name, "devinfo.txt", 19);
					EncodeUnicode(req2,"telecom/devinfo.txt",19);

					/* Name block */
					OBEXAddBlock(req, &Current, 0x01, req2, UnicodeLength(req2)*2+2);
				} else {
					return ERR_NOTSUPPORTED;
				}
			} else {
				if (s->Phone.Data.Priv.OBEXGEN.Service == OBEX_BrowsingFolders) {
					error = OBEXGEN_ChangeToFilePath(s, File->ID_FullName, true, req2);
					if (error != ERR_NONE) return error;
				} else {
					CopyUnicodeString(req2,File->ID_FullName);
				}
				CopyUnicodeString(File->Name,req2);

				s->Phone.Data.File = File;

				Current = 0;
				/* Name block */
				OBEXAddBlock(req, &Current, 0x01, req2, UnicodeLength(req2)*2+2);
			}
		}
	}

	s->Phone.Data.Priv.OBEXGEN.FileLastPart = false;

	if (s->Phone.Data.Priv.OBEXGEN.Service == OBEX_BrowsingFolders) {
		/* connection ID block */
		req[Current++] = 0xCB; // ID
		req[Current++] = 0x00; req[Current++] = 0x00;
		req[Current++] = 0x00; req[Current++] = 0x01;
	}

	smprintf(s, "Getting first file part from filesystem\n");
	error=GSM_WaitFor (s, req, Current, 0x83, 4, ID_GetFile);
	if (error != ERR_NONE) return error;

	while (!s->Phone.Data.Priv.OBEXGEN.FileLastPart) {
		Current = 0;
 	    	if (s->Phone.Data.Priv.OBEXGEN.Service == OBEX_BrowsingFolders) {
			/* connection ID block */
			req[Current++] = 0xCB; // ID
			req[Current++] = 0x00; req[Current++] = 0x00;
			req[Current++] = 0x00; req[Current++] = 0x01;
		}
		smprintf(s, "Getting file part from filesystem\n");
		error=GSM_WaitFor (s, req, Current, 0x83, 4, ID_GetFile);
		if (error != ERR_NONE) return error;
	}
	return ERR_EMPTY;
}

GSM_Error OBEXGEN_GetFilePart(GSM_StateMachine *s, GSM_File *File, int *Handle, int *Size)
{
	GSM_Error		error;

	/* Go to default service */
	error = OBEXGEN_Connect(s, 0);
	if (error != ERR_NONE) return error;

	(*Handle) = 0;
	(*Size) = 0;
	return OBEXGEN_PrivGetFilePart(s, File, false);
}


/**
 * List OBEX folder.
 *
 * @todo We assume XML reply is in UTF-8, but this doesn't have to be true.
 */
GSM_Error OBEXGEN_GetNextFileFolder(GSM_StateMachine *s, GSM_File *File, bool start)
{
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;
	GSM_Error		error;
	unsigned char		Line[500],Line2[500],*name,*size;
	int			Pos,i,j,num,pos2,Current,z;

	/* Go to default service */
	error = OBEXGEN_Connect(s, 0);
	if (error != ERR_NONE) return error;

	/* We can browse files only when using browse service */
	if (s->Phone.Data.Priv.OBEXGEN.Service != OBEX_BrowsingFolders) {
		return ERR_NOTSUPPORTED;
	}

	if (start) {
		Priv->Files[0].Folder		= true;
		Priv->Files[0].Level		= 1;
		Priv->Files[0].Name[0]		= 0;
		Priv->Files[0].Name[1]		= 0;
		Priv->Files[0].ID_FullName[0]	= 0;
		Priv->Files[0].ID_FullName[1]	= 0;

		Priv->FilesLocationsUsed 	= 1;
		Priv->FilesLocationsCurrent 	= 0;

		Current = 0;
	}

	while (1) {
		if (Priv->FilesLocationsCurrent == Priv->FilesLocationsUsed) {
			dbgprintf("Last file\n");
			return ERR_EMPTY;
		}

		CopyUnicodeString(File->ID_FullName,Priv->Files[Priv->FilesLocationsCurrent].ID_FullName);
		File->Level	= Priv->Files[Priv->FilesLocationsCurrent].Level;
		File->Folder	= Priv->Files[Priv->FilesLocationsCurrent].Folder;
		CopyUnicodeString(File->Name,Priv->Files[Priv->FilesLocationsCurrent].Name);
		Priv->FilesLocationsCurrent++;

		if (File->Folder) {
			error = OBEXGEN_ChangeToFilePath(s, File->ID_FullName, false, NULL);
			if (error != ERR_NONE) return error;

			File->Buffer		= NULL;
			File->Used		= 0;
			File->ModifiedEmpty	= true;

			error = OBEXGEN_PrivGetFilePart(s, File, true);
			if (error != ERR_NONE && error != ERR_EMPTY) return error;

			num = 0;
			Pos = 0;
			/* Calculate number of files */
			while (1) {
				MyGetLine(File->Buffer, &Pos, Line, File->Used, false);
				if (strlen(Line) == 0) break;
				name = strstr(Line,"folder name=\"");
				if (name != NULL) {
					name += 13;
					j = 0;
					while(1) {
						if (name[j] == '"') break;
						j++;
					}
					name[j] = 0;

					if (strcmp(name,".")) num++;
				}
				name = strstr(Line,"file name=\"");
				if (name != NULL) num++;
			}
			/* Shift current files to the end of list */
			if (num != 0) {
				i = Priv->FilesLocationsUsed-1;
				while (1) {
					if (i==Priv->FilesLocationsCurrent-1) break;
					memcpy(&Priv->Files[i+num],&Priv->Files[i],sizeof(GSM_File));
					i--;
				}
			}

			/* Actually parse file listing */
			Pos 	= 0;
			pos2 	= 0;
			while (1) {
				MyGetLine(File->Buffer, &Pos, Line, File->Used, false);
				if (strlen(Line) == 0) break;
				strcpy(Line2,Line);
				name = strstr(Line2,"folder name=\"");
				if (name != NULL) {
					name += 13;
					j = 0;
					while(1) {
						if (name[j] == '"') break;
						j++;
					}
					name[j] = 0;
					if (strcmp(name,".")) {
						dbgprintf("copying folder %s to %i parent %i\n",name,Priv->FilesLocationsCurrent+pos2,Priv->FilesLocationsCurrent);
						/* Convert filename from UTF-8 */
						DecodeUTF8(Priv->Files[Priv->FilesLocationsCurrent+pos2].Name, name, strlen(name));
						/* Create file name from parts */
						OBEXGEN_CreateFileName(
							Priv->Files[Priv->FilesLocationsCurrent+pos2].ID_FullName,
							File->ID_FullName,
							Priv->Files[Priv->FilesLocationsCurrent+pos2].Name
							);
						Priv->Files[Priv->FilesLocationsCurrent+pos2].Level  = File->Level+1;
						Priv->Files[Priv->FilesLocationsCurrent+pos2].Folder = true;
						Priv->FilesLocationsUsed++;
						pos2++;
					}
				}
				strcpy(Line2,Line);
				name = strstr(Line2,"file name=\"");
				if (name != NULL) {
					name += 11;
					j = 0;
					while(1) {
						if (name[j] == '"') break;
						j++;
					}
					name[j] = 0;
					dbgprintf("copying file %s to %i\n",name,Priv->FilesLocationsCurrent+pos2);
					/* Convert filename from UTF-8 */
					DecodeUTF8(Priv->Files[Priv->FilesLocationsCurrent+pos2].Name, name, strlen(name));
					/* Create file name from parts */
					OBEXGEN_CreateFileName(
						Priv->Files[Priv->FilesLocationsCurrent+pos2].ID_FullName,
						File->ID_FullName,
						Priv->Files[Priv->FilesLocationsCurrent+pos2].Name
						);

					Priv->Files[Priv->FilesLocationsCurrent+pos2].Level	= File->Level+1;
					Priv->Files[Priv->FilesLocationsCurrent+pos2].Folder 	= false;
					Priv->Files[Priv->FilesLocationsCurrent+pos2].Used 	= 0;
					strcpy(Line2,Line);
					size = strstr(Line2,"size=\"");
					if (size != NULL) Priv->Files[Priv->FilesLocationsCurrent+pos2].Used = atoi(size+6);

					Priv->Files[Priv->FilesLocationsCurrent+pos2].ModifiedEmpty = true;
					strcpy(Line2,Line);
					size = strstr(Line2,"modified=\"");
					if (size != NULL) {
						Priv->Files[Priv->FilesLocationsCurrent+pos2].ModifiedEmpty = false;
						ReadVCALDateTime(size+10, &Priv->Files[Priv->FilesLocationsCurrent+pos2].Modified);
					}
					Priv->FilesLocationsUsed++;
					pos2++;
				}
			}

			z = Priv->FilesLocationsCurrent;
			free(File->Buffer);
			File->Buffer = NULL;
		} else {
			File->Used 	    	= Priv->Files[Priv->FilesLocationsCurrent-1].Used;
			File->ModifiedEmpty 	= Priv->Files[Priv->FilesLocationsCurrent-1].ModifiedEmpty;
			if (!File->ModifiedEmpty) {
				memcpy(&File->Modified,&Priv->Files[Priv->FilesLocationsCurrent-1].Modified,sizeof(GSM_DateTime));
			}
			File->ReadOnly 		= false;
			File->Protected 	= false;
			File->Hidden		= false;
			File->System		= false;

		}
		return ERR_NONE;
	}
}

GSM_Error OBEXGEN_DeleteFile(GSM_StateMachine *s, unsigned char *ID)
{
	GSM_Error		error;
	unsigned int		Current = 0;
	unsigned char		req[200],req2[200];

	/* Go to default service */
	error = OBEXGEN_Connect(s, 0);
	if (error != ERR_NONE) return error;

	if (s->Phone.Data.Priv.OBEXGEN.Service != OBEX_BrowsingFolders) {
		return ERR_NOTSUPPORTED;
	}

	/* Go to file directory */
	error = OBEXGEN_ChangeToFilePath(s, ID, true, req2);
	if (error != ERR_NONE) return error;

	/* Name block */
	OBEXAddBlock(req, &Current, 0x01, req2, UnicodeLength(req2)*2+2);

	/* connection ID block */
	req[Current++] = 0xCB; // ID
	req[Current++] = 0x00; req[Current++] = 0x00;
	req[Current++] = 0x00; req[Current++] = 0x01;

	return GSM_WaitFor (s, req, Current, 0x82, 4, ID_AddFile);
}

GSM_Error OBEXGEN_AddFolder(GSM_StateMachine *s, GSM_File *File)
{
	GSM_Error		error;

	/* Go to default service */
	error = OBEXGEN_Connect(s, 0);
	if (error != ERR_NONE) return error;

	if (s->Phone.Data.Priv.OBEXGEN.Service != OBEX_BrowsingFolders) {
		return ERR_NOTSUPPORTED;
	}

	/* Go to file directory */
	error = OBEXGEN_ChangeToFilePath(s, File->ID_FullName, false, NULL);
	if (error != ERR_NONE) return error;

	/* Add folder */
	smprintf(s,"Adding directory\n");
	error = OBEXGEN_ChangePath(s, File->Name, 0);
	if (error != ERR_NONE) return error;

	/* Calculate path of added folder */
	OBEXGEN_CreateFileName(File->ID_FullName, File->ID_FullName, File->Name);
	return ERR_NONE;
}

/*@}*/

/**
 * \defgroup OBEXhelper OBEX helper functions which are used in IrMC code
 * \ingroup OBEXPhone
 * @{
 */

/**
 * Grabs complete single file
 */
GSM_Error OBEXGEN_GetFile(GSM_StateMachine *s, const char *FileName, unsigned char ** Buffer, int *len)
{
	GSM_Error error = ERR_NONE;
	GSM_File File;

	/* Clear structure */
	memset(&File, 0, sizeof(GSM_File));

	/* Encode file name to unicode */
	EncodeUnicode(File.ID_FullName, FileName, strlen(FileName));

	/* Grab complete file */
	while (error == ERR_NONE) {
		error = OBEXGEN_PrivGetFilePart(s, &File, false);
	}

	/* We should get ERR_EMPTY at the end of file */
	if (error != ERR_EMPTY) {
		if (File.Buffer != NULL) {
			free(File.Buffer);
		}
		return error;
	}

	/* Return data we got */
	*Buffer = File.Buffer;
	*len = File.Used;
	return ERR_NONE;
}

/**
 * Grabs complete single text file
 */
GSM_Error OBEXGEN_GetTextFile(GSM_StateMachine *s, const char *FileName, char ** Buffer)
{
	GSM_Error error = ERR_NONE;
	int len;

	/* Grab complete file */
	error = OBEXGEN_GetFile(s, FileName, (unsigned char **)Buffer, &len);
	if (error != ERR_NONE) return error;

	/* Return data we got */
	smprintf(s, "Got %d data\n", len);
	*Buffer = realloc(*Buffer, len + 1);
	if (*Buffer == NULL) {
		return ERR_MOREMEMORY;
	}
	(*Buffer)[len] = 0;
	return ERR_NONE;
}

/**
 * Sets single file on filesystem, file can be created or updated.
 */
GSM_Error OBEXGEN_SetFile(GSM_StateMachine *s, const char *FileName, unsigned char *Buffer, int Length)
{
	GSM_Error	error = ERR_NONE;
	GSM_File 	File;
	int		Pos = 0, Handle;

	/* Fill file structure */
	EncodeUnicode(File.ID_FullName, FileName, strlen(FileName));
	EncodeUnicode(File.Name, FileName, strlen(FileName));
	File.Used 	= Length;
	File.Buffer 	= Buffer;

	/* Send file */
	while (error == ERR_NONE) {
		error = OBEXGEN_PrivAddFilePart(s, &File, &Pos, &Handle);
	}
	if (error != ERR_EMPTY) return error;

	return ERR_NONE;
}

/*@}*/

/**
 * \defgroup IrMChelper Generic IrMC helper functions
 * \ingroup OBEXPhone
 * @{
 */

/**
 * Parses selected information from IrMC info.log. Information parsed:
 *  * IEL (Information Exchange Level)
 *  * Number of free records
 *  * Number of used records
 */
GSM_Error OBEXGEN_ParseInfoLog(GSM_StateMachine *s, const char *data, int *free_records, int *used, int *IEL_save)
{
	char *pos;
	int IEL;
	char free_text[] = "Free-Records:";
	char used_text[] = "Total-Records:";
	char IEL_text[] = "IEL:";

	smprintf(s, "OBEX info data:\n---\n%s\n---\n", data);

	pos = strstr(data, IEL_text);
	if (pos == NULL) {
		smprintf(s, "Could not grab Information Exchange Level, phone does not support it\n");
		return ERR_NOTSUPPORTED;
	}
	pos += strlen(IEL_text);
	/* This might be hex */
	if (pos[0] != 0 && pos[0] == '0' && pos[1] != 0 && pos[1] == 'x') {
		IEL = strtol(pos + 2, (char **)NULL, 16);
	} else {
		IEL = atoi(pos);
	}
	switch (IEL) {
		case 0x1:
			smprintf(s, "Information Exchange Level 1 supported\n");
			break;
		case 0x2:
			smprintf(s, "Information Exchange Level 1 and 2 supported\n");
			break;
		case 0x4:
			smprintf(s, "Information Exchange Level 1, 2 and 3 supported\n");
			break;
		case 0x8:
			smprintf(s, "Information Exchange Level 1, 2 and 4 supported\n");
			break;
		case 0x10:
			smprintf(s, "Information Exchange Level 1, 2, 3 and 4 supported\n");
			break;
		default:
			smprintf(s, "Could not parse Information Exchange Level (0x%x)\n", IEL);
			return ERR_INVALIDDATA;
	}

	if (IEL_save != NULL) {
		*IEL_save = IEL;
	}

	if (free_records != NULL) {
		*free_records = 0;
		pos = strstr(data, free_text);
		if (pos == NULL) {
			smprintf(s, "Could not grab number of free records\n");
			return ERR_INVALIDDATA;
		}
		pos += strlen(free_text);
		*free_records = atoi(pos);
	}

	if (used != NULL) {
		*used = 0;
		pos = strstr(data, used_text);
		if (pos == NULL) {
			smprintf(s, "Could not grab used\n");
			return ERR_INVALIDDATA;
		}
		pos += strlen(used_text);
		*used = atoi(pos);
	}

	return ERR_NONE;
}

/**
 * Grabs information from defined OBEX IrMC information log into variables.
 */
GSM_Error OBEXGEN_GetInformation(GSM_StateMachine *s, const char *path, int *free_records, int *used_records, int *IEL)
{
	GSM_Error 	error;
	char		*data;

	/* IEL by default - support adding */
	*IEL = 1;

	/* We need IrMC service for this */
	error = OBEXGEN_Connect(s, OBEX_IRMC);
	if (error != ERR_NONE) return error;

	/* Grab log info file */
	error = OBEXGEN_GetTextFile(s, path, &data);

	/* Level 0 or 1 phones do not have to expose information */
	if (error == ERR_BUG || error == ERR_FILENOTEXIST || error == ERR_PERMISSION) {
		/* Some phones do not follow IrMC specs and do not provide info.log for level 2 */
		if (IsPhoneFeatureAvailable(s->Phone.Data.ModelInfo, F_IRMC_LEVEL_2)) {
			*IEL = 2;
		}
		if (free_records == NULL) {
			/* We were asked only for IEL, so don't bail out */
			return ERR_NONE;
		} else {
			/* No support for status if no info.log */
			return ERR_NOTSUPPORTED;
		}
	} else if (error != ERR_NONE) {
		/* Something wrong has happened */
		return error;
	}

	/* Parse it */
	error = OBEXGEN_ParseInfoLog(s, data, free_records, used_records, IEL);

	free(data);

	return error;
}

/**
 * Initialises LUID database, which is used for LUID - Location mapping.
 */
GSM_Error OBEXGEN_InitLUID(GSM_StateMachine *s, const char *Name, const bool Recalculate, char *Header, char **Data, int **Offsets, int *Count, char ***LUID, int *LUIDCount)
{
	GSM_Error 	error;
	char		*pos;
	int		LUIDSize = 0;
	int		Size = 0;
	int		linepos = 0;
	int		prevpos;
	char		line[2000];
	size_t		len;
	size_t		hlen;
	int		level = 0;

	/* Free data if previously allocated */
	if (!Recalculate) {
		if (*Data != NULL) free(*Data);
	}
	/**
	 * @todo should free all data here, but this execution path is not supported now
	 */

	/* Grab file with listing */
	if (!Recalculate || *Data == NULL) {
		error = OBEXGEN_GetTextFile(s, Name, Data);
		if (error != ERR_NONE) return error;
	}

	*Count = 0;
	*Offsets = NULL;
	*LUIDCount = 0;
	*LUID = NULL;
	len = strlen(*Data);
	hlen = strlen(Header);

        while (1) {
		/* Remember line start position */
		prevpos = linepos;
                MyGetLine(*Data, &linepos, line, len, false);
                if (strlen(line) == 0) break;
                switch (level) {
			case 0:
				if (strncmp(line, Header, strlen(Header)) == 0) {
					level = 1;
					(*Count)++;
					/* Do we need to reallocate? */
					if (*Count >= Size) {
						Size += 20;
						*Offsets = realloc(*Offsets, Size * sizeof(int));
						if (*Offsets == NULL) {
							return ERR_MOREMEMORY;
						}
					}
					/* Store start of item */
					(*Offsets)[*Count] = prevpos;
				} else if (strncmp(line, "BEGIN:VCALENDAR", 15) == 0) {
					/* We need to skip vCalendar header */
				} else if (strncmp(line, "BEGIN:", 6) == 0) {
					/* Skip other event types */
					level = 2;
				}
				break;
			case 1:
				if (strncmp(line, "END:", 4) == 0) {
					level = 0;
				} else if (strncmp(line, "X-IRMC-LUID:", 12) == 0) {
					pos = line + 12; /* Length of X-IRMC-LUID: */
					(*LUIDCount)++;
					/* Do we need to reallocate? */
					if (*LUIDCount >= LUIDSize) {
						LUIDSize += 20;
						*LUID = realloc(*LUID, LUIDSize * sizeof(char *));
						if (*LUID == NULL) {
							return ERR_MOREMEMORY;
						}
					}
					/* Copy LUID text */
					(*LUID)[*LUIDCount] = strdup(pos);
#if 0
					smprintf(s, "Added LUID %s at position %d\n", (*LUID)[*LUIDCount], *LUIDCount);
#endif
				}
				break;
			case 2:
				if (strncmp(line, "END:", 4) == 0) level = 0;
				break;
		}
	}

	smprintf(s, "Data parsed, found %d entries and %d LUIDs\n", *Count, *LUIDCount);

	return ERR_NONE;
}

/*@}*/

/**
 * \defgroup IrMCphonebook IrMC phonebook support
 * \ingroup OBEXPhone
 * @{
 */

/**
 * Parses pb/info.log (phonebook IrMC information log).
 */
GSM_Error OBEXGEN_GetPbInformation(GSM_StateMachine *s, int *free_records, int *used)
{
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;

	return OBEXGEN_GetInformation(s, "telecom/pb/info.log", free_records, used, &(Priv->PbIEL));

}

/**
 * Grabs phonebook memory status
 */
GSM_Error OBEXGEN_GetMemoryStatus(GSM_StateMachine *s, GSM_MemoryStatus *Status)
{
	if (Status->MemoryType != MEM_ME) return ERR_NOTSUPPORTED;

	return OBEXGEN_GetPbInformation(s, &(Status->MemoryFree), &(Status->MemoryUsed));

}

/**
 * Initializes phonebook LUID database.
 */
GSM_Error OBEXGEN_InitPbLUID(GSM_StateMachine *s)
{
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;

	/* We might do validation here using telecom/pb/luid/cc.log fir IEL 4, but not on each request */
	if (Priv->PbData != NULL) return ERR_NONE;

	return OBEXGEN_InitLUID(s, "telecom/pb.vcf", false, "BEGIN:VCARD", &(Priv->PbData), &(Priv->PbOffsets), &(Priv->PbCount), &(Priv->PbLUID), &(Priv->PbLUIDCount));
}

/**
 * Read memory by reading static index.
 */
GSM_Error OBEXGEN_GetMemoryIndex(GSM_StateMachine *s, GSM_MemoryEntry *Entry)
{
	GSM_Error 	error;
	char		*data;
	char		*path;
	int		pos = 0;

	/* Calculate path */
	path = malloc(20 + 22); /* Length of string bellow + length of number */
	if (path == NULL) {
		return ERR_MOREMEMORY;
	}
	sprintf(path, "telecom/pb/%d.vcf", Entry->Location);
	smprintf(s, "Getting vCard %s\n", path);

	/* Grab vCard */
	error = OBEXGEN_GetTextFile(s, path, &data);
	free(path);
	if (error != ERR_NONE) return error;

	/* Decode it */
	error = GSM_DecodeVCARD(data, &pos, Entry, SonyEricsson_VCard21);
	free(data);
	if (error != ERR_NONE) return error;

	return ERR_NONE;
}

/**
 * Reads memory by reading from LUID location.
 */
GSM_Error OBEXGEN_GetMemoryLUID(GSM_StateMachine *s, GSM_MemoryEntry *Entry)
{
	GSM_Error 	error;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;
	char		*data;
	char		*path;
	int		pos = 0;

	error = OBEXGEN_InitPbLUID(s);
	if (error != ERR_NONE) return error;

	/* Check bounds */
	if (Entry->Location > Priv->PbLUIDCount) return ERR_EMPTY; /* Maybe invalid location? */
	if (Priv->PbLUID[Entry->Location] == NULL) return ERR_EMPTY;

	/* Calculate path */
	path = malloc(strlen(Priv->PbLUID[Entry->Location]) + 22); /* Length of string bellow */
	if (path == NULL) {
		return ERR_MOREMEMORY;
	}
	sprintf(path, "telecom/pb/luid/%s.vcf", Priv->PbLUID[Entry->Location]);
	smprintf(s, "Getting vCard %s\n", path);

	/* Grab vCard */
	error = OBEXGEN_GetTextFile(s, path, &data);
	free(path);
	if (error != ERR_NONE) return error;

	/* Decode it */
	error = GSM_DecodeVCARD(data, &pos, Entry, SonyEricsson_VCard21);
	free(data);
	if (error != ERR_NONE) return error;

	return ERR_NONE;
}

/**
 * Reads memory by reading from full data.
 */
GSM_Error OBEXGEN_GetMemoryFull(GSM_StateMachine *s, GSM_MemoryEntry *Entry)
{
	GSM_Error 	error;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;
	int		pos = 0;

	/* Read phonebook data */
	error = OBEXGEN_InitPbLUID(s);
	if (error != ERR_NONE) return error;

	/* Check bounds */
	if (Entry->Location > Priv->PbCount) return ERR_EMPTY; /* Maybe invalid location? */

	/* Decode vCard */
	error = GSM_DecodeVCARD(Priv->PbData + Priv->PbOffsets[Entry->Location], &pos, Entry, SonyEricsson_VCard21);
	if (error != ERR_NONE) return error;

	return ERR_NONE;
}

GSM_Error OBEXGEN_GetMemory(GSM_StateMachine *s, GSM_MemoryEntry *Entry)
{
	GSM_Error 	error;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;

	if (Entry->MemoryType != MEM_ME) return ERR_NOTSUPPORTED;

	/* We need IrMC service for this */
	error = OBEXGEN_Connect(s, OBEX_IRMC);
	if (error != ERR_NONE) return error;

	/* We need IEL to correctly talk to phone */
	if (Priv->PbIEL == -1) {
		error = OBEXGEN_GetPbInformation(s, NULL, NULL);
		if (error != ERR_NONE) return error;
	}

	/* Use correct function according to supported IEL */
	if (Priv->PbIEL == 0x8 || Priv->PbIEL == 0x10) {
		return OBEXGEN_GetMemoryLUID(s, Entry);
	} else if (Priv->PbIEL == 0x4) {
		return OBEXGEN_GetMemoryIndex(s, Entry);
	} else if (Priv->PbIEL == 0x2) {
		return OBEXGEN_GetMemoryFull(s, Entry);
	} else {
		smprintf(s, "Can not read phonebook from IEL 1 phone\n");
		return ERR_NOTSUPPORTED;
	}
}

GSM_Error OBEXGEN_GetNextMemory(GSM_StateMachine *s, GSM_MemoryEntry *Entry, bool start)
{
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;
	GSM_Error 	error = ERR_EMPTY;;

	/* Get  location */
	if (start) {
		Entry->Location = 1;
		Priv->ReadPhonebook = 0;
	} else {
		Entry->Location++;
	}

	/* Have we read them all? */
	if (Priv->ReadPhonebook == Priv->PbCount) {
		return ERR_EMPTY;
	}

	/* Do real getting */
	while (error == ERR_EMPTY) {
		error = OBEXGEN_GetMemory(s, Entry);
		if (error == ERR_NONE) {
			Priv->ReadPhonebook++;
		} else if (error == ERR_EMPTY) {
			Entry->Location++;
		}
	}
	return error;
}

GSM_Error OBEXGEN_AddMemory(GSM_StateMachine *s, GSM_MemoryEntry *Entry)
{
	unsigned char 		req[5000];
	int			size=0;
	GSM_Error		error;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;

	if (Entry->MemoryType != MEM_ME) return ERR_NOTSUPPORTED;

	/* We need IrMC service for this */
	error = OBEXGEN_Connect(s, OBEX_IRMC);
	if (error != ERR_NONE) return error;

	/* We need IEL to correctly talk to phone */
	if (Priv->PbIEL == -1) {
		error = OBEXGEN_GetPbInformation(s, NULL, NULL);
		if (error != ERR_NONE) return error;
	}

	/* Encode vCard */
	GSM_EncodeVCARD(req, &size, Entry, true, SonyEricsson_VCard21);

	/* Use correct function according to supported IEL */
	if (Priv->PbIEL == 0x8 || Priv->PbIEL == 0x10) {
		/* We need to grab LUID list now in order to keep position later */
		error = OBEXGEN_InitPbLUID(s);
		if (error != ERR_NONE) return error;

		smprintf(s,"Adding phonebook entry %d:\n%s\n", size, req);
		Priv->UpdatePbLUID = true;
		error = OBEXGEN_SetFile(s, "telecom/pb/luid/.vcf", req, size);
		Entry->Location = Priv->PbLUIDCount;
		return error;
	} else {
		/* I don't know add command for other levels, just plain send vCard */
		Entry->Location = 0;
		smprintf(s,"Sending phonebook entry\n");
		return OBEXGEN_SetFile(s, "gammu.vcf", req, size);
	}
}

GSM_Error OBEXGEN_SetMemoryLUID(GSM_StateMachine *s, GSM_MemoryEntry *Entry, char *Data, int Size)
{
	GSM_Error 	error;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;
	char		*path;

	error = OBEXGEN_InitPbLUID(s);
	if (error != ERR_NONE) return error;

	/* Check bounds */
	if (Entry->Location > Priv->PbLUIDCount) return ERR_INVALIDLOCATION;
	if (Priv->PbLUID[Entry->Location] == NULL) return ERR_INVALIDLOCATION;

	/* Forget entry if we're deleting */
	if (Size == 0) {
		free(Priv->PbLUID[Entry->Location]);
		Priv->PbLUID[Entry->Location] = NULL;
		Priv->PbCount--;
	}

	/* Calculate path */
	path = malloc(strlen(Priv->PbLUID[Entry->Location]) + 22); /* Length of string bellow */
	if (path == NULL) {
		return ERR_MOREMEMORY;
	}
	sprintf(path, "telecom/pb/luid/%s.vcf", Priv->PbLUID[Entry->Location]);
	smprintf(s, "Seting vCard %s\n", path);

	/* Grab vCard */
	return OBEXGEN_SetFile(s, path, Data, Size);
}

GSM_Error OBEXGEN_SetMemoryIndex(GSM_StateMachine *s, GSM_MemoryEntry *Entry, char *Data, int Size)
{
	char		*path;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;

	/* Forget entry if we're deleting */
	if (Size == 0) {
		Priv->PbCount--;
	}

	/* Calculate path */
	path = malloc(20 + 22); /* Length of string bellow + length of number */
	if (path == NULL) {
		return ERR_MOREMEMORY;
	}
	sprintf(path, "telecom/pb/%d.vcf", Entry->Location);
	smprintf(s, "Seting vCard %s\n", path);

	/* Grab vCard */
	return OBEXGEN_SetFile(s, path, Data, Size);
}

GSM_Error OBEXGEN_SetMemory(GSM_StateMachine *s, GSM_MemoryEntry *Entry)
{
	unsigned char 		req[5000];
	int			size=0;
	GSM_Error		error;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;

	if (Entry->MemoryType != MEM_ME) return ERR_NOTSUPPORTED;

	/* We need IrMC service for this */
	error = OBEXGEN_Connect(s, OBEX_IRMC);
	if (error != ERR_NONE) return error;

	/* We need IEL to correctly talk to phone */
	if (Priv->PbIEL == -1) {
		error = OBEXGEN_GetPbInformation(s, NULL, NULL);
		if (error != ERR_NONE) return error;
	}

	/* Encode vCard */
	GSM_EncodeVCARD(req, &size, Entry, true, SonyEricsson_VCard21);

	/* Use correct function according to supported IEL */
	if (Priv->PbIEL == 0x8 || Priv->PbIEL == 0x10) {
		return OBEXGEN_SetMemoryLUID(s, Entry, req, size);
	} else if (Priv->PbIEL == 0x4) {
		return OBEXGEN_SetMemoryIndex(s, Entry, req, size);
	} else if (Priv->PbIEL == 0x2) {
		/* Work on full phonebook */
		return ERR_NOTIMPLEMENTED;
	} else {
		return ERR_NOTSUPPORTED;
	}
}

GSM_Error OBEXGEN_DeleteMemory(GSM_StateMachine *s, GSM_MemoryEntry *Entry)
{
	GSM_Error		error;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;

	if (Entry->MemoryType != MEM_ME) return ERR_NOTSUPPORTED;

	/* We need IrMC service for this */
	error = OBEXGEN_Connect(s, OBEX_IRMC);
	if (error != ERR_NONE) return error;

	/* We need IEL to correctly talk to phone */
	if (Priv->PbIEL == -1) {
		error = OBEXGEN_GetPbInformation(s, NULL, NULL);
		if (error != ERR_NONE) return error;
	}

	/* Use correct function according to supported IEL */
	if (Priv->PbIEL == 0x8 || Priv->PbIEL == 0x10) {
		return OBEXGEN_SetMemoryLUID(s, Entry, "", 0);
	} else if (Priv->PbIEL == 0x4) {
		return OBEXGEN_SetMemoryIndex(s, Entry, "", 0);
	} else if (Priv->PbIEL == 0x2) {
		/* Work on full phonebook */
		return ERR_NOTIMPLEMENTED;
	} else {
		return ERR_NOTSUPPORTED;
	}
}

GSM_Error OBEXGEN_DeleteAllMemory(GSM_StateMachine *s, GSM_MemoryType MemoryType)
{
	GSM_Error		error;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;
	GSM_MemoryEntry		entry;

	/* We need IrMC service for this */
	error = OBEXGEN_Connect(s, OBEX_IRMC);
	if (error != ERR_NONE) return error;

	/* We need count of entries */
	error = OBEXGEN_InitPbLUID(s);
	if (error != ERR_NONE) return error;

	/* Delete all entries */
	entry.Location = 1;
	entry.MemoryType = MEM_ME;
	while (Priv->PbCount > 0) {
		error = OBEXGEN_DeleteMemory(s, &entry);
		if (error != ERR_NONE && error != ERR_EMPTY) return error;
		entry.Location++;
	}
	return error;
}

/*@}*/

/**
 * \defgroup IrMCcaltodo IrMC common calendar and todo functions
 * \ingroup OBEXPhone
 * @{
 */

/**
 * Parses cal/info.log (calendar IrMC information log).
 */
GSM_Error OBEXGEN_GetCalInformation(GSM_StateMachine *s, int *free_records, int *used)
{
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;

	return OBEXGEN_GetInformation(s, "telecom/cal/info.log", free_records, used, &(Priv->CalIEL));

}

/**
 * Initializes calendar LUID database.
 */
GSM_Error OBEXGEN_InitCalLUID(GSM_StateMachine *s)
{
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;
	GSM_Error 	error;

	/* We might do validation here using telecom/cal/luid/cc.log fir IEL 4, but not on each request */
	if (Priv->CalData != NULL) return ERR_NONE;

	error = OBEXGEN_InitLUID(s, "telecom/cal.vcs", false, "BEGIN:VEVENT", &(Priv->CalData), &(Priv->CalOffsets), &(Priv->CalCount), &(Priv->CalLUID), &(Priv->CalLUIDCount));
	if (error != ERR_NONE) return error;
	return OBEXGEN_InitLUID(s, "telecom/cal.vcs", true, "BEGIN:VTODO", &(Priv->CalData), &(Priv->TodoOffsets), &(Priv->TodoCount), &(Priv->TodoLUID), &(Priv->TodoLUIDCount));
}

/*@}*/

/**
 * \defgroup IrMCcalendar IrMC calendar support
 * \ingroup OBEXPhone
 * @{
 */

/**
 * Grabs calendar memory status
 */
GSM_Error OBEXGEN_GetCalendarStatus(GSM_StateMachine *s, GSM_CalendarStatus *Status)
{
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;
	GSM_Error 	error;

	error = OBEXGEN_InitCalLUID(s);
	if (error != ERR_NONE) return error;

	Status->Used = Priv->CalCount;

	return OBEXGEN_GetCalInformation(s, &(Status->Free), NULL);

}

/**
 * Read memory by reading static index.
 */
GSM_Error OBEXGEN_GetCalendarIndex(GSM_StateMachine *s, GSM_CalendarEntry *Entry)
{
	GSM_Error 	error;
	char		*data;
	char		*path;
	int		pos = 0;
	GSM_ToDoEntry	ToDo;

	/* Calculate path */
	path = malloc(20 + 22); /* Length of string bellow + length of number */
	if (path == NULL) {
		return ERR_MOREMEMORY;
	}
	sprintf(path, "telecom/cal/%d.vcs", Entry->Location);
	smprintf(s, "Getting vCalendar %s\n", path);

	/* Grab vCalendar */
	error = OBEXGEN_GetTextFile(s, path, &data);
	free(path);
	if (error != ERR_NONE) return error;

	/* Decode it */
	error = GSM_DecodeVCALENDAR_VTODO(data, &pos, Entry, &ToDo, SonyEricsson_VCalendar, SonyEricsson_VToDo);
	free(data);
	if (error != ERR_NONE) return error;

	return ERR_NONE;
}

/**
 * Reads memory by reading from LUID location.
 */
GSM_Error OBEXGEN_GetCalendarLUID(GSM_StateMachine *s, GSM_CalendarEntry *Entry)
{
	GSM_Error 	error;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;
	char		*data;
	char		*path;
	int		pos = 0;
	GSM_ToDoEntry	ToDo;

	error = OBEXGEN_InitCalLUID(s);
	if (error != ERR_NONE) return error;

	/* Check bounds */
	if (Entry->Location > Priv->CalLUIDCount) return ERR_EMPTY; /* Maybe invalid location? */
	if (Priv->CalLUID[Entry->Location] == NULL) return ERR_EMPTY;

	/* Calculate path */
	path = malloc(strlen(Priv->CalLUID[Entry->Location]) + 22); /* Length of string bellow */
	if (path == NULL) {
		return ERR_MOREMEMORY;
	}
	sprintf(path, "telecom/cal/luid/%s.vcs", Priv->CalLUID[Entry->Location]);
	smprintf(s, "Getting vCalendar %s\n", path);

	/* Grab vCalendar */
	error = OBEXGEN_GetTextFile(s, path, &data);
	free(path);
	if (error != ERR_NONE) return error;

	/* Decode it */
	error = GSM_DecodeVCALENDAR_VTODO(data, &pos, Entry, &ToDo, SonyEricsson_VCalendar, SonyEricsson_VToDo);
	free(data);
	if (error != ERR_NONE) return error;

	return ERR_NONE;
}

/**
 * Reads memory by reading from full data.
 */
GSM_Error OBEXGEN_GetCalendarFull(GSM_StateMachine *s, GSM_CalendarEntry *Entry)
{
	GSM_Error 	error;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;
	int		pos = 0;
	GSM_ToDoEntry	ToDo;

	/* Read calendar data */
	error = OBEXGEN_InitCalLUID(s);
	if (error != ERR_NONE) return error;

	/* Check bounds */
	if (Entry->Location > Priv->CalCount) return ERR_EMPTY; /* Maybe invalid location? */

	/* Decode vCalendar */
	error = GSM_DecodeVCALENDAR_VTODO(Priv->CalData + Priv->CalOffsets[Entry->Location], &pos, Entry, &ToDo, SonyEricsson_VCalendar, SonyEricsson_VToDo);
	if (error != ERR_NONE) return error;

	return ERR_NONE;
}

GSM_Error OBEXGEN_GetCalendar(GSM_StateMachine *s, GSM_CalendarEntry *Entry)
{
	GSM_Error 	error;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;

	/* We need IrMC service for this */
	error = OBEXGEN_Connect(s, OBEX_IRMC);
	if (error != ERR_NONE) return error;

	/* We need IEL to correctly talk to phone */
	if (Priv->CalIEL == -1) {
		error = OBEXGEN_GetCalInformation(s, NULL, NULL);
		if (error != ERR_NONE) return error;
	}

	/* Use correct function according to supported IEL */
	if (Priv->CalIEL == 0x8 || Priv->CalIEL == 0x10) {
		return OBEXGEN_GetCalendarLUID(s, Entry);
	} else if (Priv->CalIEL == 0x4) {
		return OBEXGEN_GetCalendarIndex(s, Entry);
	} else if (Priv->CalIEL == 0x2) {
		return OBEXGEN_GetCalendarFull(s, Entry);
	} else {
		smprintf(s, "Can not read calendar from IEL 1 phone\n");
		return ERR_NOTSUPPORTED;
	}
}

GSM_Error OBEXGEN_GetNextCalendar(GSM_StateMachine *s, GSM_CalendarEntry *Entry, bool start)
{
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;
	GSM_Error 	error = ERR_EMPTY;;

	/* Get  location */
	if (start) {
		Entry->Location = 1;
		Priv->ReadCalendar = 0;
	} else {
		Entry->Location++;
	}

	/* Have we read them all? */
	if (Priv->ReadCalendar == Priv->CalCount) {
		return ERR_EMPTY;
	}

	/* Do real getting */
	while (error == ERR_EMPTY) {
		error = OBEXGEN_GetCalendar(s, Entry);
		if (error == ERR_NONE) {
			Priv->ReadCalendar++;
		} else if (error == ERR_EMPTY) {
			Entry->Location++;
		}
	}
	return error;
}

GSM_Error OBEXGEN_AddCalendar(GSM_StateMachine *s, GSM_CalendarEntry *Entry)
{
	unsigned char 		req[5000];
	int			size=0;
	GSM_Error		error;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;

	/* We need IrMC service for this */
	error = OBEXGEN_Connect(s, OBEX_IRMC);
	if (error != ERR_NONE) return error;

	/* We need IEL to correctly talk to phone */
	if (Priv->CalIEL == -1) {
		error = OBEXGEN_GetCalInformation(s, NULL, NULL);
		if (error != ERR_NONE) return error;
	}

	/* Encode vCalendar */
	GSM_EncodeVCALENDAR(req, &size, Entry, true, SonyEricsson_VCalendar);

	/* Use correct function according to supported IEL */
	if (Priv->CalIEL == 0x8 || Priv->CalIEL == 0x10) {
		/* We need to grab LUID list now in order to keep position later */
		error = OBEXGEN_InitCalLUID(s);
		if (error != ERR_NONE) return error;

		smprintf(s,"Adding calendar entry %d:\n%s\n", size, req);
		Priv->UpdateCalLUID = true;
		error = OBEXGEN_SetFile(s, "telecom/cal/luid/.vcs", req, size);
		Entry->Location = Priv->CalLUIDCount;
		return error;
	} else {
		/* I don't know add command for other levels, just plain send vCalendar */
		Entry->Location = 0;
		smprintf(s,"Sending calendar entry\n");
		return OBEXGEN_SetFile(s, "gammu.vcs", req, size);
	}
}

GSM_Error OBEXGEN_SetCalendarLUID(GSM_StateMachine *s, GSM_CalendarEntry *Entry, char *Data, int Size)
{
	GSM_Error 	error;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;
	char		*path;

	error = OBEXGEN_InitCalLUID(s);
	if (error != ERR_NONE) return error;

	/* Check bounds */
	if (Entry->Location > Priv->CalLUIDCount) return ERR_INVALIDLOCATION;
	if (Priv->CalLUID[Entry->Location] == NULL) return ERR_INVALIDLOCATION;

	/* Forget entry if we're deleting */
	if (Size == 0) {
		free(Priv->CalLUID[Entry->Location]);
		Priv->CalLUID[Entry->Location] = NULL;
		Priv->CalCount--;
	}

	/* Calculate path */
	path = malloc(strlen(Priv->CalLUID[Entry->Location]) + 22); /* Length of string bellow */
	if (path == NULL) {
		return ERR_MOREMEMORY;
	}
	sprintf(path, "telecom/cal/luid/%s.vcs", Priv->CalLUID[Entry->Location]);
	smprintf(s, "Seting vCalendar %s\n", path);

	/* Grab vCalendar */
	return OBEXGEN_SetFile(s, path, Data, Size);
}

GSM_Error OBEXGEN_SetCalendarIndex(GSM_StateMachine *s, GSM_CalendarEntry *Entry, char *Data, int Size)
{
	char		*path;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;

	/* Forget entry if we're deleting */
	if (Size == 0) {
		Priv->CalCount--;
	}

	/* Calculate path */
	path = malloc(20 + 22); /* Length of string bellow + length of number */
	if (path == NULL) {
		return ERR_MOREMEMORY;
	}
	sprintf(path, "telecom/cal/%d.vcs", Entry->Location);
	smprintf(s, "Seting vCalendar %s\n", path);

	/* Grab vCalendar */
	return OBEXGEN_SetFile(s, path, Data, Size);
}

GSM_Error OBEXGEN_SetCalendar(GSM_StateMachine *s, GSM_CalendarEntry *Entry)
{
	unsigned char 		req[5000];
	int			size=0;
	GSM_Error		error;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;

	/* We need IrMC service for this */
	error = OBEXGEN_Connect(s, OBEX_IRMC);
	if (error != ERR_NONE) return error;

	/* We need IEL to correctly talk to phone */
	if (Priv->CalIEL == -1) {
		error = OBEXGEN_GetCalInformation(s, NULL, NULL);
		if (error != ERR_NONE) return error;
	}

	/* Encode vCalendar */
	GSM_EncodeVCALENDAR(req, &size, Entry, true, SonyEricsson_VCalendar);

	/* Use correct function according to supported IEL */
	if (Priv->CalIEL == 0x8 || Priv->CalIEL == 0x10) {
		return OBEXGEN_SetCalendarLUID(s, Entry, req, size);
	} else if (Priv->CalIEL == 0x4) {
		return OBEXGEN_SetCalendarIndex(s, Entry, req, size);
	} else if (Priv->CalIEL == 0x2) {
		/* Work on full calendar */
		return ERR_NOTIMPLEMENTED;
	} else {
		return ERR_NOTSUPPORTED;
	}
}

GSM_Error OBEXGEN_DeleteCalendar(GSM_StateMachine *s, GSM_CalendarEntry *Entry)
{
	GSM_Error		error;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;

	/* We need IrMC service for this */
	error = OBEXGEN_Connect(s, OBEX_IRMC);
	if (error != ERR_NONE) return error;

	/* We need IEL to correctly talk to phone */
	if (Priv->CalIEL == -1) {
		error = OBEXGEN_GetCalInformation(s, NULL, NULL);
		if (error != ERR_NONE) return error;
	}

	/* Use correct function according to supported IEL */
	if (Priv->CalIEL == 0x8 || Priv->CalIEL == 0x10) {
		return OBEXGEN_SetCalendarLUID(s, Entry, "", 0);
	} else if (Priv->CalIEL == 0x4) {
		return OBEXGEN_SetCalendarIndex(s, Entry, "", 0);
	} else if (Priv->CalIEL == 0x2) {
		/* Work on full calendar */
		return ERR_NOTIMPLEMENTED;
	} else {
		return ERR_NOTSUPPORTED;
	}
}

GSM_Error OBEXGEN_DeleteAllCalendar(GSM_StateMachine *s)
{
	GSM_Error		error;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;
	GSM_CalendarEntry		entry;

	/* We need IrMC service for this */
	error = OBEXGEN_Connect(s, OBEX_IRMC);
	if (error != ERR_NONE) return error;

	/* We need count of entries */
	error = OBEXGEN_InitCalLUID(s);
	if (error != ERR_NONE) return error;

	/* Delete all entries */
	entry.Location = 1;
	while (Priv->CalCount > 0) {
		error = OBEXGEN_DeleteCalendar(s, &entry);
		if (error != ERR_NONE && error != ERR_EMPTY) return error;
		entry.Location++;
	}
	return error;
}

/*@}*/

/**
 * \defgroup IrMCtodo IrMC todo support
 * \ingroup OBEXPhone
 * @{
 */

/**
 * Grabs todo memory status
 */
GSM_Error OBEXGEN_GetTodoStatus(GSM_StateMachine *s, GSM_ToDoStatus *Status)
{
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;
	GSM_Error 	error;

	error = OBEXGEN_InitCalLUID(s);
	if (error != ERR_NONE) return error;

	Status->Used = Priv->TodoCount;

	return OBEXGEN_GetCalInformation(s, &(Status->Free), NULL);

}

/**
 * Read memory by reading static index.
 */
GSM_Error OBEXGEN_GetTodoIndex(GSM_StateMachine *s, GSM_ToDoEntry *Entry)
{
	GSM_Error 	error;
	char		*data;
	char		*path;
	int		pos = 0;
	GSM_CalendarEntry	Cal;

	/* Todoculate path */
	path = malloc(20 + 22); /* Length of string bellow + length of number */
	if (path == NULL) {
		return ERR_MOREMEMORY;
	}
	sprintf(path, "telecom/cal/%d.vcs", Entry->Location);
	smprintf(s, "Getting vTodo %s\n", path);

	/* Grab vTodo */
	error = OBEXGEN_GetTextFile(s, path, &data);
	free(path);
	if (error != ERR_NONE) return error;

	/* Decode it */
	error = GSM_DecodeVCALENDAR_VTODO(data, &pos, &Cal, Entry, SonyEricsson_VCalendar, SonyEricsson_VToDo);
	free(data);
	if (error != ERR_NONE) return error;

	return ERR_NONE;
}

/**
 * Reads memory by reading from LUID location.
 */
GSM_Error OBEXGEN_GetTodoLUID(GSM_StateMachine *s, GSM_ToDoEntry *Entry)
{
	GSM_Error 	error;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;
	char		*data;
	char		*path;
	int		pos = 0;
	GSM_CalendarEntry	Cal;

	error = OBEXGEN_InitCalLUID(s);
	if (error != ERR_NONE) return error;

	/* Check bounds */
	if (Entry->Location > Priv->TodoLUIDCount) return ERR_EMPTY; /* Maybe invalid location? */
	if (Priv->TodoLUID[Entry->Location] == NULL) return ERR_EMPTY;

	/* Todoculate path */
	path = malloc(strlen(Priv->TodoLUID[Entry->Location]) + 22); /* Length of string bellow */
	if (path == NULL) {
		return ERR_MOREMEMORY;
	}
	sprintf(path, "telecom/cal/luid/%s.vcs", Priv->TodoLUID[Entry->Location]);
	smprintf(s, "Getting vTodo %s\n", path);

	/* Grab vTodo */
	error = OBEXGEN_GetTextFile(s, path, &data);
	free(path);
	if (error != ERR_NONE) return error;

	/* Decode it */
	error = GSM_DecodeVCALENDAR_VTODO(data, &pos, &Cal, Entry, SonyEricsson_VCalendar, SonyEricsson_VToDo);
	free(data);
	if (error != ERR_NONE) return error;

	return ERR_NONE;
}

/**
 * Reads memory by reading from full data.
 */
GSM_Error OBEXGEN_GetTodoFull(GSM_StateMachine *s, GSM_ToDoEntry *Entry)
{
	GSM_Error 	error;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;
	int		pos = 0;
	GSM_CalendarEntry	Cal;

	/* Read todo data */
	error = OBEXGEN_InitCalLUID(s);
	if (error != ERR_NONE) return error;

	/* Check bounds */
	if (Entry->Location > Priv->TodoCount) return ERR_EMPTY; /* Maybe invalid location? */

	/* Decode vTodo */
	error = GSM_DecodeVCALENDAR_VTODO(Priv->CalData + Priv->TodoOffsets[Entry->Location], &pos, &Cal, Entry, SonyEricsson_VCalendar, SonyEricsson_VToDo);
	if (error != ERR_NONE) return error;

	return ERR_NONE;
}

GSM_Error OBEXGEN_GetTodo(GSM_StateMachine *s, GSM_ToDoEntry *Entry)
{
	GSM_Error 	error;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;

	/* We need IrMC service for this */
	error = OBEXGEN_Connect(s, OBEX_IRMC);
	if (error != ERR_NONE) return error;

	/* We need IEL to correctly talk to phone */
	if (Priv->CalIEL == -1) {
		error = OBEXGEN_GetCalInformation(s, NULL, NULL);
		if (error != ERR_NONE) return error;
	}

	/* Use correct function according to supported IEL */
	if (Priv->CalIEL == 0x8 || Priv->CalIEL == 0x10) {
		return OBEXGEN_GetTodoLUID(s, Entry);
	} else if (Priv->CalIEL == 0x4) {
		return OBEXGEN_GetTodoIndex(s, Entry);
	} else if (Priv->CalIEL == 0x2) {
		return OBEXGEN_GetTodoFull(s, Entry);
	} else {
		smprintf(s, "Can not read todo from IEL 1 phone\n");
		return ERR_NOTSUPPORTED;
	}
}

GSM_Error OBEXGEN_GetNextTodo(GSM_StateMachine *s, GSM_ToDoEntry *Entry, bool start)
{
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;
	GSM_Error 	error = ERR_EMPTY;;

	/* Get  location */
	if (start) {
		Entry->Location = 1;
		Priv->ReadTodo = 0;
	} else {
		Entry->Location++;
	}

	/* Have we read them all? */
	if (Priv->ReadTodo == Priv->TodoCount) {
		return ERR_EMPTY;
	}

	/* Do real getting */
	while (error == ERR_EMPTY) {
		error = OBEXGEN_GetTodo(s, Entry);
		if (error == ERR_NONE) {
			Priv->ReadTodo++;
		} else if (error == ERR_EMPTY) {
			Entry->Location++;
		}
	}
	return error;
}

GSM_Error OBEXGEN_AddTodo(GSM_StateMachine *s, GSM_ToDoEntry *Entry)
{
	unsigned char 		req[5000];
	int			size=0;
	GSM_Error		error;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;

	/* We need IrMC service for this */
	error = OBEXGEN_Connect(s, OBEX_IRMC);
	if (error != ERR_NONE) return error;

	/* We need IEL to correctly talk to phone */
	if (Priv->CalIEL == -1) {
		error = OBEXGEN_GetCalInformation(s, NULL, NULL);
		if (error != ERR_NONE) return error;
	}

	/* Encode vTodo */
	GSM_EncodeVTODO(req, &size, Entry, true, SonyEricsson_VToDo);

	/* Use correct function according to supported IEL */
	if (Priv->CalIEL == 0x8 || Priv->CalIEL == 0x10) {
		/* We need to grab LUID list now in order to keep position later */
		error = OBEXGEN_InitCalLUID(s);
		if (error != ERR_NONE) return error;

		smprintf(s,"Adding todo entry %d:\n%s\n", size, req);
		Priv->UpdateTodoLUID = true;
		error = OBEXGEN_SetFile(s, "telecom/cal/luid/.vcs", req, size);
		Entry->Location = Priv->TodoLUIDCount;
		return error;
	} else {
		/* I don't know add command for other levels, just plain send vTodo */
		Entry->Location = 0;
		smprintf(s,"Sending todo entry\n");
		return OBEXGEN_SetFile(s, "gammu.vcs", req, size);
	}
}

GSM_Error OBEXGEN_SetTodoLUID(GSM_StateMachine *s, GSM_ToDoEntry *Entry, char *Data, int Size)
{
	GSM_Error 	error;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;
	char		*path;

	error = OBEXGEN_InitCalLUID(s);
	if (error != ERR_NONE) return error;

	/* Check bounds */
	if (Entry->Location > Priv->TodoLUIDCount) return ERR_INVALIDLOCATION;
	if (Priv->TodoLUID[Entry->Location] == NULL) return ERR_INVALIDLOCATION;

	/* Forget entry if we're deleting */
	if (Size == 0) {
		free(Priv->TodoLUID[Entry->Location]);
		Priv->TodoLUID[Entry->Location] = NULL;
		Priv->TodoCount--;
	}

	/* Todoculate path */
	path = malloc(strlen(Priv->TodoLUID[Entry->Location]) + 22); /* Length of string bellow */
	if (path == NULL) {
		return ERR_MOREMEMORY;
	}
	sprintf(path, "telecom/cal/luid/%s.vcs", Priv->TodoLUID[Entry->Location]);
	smprintf(s, "Seting vTodo %s\n", path);

	/* Grab vTodo */
	return OBEXGEN_SetFile(s, path, Data, Size);
}

GSM_Error OBEXGEN_SetTodoIndex(GSM_StateMachine *s, GSM_ToDoEntry *Entry, char *Data, int Size)
{
	char		*path;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;

	/* Forget entry if we're deleting */
	if (Size == 0) {
		Priv->TodoCount--;
	}

	/* Todoculate path */
	path = malloc(20 + 22); /* Length of string bellow + length of number */
	if (path == NULL) {
		return ERR_MOREMEMORY;
	}
	sprintf(path, "telecom/cal/%d.vcs", Entry->Location);
	smprintf(s, "Seting vTodo %s\n", path);

	/* Grab vTodo */
	return OBEXGEN_SetFile(s, path, Data, Size);
}

GSM_Error OBEXGEN_SetTodo(GSM_StateMachine *s, GSM_ToDoEntry *Entry)
{
	unsigned char 		req[5000];
	int			size=0;
	GSM_Error		error;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;

	/* We need IrMC service for this */
	error = OBEXGEN_Connect(s, OBEX_IRMC);
	if (error != ERR_NONE) return error;

	/* We need IEL to correctly talk to phone */
	if (Priv->CalIEL == -1) {
		error = OBEXGEN_GetCalInformation(s, NULL, NULL);
		if (error != ERR_NONE) return error;
	}

	/* Encode vTodo */
	GSM_EncodeVTODO(req, &size, Entry, true, SonyEricsson_VToDo);

	/* Use correct function according to supported IEL */
	if (Priv->CalIEL == 0x8 || Priv->CalIEL == 0x10) {
		return OBEXGEN_SetTodoLUID(s, Entry, req, size);
	} else if (Priv->CalIEL == 0x4) {
		return OBEXGEN_SetTodoIndex(s, Entry, req, size);
	} else if (Priv->CalIEL == 0x2) {
		/* Work on full todo */
		return ERR_NOTIMPLEMENTED;
	} else {
		return ERR_NOTSUPPORTED;
	}
}

GSM_Error OBEXGEN_DeleteTodo(GSM_StateMachine *s, GSM_ToDoEntry *Entry)
{
	GSM_Error		error;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;

	/* We need IrMC service for this */
	error = OBEXGEN_Connect(s, OBEX_IRMC);
	if (error != ERR_NONE) return error;

	/* We need IEL to correctly talk to phone */
	if (Priv->CalIEL == -1) {
		error = OBEXGEN_GetCalInformation(s, NULL, NULL);
		if (error != ERR_NONE) return error;
	}

	/* Use correct function according to supported IEL */
	if (Priv->CalIEL == 0x8 || Priv->CalIEL == 0x10) {
		return OBEXGEN_SetTodoLUID(s, Entry, "", 0);
	} else if (Priv->CalIEL == 0x4) {
		return OBEXGEN_SetTodoIndex(s, Entry, "", 0);
	} else if (Priv->CalIEL == 0x2) {
		/* Work on full todo */
		return ERR_NOTIMPLEMENTED;
	} else {
		return ERR_NOTSUPPORTED;
	}
}

GSM_Error OBEXGEN_DeleteAllTodo(GSM_StateMachine *s)
{
	GSM_Error		error;
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;
	GSM_ToDoEntry		entry;

	/* We need IrMC service for this */
	error = OBEXGEN_Connect(s, OBEX_IRMC);
	if (error != ERR_NONE) return error;

	/* We need count of entries */
	error = OBEXGEN_InitCalLUID(s);
	if (error != ERR_NONE) return error;

	/* Delete all entries */
	entry.Location = 1;
	while (Priv->TodoCount > 0) {
		error = OBEXGEN_DeleteTodo(s, &entry);
		if (error != ERR_NONE && error != ERR_EMPTY) return error;
		entry.Location++;
	}
	return error;
}

/*@}*/

/**
 * \defgroup OBEXcap Phone information using OBEX capability XML or IrMC devinfo
 * \ingroup OBEXPhone
 * @{
 */

GSM_Error OBEXGEN_GetDevinfoField(GSM_StateMachine *s, const char *Name, char *Dest)
{
	char			*pos;
	char			*dest;
	char			match[200];
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;

	if (Priv->OBEXDevinfo == NULL) return ERR_NOTSUPPORTED;

	/* Match begin tag */
	match[0] = 0;
	strcat(match, Name);
	strcat(match, ":");

	pos = strstr(Priv->OBEXDevinfo, match);
	if (pos == NULL) return ERR_INVALIDDATA;
	pos += strlen(match);

	/* Copy to end of line */
	dest = Dest;
	while (*pos != 0 && *pos != '\r' && *pos != '\n') {
		*(dest++) = *(pos++);
	}
	*dest  = 0;

	return ERR_NONE;
}

GSM_Error OBEXGEN_GetCapabilityField(GSM_StateMachine *s, const char *Name, char *Dest)
{
	char			*pos_start;
	char			*pos_end;
	char			match[200];
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;

	if (Priv->OBEXCapability == NULL) return ERR_NOTSUPPORTED;

	/* Match XML begin tag */
	match[0] = 0;
	strcat(match, "<");
	strcat(match, Name);
	strcat(match, ">");

	pos_start = strstr(Priv->OBEXCapability, match);
	if (pos_start == NULL) return ERR_INVALIDDATA;
	pos_start += strlen(match);

	/* Match XML end tag */
	match[0] = 0;
	strcat(match, "</");
	strcat(match, Name);
	strcat(match, ">");

	pos_end = strstr(pos_start, match);
	if (pos_end == NULL) return ERR_INVALIDDATA;

	/* Copy result string */
	strncpy(Dest, pos_start, pos_end - pos_start);
	Dest[pos_end - pos_start] = 0;

	return ERR_NONE;
}

GSM_Error OBEXGEN_GetCapabilityFieldAttrib(GSM_StateMachine *s, const char *Name, const char *Attrib, char *Dest)
{
	char			*pos_start;
	char			*pos_end;
	char			match[200];
	GSM_Phone_OBEXGENData	*Priv = &s->Phone.Data.Priv.OBEXGEN;

	if (Priv->OBEXCapability == NULL) return ERR_NOTSUPPORTED;

	/* Match XML begin tag */
	match[0] = 0;
	strcat(match, "<");
	strcat(match, Name);

	pos_start = strstr(Priv->OBEXCapability, match);
	if (pos_start == NULL) return ERR_INVALIDDATA;
	pos_start += strlen(match);

	/* Match attribute begin */
	match[0] = 0;
	strcat(match, Attrib);
	strcat(match, "=\"");

	pos_start = strstr(pos_start, match);
	if (pos_start == NULL) return ERR_INVALIDDATA;
	pos_start += strlen(match);

	/* Match end quote */
	pos_end = strchr(pos_start, '"');
	if (pos_end == NULL) return ERR_INVALIDDATA;

	/* Copy result string */
	strncpy(Dest, pos_start, pos_end - pos_start);
	Dest[pos_end - pos_start] = 0;

	return ERR_NONE;
}

GSM_Error OBEXGEN_GetManufacturer(GSM_StateMachine *s)
{
	GSM_Error		error;

	if (s->Phone.Data.Manufacturer[0] != 0) return ERR_NONE;

	error = OBEXGEN_GetCapabilityField(s, "Manufacturer", s->Phone.Data.Manufacturer);
	if (error == ERR_NONE) return ERR_NONE;

	return OBEXGEN_GetDevinfoField(s, "MANU", s->Phone.Data.Manufacturer);
}

GSM_Error OBEXGEN_GetModel(GSM_StateMachine *s)
{
	GSM_Error		error;

	if (s->Phone.Data.Model[0] != 0) return ERR_NONE;

	error = OBEXGEN_GetCapabilityField(s, "Model", s->Phone.Data.Model);
	if (error == ERR_NONE) return ERR_NONE;

	return OBEXGEN_GetDevinfoField(s, "MOD", s->Phone.Data.Model);
}

GSM_Error OBEXGEN_GetFirmware(GSM_StateMachine *s)
{
	GSM_Error		error;

	if (s->Phone.Data.Version[0] != 0) return ERR_NONE;

	error = OBEXGEN_GetCapabilityFieldAttrib(s, "SW", "Version", s->Phone.Data.Version);
	if (error == ERR_NONE) {
		/* We don't care about error here, it is optional */
		OBEXGEN_GetCapabilityFieldAttrib(s, "SW", "Date", s->Phone.Data.VerDate);
	}
	if (error == ERR_NONE) return ERR_NONE;
	OBEXGEN_GetDevinfoField(s, "SW-DATE", s->Phone.Data.VerDate);

	return OBEXGEN_GetDevinfoField(s, "SW-VERSION", s->Phone.Data.Version);
}

GSM_Error OBEXGEN_GetIMEI(GSM_StateMachine *s)
{
	GSM_Error		error;

	if (s->Phone.Data.IMEI[0] != 0) return ERR_NONE;

	error = OBEXGEN_GetCapabilityField(s, "SN", s->Phone.Data.IMEI);
	if (error == ERR_NONE) return ERR_NONE;

	return OBEXGEN_GetDevinfoField(s, "SN", s->Phone.Data.IMEI);
}

/*@}*/
GSM_Reply_Function OBEXGENReplyFunctions[] = {
	/* CONTINUE block */
	{OBEXGEN_ReplyAddFilePart,	"\x90",0x00,0x00,ID_AddFile			},
	{OBEXGEN_ReplyGetFilePart,	"\x90",0x00,0x00,ID_GetFile			},

	/* OK block */
	{OBEXGEN_ReplyChangePath,	"\xA0",0x00,0x00,ID_SetPath			},
	{OBEXGEN_ReplyConnect,		"\xA0",0x00,0x00,ID_Initialise			},
	{OBEXGEN_ReplyAddFilePart,	"\xA0",0x00,0x00,ID_AddFile			},
	{OBEXGEN_ReplyGetFilePart,	"\xA0",0x00,0x00,ID_GetFile			},

	/* FOLDER CREATED block */
	{OBEXGEN_ReplyChangePath,	"\xA1",0x00,0x00,ID_SetPath			},

	/* NOT UNDERSTAND block */
	{OBEXGEN_ReplyConnect,		"\xC0",0x00,0x00,ID_Initialise			},
	{OBEXGEN_ReplyGetFilePart,	"\xC0",0x00,0x00,ID_GetFile			},
	{OBEXGEN_ReplyAddFilePart,	"\xC0",0x00,0x00,ID_AddFile			},

	/* Not allowed block */
	{OBEXGEN_ReplyConnect,		"\xC1",0x00,0x00,ID_Initialise			},
	{OBEXGEN_ReplyGetFilePart,	"\xC1",0x00,0x00,ID_GetFile			},
	{OBEXGEN_ReplyAddFilePart,	"\xC1",0x00,0x00,ID_AddFile			},

	/* FORBIDDEN block */
	{OBEXGEN_ReplyChangePath,	"\xC3",0x00,0x00,ID_SetPath			},
	{OBEXGEN_ReplyGetFilePart,	"\xC3",0x00,0x00,ID_GetFile			},
	{OBEXGEN_ReplyAddFilePart,	"\xC3",0x00,0x00,ID_AddFile			},

	/* NOT FOUND block */
	{OBEXGEN_ReplyGetFilePart,	"\xC4",0x00,0x00,ID_GetFile			},
	{OBEXGEN_ReplyChangePath,	"\xC4",0x00,0x00,ID_SetPath			},

	{NULL,				"\x00",0x00,0x00,ID_None			}
};

GSM_Phone_Functions OBEXGENPhone = {
	"obex|seobex|obexirmc|obexnone",
	OBEXGENReplyFunctions,
	OBEXGEN_Initialise,
	OBEXGEN_Terminate,
	GSM_DispatchMessage,
	NOTIMPLEMENTED,			/* 	ShowStartInfo		*/
	OBEXGEN_GetManufacturer,
	OBEXGEN_GetModel,
	OBEXGEN_GetFirmware,
	OBEXGEN_GetIMEI,
	NOTIMPLEMENTED,			/*	GetOriginalIMEI		*/
	NOTIMPLEMENTED,			/*	GetManufactureMonth	*/
	NOTIMPLEMENTED,			/*	GetProductCode		*/
	NOTIMPLEMENTED,			/*	GetHardware		*/
	NOTIMPLEMENTED,			/*	GetPPM			*/
	NOTIMPLEMENTED,			/*	GetSIMIMSI		*/
	NOTIMPLEMENTED,			/*	GetDateTime		*/
	NOTIMPLEMENTED,			/*	SetDateTime		*/
	NOTIMPLEMENTED,			/*	GetAlarm		*/
	NOTIMPLEMENTED,			/*	SetAlarm		*/
	NOTSUPPORTED,			/* 	GetLocale		*/
	NOTSUPPORTED,			/* 	SetLocale		*/
	NOTIMPLEMENTED,			/*	PressKey		*/
	NOTIMPLEMENTED,			/*	Reset			*/
	NOTIMPLEMENTED,			/*	ResetPhoneSettings	*/
	NOTIMPLEMENTED,			/*	EnterSecurityCode	*/
	NOTIMPLEMENTED,			/*	GetSecurityStatus	*/
	NOTIMPLEMENTED,			/*	GetDisplayStatus	*/
	NOTIMPLEMENTED,			/*	SetAutoNetworkLogin	*/
	NOTIMPLEMENTED,			/*	GetBatteryCharge	*/
	NOTIMPLEMENTED,			/*	GetSignalQuality	*/
	NOTIMPLEMENTED,			/*	GetNetworkInfo		*/
	NOTIMPLEMENTED,     		/*  	GetCategory 		*/
 	NOTSUPPORTED,       		/*  	AddCategory 		*/
        NOTIMPLEMENTED,      		/*  	GetCategoryStatus 	*/
	OBEXGEN_GetMemoryStatus,
	OBEXGEN_GetMemory,
	OBEXGEN_GetNextMemory,
	OBEXGEN_SetMemory,
	OBEXGEN_AddMemory,
	OBEXGEN_DeleteMemory,
	OBEXGEN_DeleteAllMemory,
	NOTIMPLEMENTED,			/*	GetSpeedDial		*/
	NOTIMPLEMENTED,			/*	SetSpeedDial		*/
	NOTIMPLEMENTED,			/*	GetSMSC			*/
	NOTIMPLEMENTED,			/*	SetSMSC			*/
	NOTIMPLEMENTED,			/*	GetSMSStatus		*/
	NOTIMPLEMENTED,			/*	GetSMS			*/
	NOTIMPLEMENTED,			/*	GetNextSMS		*/
	NOTIMPLEMENTED,			/*	SetSMS			*/
	NOTIMPLEMENTED,			/*	AddSMS			*/
	NOTIMPLEMENTED,			/* 	DeleteSMS 		*/
	NOTIMPLEMENTED,			/*	SendSMSMessage		*/
	NOTSUPPORTED,			/*	SendSavedSMS		*/
	NOTSUPPORTED,			/*	SetFastSMSSending	*/
	NOTIMPLEMENTED,			/*	SetIncomingSMS		*/
	NOTIMPLEMENTED,			/* 	SetIncomingCB		*/
	NOTIMPLEMENTED,			/*	GetSMSFolders		*/
 	NOTIMPLEMENTED,			/* 	AddSMSFolder		*/
 	NOTIMPLEMENTED,			/* 	DeleteSMSFolder		*/
	NOTIMPLEMENTED,			/*	DialVoice		*/
	NOTIMPLEMENTED,			/*	AnswerCall		*/
	NOTIMPLEMENTED,			/*	CancelCall		*/
 	NOTIMPLEMENTED,			/* 	HoldCall 		*/
 	NOTIMPLEMENTED,			/* 	UnholdCall 		*/
 	NOTIMPLEMENTED,			/* 	ConferenceCall 		*/
 	NOTIMPLEMENTED,			/* 	SplitCall		*/
 	NOTIMPLEMENTED,			/* 	TransferCall		*/
 	NOTIMPLEMENTED,			/* 	SwitchCall		*/
 	NOTIMPLEMENTED,			/* 	GetCallDivert		*/
 	NOTIMPLEMENTED,			/* 	SetCallDivert		*/
 	NOTIMPLEMENTED,			/* 	CancelAllDiverts	*/
	NOTIMPLEMENTED,			/*	SetIncomingCall		*/
	NOTIMPLEMENTED,			/*  	SetIncomingUSSD		*/
	NOTIMPLEMENTED,			/*	SendDTMF		*/
	NOTIMPLEMENTED,			/*	GetRingtone		*/
	NOTIMPLEMENTED,			/*	SetRingtone		*/
	NOTIMPLEMENTED,			/*	GetRingtonesInfo	*/
	NOTIMPLEMENTED,			/* 	DeleteUserRingtones	*/
	NOTIMPLEMENTED,			/*	PlayTone		*/
	NOTIMPLEMENTED,			/*	GetWAPBookmark		*/
	NOTIMPLEMENTED,			/* 	SetWAPBookmark 		*/
	NOTIMPLEMENTED, 		/* 	DeleteWAPBookmark 	*/
	NOTIMPLEMENTED,			/* 	GetWAPSettings 		*/
	NOTIMPLEMENTED,			/* 	SetWAPSettings 		*/
	NOTSUPPORTED,			/*	GetSyncMLSettings	*/
	NOTSUPPORTED,			/*	SetSyncMLSettings	*/
	NOTSUPPORTED,			/*	GetChatSettings		*/
	NOTSUPPORTED,			/*	SetChatSettings		*/
	NOTSUPPORTED,			/* 	GetMMSSettings		*/
	NOTSUPPORTED,			/* 	SetMMSSettings		*/
	NOTSUPPORTED,			/*	GetMMSFolders		*/
	NOTSUPPORTED,			/*	GetNextMMSFileInfo	*/
	NOTIMPLEMENTED,			/*	GetBitmap		*/
	NOTIMPLEMENTED,			/*	SetBitmap		*/
	OBEXGEN_GetTodoStatus,
	OBEXGEN_GetTodo,
	OBEXGEN_GetNextTodo,
	OBEXGEN_SetTodo,
	OBEXGEN_AddTodo,
	OBEXGEN_DeleteTodo,
	OBEXGEN_DeleteAllTodo,
	OBEXGEN_GetCalendarStatus,
	OBEXGEN_GetCalendar,
    	OBEXGEN_GetNextCalendar,
	OBEXGEN_SetCalendar,
	OBEXGEN_AddCalendar,
	OBEXGEN_DeleteCalendar,
	OBEXGEN_DeleteAllCalendar,
	NOTSUPPORTED,			/* 	GetCalendarSettings	*/
	NOTSUPPORTED,			/* 	SetCalendarSettings	*/
	NOTIMPLEMENTED,			/*	GetNoteStatus		*/
	NOTIMPLEMENTED,			/*	GetNote			*/
	NOTIMPLEMENTED,			/*	GetNextNote		*/
	NOTIMPLEMENTED,			/*	SetNote			*/
	NOTIMPLEMENTED,			/*	AddNote			*/
	NOTIMPLEMENTED,			/* 	DeleteNote		*/
	NOTIMPLEMENTED,			/*	DeleteAllNotes		*/
	NOTIMPLEMENTED, 		/*	GetProfile		*/
	NOTIMPLEMENTED, 		/*	SetProfile		*/
    	NOTIMPLEMENTED,			/*  	GetFMStation        	*/
    	NOTIMPLEMENTED,			/*  	SetFMStation        	*/
    	NOTIMPLEMENTED,			/*  	ClearFMStations       	*/
	OBEXGEN_GetNextFileFolder,
	NOTIMPLEMENTED,			/*	GetFolderListing	*/
	NOTSUPPORTED,			/*	GetNextRootFolder	*/
	NOTSUPPORTED,			/*	SetFileAttributes	*/
	OBEXGEN_GetFilePart,
	OBEXGEN_AddFilePart,
	NOTIMPLEMENTED, 		/* 	GetFileSystemStatus	*/
	OBEXGEN_DeleteFile,
	OBEXGEN_AddFolder,
	OBEXGEN_DeleteFile,		/* 	DeleteFolder		*/
	NOTSUPPORTED,			/* 	GetGPRSAccessPoint	*/
	NOTSUPPORTED			/* 	SetGPRSAccessPoint	*/
};

#endif
/*@}*/

/* How should editor hadle tabs in this file? Add editor commands here.
 * vim: noexpandtab sw=8 ts=8 sts=8:
 */
