                                           
#include "../../gsmstate.h"

#ifdef GSM_ENABLE_ATGEN

#include <string.h>
#include <time.h>
#include <ctype.h>

#include "../../gsmcomon.h"
#include "../../misc/coding/coding.h"

extern GSM_Reply_Function 		ATGENReplyFunctions[];
extern GSM_Error ATGEN_DispatchMessage	(GSM_StateMachine *s);

#ifdef GSM_ENABLE_OBEXGEN

extern GSM_Reply_Function 		OBEXGENReplyFunctions[];
extern GSM_Error OBEXGEN_GetFilePart	(GSM_StateMachine *s, GSM_File *File);
extern GSM_Error OBEXGEN_AddFilePart	(GSM_StateMachine *s, GSM_File *File, int *Pos);
extern GSM_Error OBEXGEN_Disconnect	(GSM_StateMachine *s);

#if defined(GSM_ENABLE_BLUEOBEX) || defined(GSM_ENABLE_IRDAOBEX)

static GSM_Error SONYERIC_SetOBEXMode(GSM_StateMachine *s)
{
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;
	GSM_Error		error;

	if (Priv->OBEX) return GE_NONE;

	dbgprintf ("Changing to OBEX\n");

	error=GSM_WaitFor (s, "AT*EOBEX\r", 9, 0x00, 4, ID_SetOBEX);
	if (error != GE_NONE) return error;

	error = s->Protocol.Functions->Terminate(s);
	if (error != GE_NONE) return error;

	s->Protocol.Functions = &OBEXProtocol;
	error = s->Protocol.Functions->Initialise(s);
	if (error != GE_NONE) {
		s->Protocol.Functions = &ATProtocol;
		return error;
	}
	strcpy(s->CurrentConfig->Model,"seobex");
	s->Phone.Data.Priv.OBEXGEN.Service 	= 0;
	s->Phone.Functions->DispatchMessage	= GSM_DispatchMessage;
	s->Phone.Functions->ReplyFunctions	= OBEXGENReplyFunctions;
	Priv->OBEX				= true;
	return GE_NONE;
}

static GSM_Error SONYERIC_SetATMode(GSM_StateMachine *s)
{
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;
	GSM_Error		error;

	if (!Priv->OBEX) return GE_NONE;

	dbgprintf ("Changing to AT\n");

	error = OBEXGEN_Disconnect(s);
	if (error != GE_NONE) return error;

	error = s->Protocol.Functions->Terminate(s);
	if (error != GE_NONE) return error;

	s->Protocol.Functions = &ATProtocol;
	error = s->Protocol.Functions->Initialise(s);
	if (error != GE_NONE) {
		s->Protocol.Functions = &OBEXProtocol;
		return error;
	}
	strcpy(s->CurrentConfig->Model,"at");
	s->Phone.Functions->DispatchMessage	= ATGEN_DispatchMessage;
	s->Phone.Functions->ReplyFunctions	= ATGENReplyFunctions;
	Priv->OBEX				= false;
	return GE_NONE;
}

static GSM_Error SONYERIC_GetFile(GSM_StateMachine *s, GSM_File *File, unsigned char *FileName)
{
	GSM_Error error;

	strcpy(File->ID_FullName,FileName);
	File->Used 	= 0;
	if (File->Buffer != NULL) free(File->Buffer);
	File->Buffer 	= NULL;

	error = SONYERIC_SetOBEXMode(s);
	if (error != GE_NONE) return error;

	error = GE_NONE;
	while (error == GE_NONE) error = OBEXGEN_GetFilePart(s,File);
	if (error != GE_EMPTY) return error;

	return SONYERIC_SetATMode(s);
}

static GSM_Error SONYERIC_SetFile(GSM_StateMachine *s, unsigned char *FileName, unsigned char *Buffer, int Length)
{
	GSM_Error	error;
	GSM_File 	File;
	int		Pos = 0;

	error = SONYERIC_SetOBEXMode(s);
	if (error != GE_NONE) return error;

	strcpy(File.ID_FullName,FileName);
	EncodeUnicode(File.Name,FileName,strlen(FileName));
	File.Used 	= Length;
	File.Buffer 	= malloc(Length);
	memcpy(File.Buffer,Buffer,Length);

	error = GE_NONE;
	while (error == GE_NONE) error = OBEXGEN_AddFilePart(s,&File,&Pos);
	free(File.Buffer);
	if (error != GE_EMPTY) return error;

	return SONYERIC_SetATMode(s);
}

#endif

GSM_Error SONYERIC_GetNextCalendar(GSM_StateMachine *s, GSM_CalendarEntry *Note, bool start)
{
#if defined(GSM_ENABLE_BLUEOBEX) || defined(GSM_ENABLE_IRDAOBEX)
	GSM_Error		error;
	GSM_ToDoEntry		ToDo;
	int			Pos, num, Loc;
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;

	if (start) {
		error = SONYERIC_GetFile(s, &Priv->file, "telecom/cal.vcs");
		if (error != GE_NONE) return error;

		Note->Location = 1;
	} else {
		Note->Location++;
	}
	smprintf(s, "Getting calendar note %i\n",Note->Location);

	Loc = Note->Location;
	Pos = 0;
	num = 0;
	while (1) {
		error = GSM_DecodeVCALENDAR_VTODO(Priv->file.Buffer, &Pos, Note, &ToDo, SonyEricsson_VCalendar, SonyEricsson_VToDo);
		if (error == GE_EMPTY) break;
		if (error != GE_NONE) return error;
		if (Note->EntriesNum != 0) {			
			num++;
			if (num == Loc) return GE_NONE;
		}
	}
	return GE_EMPTY;
#else
	return GE_SOURCENOTAVAILABLE;
#endif
}

GSM_Error SONYERIC_GetNextToDo(GSM_StateMachine *s, GSM_ToDoEntry *ToDo, bool start)
{
#if defined(GSM_ENABLE_BLUEOBEX) || defined(GSM_ENABLE_IRDAOBEX)
	GSM_Error		error;
	GSM_CalendarEntry	Calendar;
	int			Pos, num, Loc;
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;

	if (Priv->Manufacturer!=AT_Ericsson) return GE_NOTSUPPORTED;

	if (start) {
		error = SONYERIC_GetFile(s, &Priv->file, "telecom/cal.vcs");
		if (error != GE_NONE) return error;

		ToDo->Location = 1;
	} else {
		ToDo->Location++;
	}
	smprintf(s,"Getting ToDo %i\n",ToDo->Location);

	Loc = ToDo->Location;	
	Pos = 0;
	num = 0;
	while (1) {
		error = GSM_DecodeVCALENDAR_VTODO(Priv->file.Buffer, &Pos, &Calendar, ToDo, SonyEricsson_VCalendar, SonyEricsson_VToDo);
		if (error == GE_EMPTY) break;
		if (error != GE_NONE) return error;
		if (ToDo->EntriesNum != 0) {			
			num++;
			if (num == Loc) return GE_NONE;
		}
	}

	return GE_EMPTY;
#else
	return GE_SOURCENOTAVAILABLE;
#endif
}

GSM_Error SONYERIC_GetToDoStatus(GSM_StateMachine *s, GSM_ToDoStatus *status)
{
#if defined(GSM_ENABLE_BLUEOBEX) || defined(GSM_ENABLE_IRDAOBEX)
	GSM_Error		error;
	GSM_ToDoEntry		ToDo;
	GSM_CalendarEntry 	Calendar;
	int			Pos;
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;

	if (Priv->Manufacturer!=AT_Ericsson) return GE_NOTSUPPORTED;

	smprintf(s,"Getting ToDo status\n");

	error = SONYERIC_GetFile(s, &Priv->file, "telecom/cal.vcs");
	if (error != GE_NONE) return error;

	status->Used 	= 0;
	Pos 		= 0;
	while (1) {
		error = GSM_DecodeVCALENDAR_VTODO(Priv->file.Buffer, &Pos, &Calendar, &ToDo, SonyEricsson_VCalendar, SonyEricsson_VToDo);
		if (error == GE_EMPTY) break;
		if (error != GE_NONE) return error;
		if (ToDo.EntriesNum != 0) status->Used++;
	}
	
	return GE_NONE;
#else
	return GE_SOURCENOTAVAILABLE;
#endif
}

GSM_Error SONYERIC_AddCalendarNote(GSM_StateMachine *s, GSM_CalendarEntry *Note)
{
#if defined(GSM_ENABLE_BLUEOBEX) || defined(GSM_ENABLE_IRDAOBEX)
	unsigned char 		req[5000];
	int			size=0;

	smprintf(s,"Adding calendar note\n");

	GSM_EncodeVCALENDAR(req,&size,Note,true,SonyEricsson_VCalendar);

	return SONYERIC_SetFile(s, "telecom/cal/luid/.vcs", req, size);
#else
	return GE_SOURCENOTAVAILABLE;
#endif
}

GSM_Error SONYERIC_AddToDo(GSM_StateMachine *s, GSM_ToDoEntry *ToDo)
{
#if defined(GSM_ENABLE_BLUEOBEX) || defined(GSM_ENABLE_IRDAOBEX)
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;
	unsigned char 		req[5000];
	int			size=0;

	if (Priv->Manufacturer!=AT_Ericsson) return GE_NOTSUPPORTED;

	smprintf(s,"Adding ToDo\n");

	GSM_EncodeVTODO(req,&size,ToDo,true,SonyEricsson_VToDo);

	return SONYERIC_SetFile(s, "telecom/cal/luid/.vcs", req, size);
#else
	return GE_SOURCENOTAVAILABLE;
#endif
}

GSM_Error SONYERIC_DeleteAllToDo(GSM_StateMachine *s)
{
#if defined(GSM_ENABLE_BLUEOBEX) || defined(GSM_ENABLE_IRDAOBEX)
	GSM_Error		error;
	int			Pos,Level = 0,Used;
	unsigned char		*Buf;
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;
	unsigned char 		Line[2000];

	if (Priv->Manufacturer!=AT_Ericsson) return GE_NOTSUPPORTED;

	smprintf(s,"Deleting all ToDo\n");

	error = SONYERIC_GetFile(s, &Priv->file, "telecom/cal.vcs");
	if (error != GE_NONE) return error;

	Pos  = 0;
	Buf  = NULL;
	Used = 0;
	while (1) {
		MyGetLine(Priv->file.Buffer, &Pos, Line, Priv->file.Used);
		if (strlen(Line) == 0) break;
		dbgprintf("Line is %s,%i,%i\n",Line,Priv->file.Used,Pos);
		switch (Level) {
		case 0:
			if (strstr(Line,"BEGIN:VTODO")) {
				Level = 2;
				break;
			}
			Buf=(unsigned char *)realloc(Buf,Used+strlen(Line)+3);
			strcpy(Buf+Used,Line);
			Used=Used+strlen(Line)+3;
			Buf[Used-3] = 13;
			Buf[Used-2] = 10;
			Buf[Used-1] = 0x00;
			break;
		case 2: /* ToDo note */
			if (strstr(Line,"END:VTODO")) {
				Level = 0;
			}
			break;
		}
	}

	error = SONYERIC_SetFile(s, "telecom/cal.vcs", Buf, Used);
//	if (Buf != NULL) free(Buf);
	return error;
#else
	return GE_SOURCENOTAVAILABLE;
#endif
}

GSM_Error SONYERIC_DelCalendarNote(GSM_StateMachine *s, GSM_CalendarEntry *Note)
{
#if defined(GSM_ENABLE_BLUEOBEX) || defined(GSM_ENABLE_IRDAOBEX)
	GSM_Error		error;
	int			Pos,Level = 0,Loc=0,Used;
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;
	unsigned char 		Line[2000];
	unsigned char		*Buf;

	smprintf(s, "Deleting calendar note %i\n",Note->Location);

	error = SONYERIC_GetFile(s, &Priv->file, "telecom/cal.vcs");
	if (error != GE_NONE) return error;

	Pos  = 0;
	Buf  = NULL;
	Used = 0;
	while (1) {
		MyGetLine(Priv->file.Buffer, &Pos, Line, Priv->file.Used);
		if (strlen(Line) == 0) break;
		dbgprintf("Line is %s,%i,%i\n",Line,Priv->file.Used,Pos);
		switch (Level) {
		case 0:
			if (strstr(Line,"BEGIN:VEVENT")) {
				Loc++;
				if (Loc == Note->Location) {
					Level = 1;
					break;
				}
			}
			Buf=(unsigned char *)realloc(Buf,Used+strlen(Line)+3);
			strcpy(Buf+Used,Line);
			Used=Used+strlen(Line)+3;
			Buf[Used-3] = 13;
			Buf[Used-2] = 10;
			Buf[Used-1] = 0x00;
			break;
		case 1: /* Calendar note */
			if (strstr(Line,"END:VEVENT")) {
				Level = 0;
			}
			break;
		}
	}

	DumpMessage(stderr,Buf,Used);
	fflush(stderr);

	error = SONYERIC_SetFile(s, "telecom/cal.vcs", Buf, Used);
	if (Buf != NULL) free(Buf);
	return error;
#else
	return GE_SOURCENOTAVAILABLE;
#endif
}

GSM_Error SONYERIC_GetCalendarStatus(GSM_StateMachine *s, GSM_CalendarStatus *Status)
{
#if defined(GSM_ENABLE_BLUEOBEX) || defined(GSM_ENABLE_IRDAOBEX)
	GSM_Error		error;
	GSM_ToDoEntry		ToDo;
	GSM_CalendarEntry 	Calendar;
	int			Pos;
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;

	if (Priv->Manufacturer!=AT_Ericsson) return GE_NOTSUPPORTED;

	smprintf(s, "Getting calendar status\n");

	error = SONYERIC_GetFile(s, &Priv->file, "telecom/cal.vcs");
	if (error != GE_NONE) return error;

	Status->Used 	= 0;
	Pos  		= 0;
	while (1) {
		error = GSM_DecodeVCALENDAR_VTODO(Priv->file.Buffer, &Pos, &Calendar, &ToDo, SonyEricsson_VCalendar, SonyEricsson_VToDo);
		if (error == GE_EMPTY) break;
		if (error != GE_NONE) return error;
		if (Calendar.EntriesNum != 0) Status->Used++;
	}
	
	return GE_NONE;
#else
	return GE_SOURCENOTAVAILABLE;
#endif
}

#endif
#endif

/* How should editor hadle tabs in this file? Add editor commands here.
 * vim: noexpandtab sw=8 ts=8 sts=8:
 */
