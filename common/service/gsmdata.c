/* (c) 2002-2004 by Marcin Wiacek */

#include <string.h>
#include <stdio.h>

#include <gammu-debug.h>
#include <gammu-datetime.h>

#include "gsmdata.h"
#include "../misc/coding/coding.h"

/* http://forum.nokia.com: OTA MMS Settings 1.0, OTA Settings 7.0 */
static void AddWAPSMSParameterText(unsigned char *Buffer, int *Length, unsigned char ID, char *Text, int Len)
{
	int i;

	Buffer[(*Length)++] = 0x87; 			//PARM with attributes
	Buffer[(*Length)++] = ID;
	Buffer[(*Length)++] = 0x11; 			//VALUE
	Buffer[(*Length)++] = 0x03; 			//Inline string
	for (i=0;i<Len;i++) {
		Buffer[(*Length)++] = Text[i];		//Text
	}
	Buffer[(*Length)++] = 0x00; 			//END Inline string
	Buffer[(*Length)++] = 0x01; 			//END PARMeter
}

/* http://forum.nokia.com: OTA MMS Settings 1.0, OTA Settings 7.0 */
static void AddWAPSMSParameterInt(unsigned char *Buffer, int *Length, unsigned char ID, unsigned char Value)
{
	Buffer[(*Length)++] = 0x87; 			//PARM with attributes
	Buffer[(*Length)++] = ID;
	Buffer[(*Length)++] = Value;
	Buffer[(*Length)++] = 0x01; 			//END PARMeter
}

/* http://forum.nokia.com  : OTA MMS Settings 1.0, OTA Settings 7.0
 * http://www.wapforum.org : Wireless Datagram Protocol
 */
void NOKIA_EncodeWAPMMSSettingsSMSText(unsigned char *Buffer, int *Length, GSM_WAPSettings *settings, bool MMS)
{
	int 		i;
	unsigned char 	buffer[400];

	Buffer[(*Length)++] = 0x01; 			//Push ID
	Buffer[(*Length)++] = 0x06; 			//PDU Type (push)
	Buffer[(*Length)++] = 0x2C; 			//Headers length (content type + headers)
	strcpy(Buffer+(*Length),"\x1F\x2A");
	(*Length)=(*Length)+2;				//Value length
	strcpy(Buffer+(*Length),"application/x-wap-prov.browser-settings");
	(*Length)=(*Length)+39;				//MIME-Type
	Buffer[(*Length)++] = 0x00; 			//end inline string
	strcpy(Buffer+(*Length),"\x81\xEA");
	(*Length)=(*Length)+2;				//charset UTF-8 short int.
	strcpy(Buffer+(*Length),"\x01\x01");
	(*Length)=(*Length)+2;				//version WBXML 1.1
	Buffer[(*Length)++] = 0x6A; 			//charset UTF-8
	Buffer[(*Length)++] = 0x00; 			//string table length

	Buffer[(*Length)++] = 0x45; 			//CHARACTERISTIC-LIST with content
		Buffer[(*Length)++] = 0xC6; 		//CHARACTERISTIC with content and attributes
		Buffer[(*Length)++] = 0x06; 		//TYPE=ADDRESS
		Buffer[(*Length)++] = 0x01; 		//END PARMeter
			switch (settings->Bearer) {
			case WAPSETTINGS_BEARER_GPRS:
				/* Bearer */
				AddWAPSMSParameterInt(Buffer, Length, 0x12, 0x49);
				/* PPP_LOGINTYPE (manual login or not) */
				if (settings->ManualLogin) {
					AddWAPSMSParameterInt(Buffer, Length, 0x1D, 0x65);
				} else {
					AddWAPSMSParameterInt(Buffer, Length, 0x1D, 0x64);
				}
				/* PPP_AUTHTYPE*/
				if (settings->IsNormalAuthentication) {
					/* OTA_CSD_AUTHTYPE_PAP */
					AddWAPSMSParameterInt(Buffer, Length, 0x22, 0x70);
				} else {
					/* OTA_CSD_AUTHTYPE_CHAP */
					AddWAPSMSParameterInt(Buffer, Length, 0x22, 0x71);
				}
				/* GPRS_ACCESSPOINTNAME */
				AddWAPSMSParameterText(Buffer, Length, 0x1C, DecodeUnicodeString(settings->DialUp), UnicodeLength(settings->DialUp));
				/* PROXY */
				AddWAPSMSParameterText(Buffer, Length, 0x13, DecodeUnicodeString(settings->IPAddress), UnicodeLength(settings->IPAddress));
				/* PPP_AUTHNAME (user) */
				AddWAPSMSParameterText(Buffer, Length, 0x23, DecodeUnicodeString(settings->User), UnicodeLength(settings->User));
				/* PPP_AUTHSECRET (password) */
				AddWAPSMSParameterText(Buffer, Length, 0x24, DecodeUnicodeString(settings->Password), UnicodeLength(settings->Password));
				break;
			case WAPSETTINGS_BEARER_DATA:
				/* Bearer */
				AddWAPSMSParameterInt(Buffer, Length, 0x12, 0x45);
				/* CSD_DIALSTRING */
				AddWAPSMSParameterText(Buffer, Length, 0x21, DecodeUnicodeString(settings->DialUp), UnicodeLength(settings->DialUp));
				/* PROXY */
				AddWAPSMSParameterText(Buffer, Length, 0x13, DecodeUnicodeString(settings->IPAddress), UnicodeLength(settings->IPAddress));
				/* PPP_LOGINTYPE (manual login or not) */
				if (settings->ManualLogin) {
					AddWAPSMSParameterInt(Buffer, Length, 0x1D, 0x65);
				} else {
					AddWAPSMSParameterInt(Buffer, Length, 0x1D, 0x64);
				}
				/* PPP_AUTHTYPE*/
				if (settings->IsNormalAuthentication) {
					/* OTA_CSD_AUTHTYPE_PAP */
					AddWAPSMSParameterInt(Buffer, Length, 0x22, 0x70);
				} else {
					/* OTA_CSD_AUTHTYPE_CHAP */
					AddWAPSMSParameterInt(Buffer, Length, 0x22, 0x71);
				}
				/* CSD_CALLTYPE (type of call) */
				if (settings->IsISDNCall) {
					/* ISDN */
					AddWAPSMSParameterInt(Buffer, Length, 0x28, 0x73);
				} else {
					/* analogue */
					AddWAPSMSParameterInt(Buffer, Length, 0x28, 0x72);
				}
				/* CSD_CALLSPEED (speed of call) */
				switch (settings->Speed) {
				case WAPSETTINGS_SPEED_AUTO:
					AddWAPSMSParameterInt(Buffer, Length, 0x29, 0x6A);
					break;
				case WAPSETTINGS_SPEED_9600:
					AddWAPSMSParameterInt(Buffer, Length, 0x29, 0x6B);
					break;
				case WAPSETTINGS_SPEED_14400:
					AddWAPSMSParameterInt(Buffer, Length, 0x29, 0x6C);
				}
				/* PPP_AUTHNAME (user) */
				AddWAPSMSParameterText(Buffer, Length, 0x23, DecodeUnicodeString(settings->User), UnicodeLength(settings->User));
				/* PPP_AUTHSECRET (password) */
				AddWAPSMSParameterText(Buffer, Length, 0x24, DecodeUnicodeString(settings->Password), UnicodeLength(settings->Password));
				break;
#ifdef DEVELOP
			case WAPSETTINGS_BEARER_SMS:
				/* Bearer */
				AddWAPSMSParameterInt(Buffer, Length, 0x12, 0x41);
				/* PROXY */
				AddWAPSMSParameterText(Buffer, Length, 0x13, DecodeUnicodeString(settings->Server), UnicodeLength(settings->Server));
				/* SMS_SMSC_ADDRESS */
				// .....
				break;
			case WAPSETTINGS_BEARER_USSD:
				/* FIXME */
				/* Bearer */
				AddWAPSMSParameterInt(Buffer, Length, 0x12, 0x41);
				/* PROXY */
				AddWAPSMSParameterText(Buffer, Length, 0x13, DecodeUnicodeString(settings->Service), UnicodeLength(settings->Service));
				/* USSD_SERVICE_CODE */
				/* FIXME */
				AddWAPSMSParameterText(Buffer, Length, 0x13, DecodeUnicodeString(settings->Code), UnicodeLength(settings->Code));
#else
			case WAPSETTINGS_BEARER_SMS:
			case WAPSETTINGS_BEARER_USSD:
				break;
#endif
			}
			/* PORT */
			if (settings->IsSecurity) {
				if (settings->IsContinuous) {
					/* Port = 9203. Continuous */
					AddWAPSMSParameterInt(Buffer, Length, 0x14, 0x63);
				} else {
					/* Port = 9202. Temporary */
					AddWAPSMSParameterInt(Buffer, Length, 0x14, 0x62);
				}
			} else {
				if (settings->IsContinuous) {
					/* Port = 9201. Continuous */
					AddWAPSMSParameterInt(Buffer, Length, 0x14, 0x61);
				} else {
					/* Port = 9200. Temporary */
					AddWAPSMSParameterInt(Buffer, Length, 0x14, 0x60);
				}
			}
		Buffer[(*Length)++] = 0x01; 		//END PARMeter

		/* URL */
		Buffer[(*Length)++] = 0x86; 		//CHARACTERISTIC-LIST with attributes
		if (MMS) {
			Buffer[(*Length)++] = 0x7C; 	//TYPE = MMSURL
		} else {
			Buffer[(*Length)++] = 0x07; 	//TYPE = URL
		}
		Buffer[(*Length)++] = 0x11; 		//VALUE
		Buffer[(*Length)++] = 0x03; 		//Inline string
		sprintf(buffer,"%s",DecodeUnicodeString(settings->HomePage));
		for (i=0;i<(int)strlen(buffer);i++) {
			Buffer[(*Length)++] = buffer[i];//Text
		}
		Buffer[(*Length)++] = 0x00; 		//END Inline string
		Buffer[(*Length)++] = 0x01; 		//END PARMeter

		/* ISP_NAME (name) */
		Buffer[(*Length)++] = 0xC6; 		//CHARACTERISTIC with content and attributes
		Buffer[(*Length)++] = 0x08; 		//TYPE=NAME
		Buffer[(*Length)++] = 0x01; 		//END PARMeter
			/* Settings name */
			AddWAPSMSParameterText(Buffer, Length, 0x15, DecodeUnicodeString(settings->Title), UnicodeLength(settings->Title));
		Buffer[(*Length)++] = 0x01; 		//END PARMeter
	Buffer[(*Length)++] = 0x01;			//END PARMeter
}

/* http://forum.nokia.com: OTA Settings 7.0 */
/* first it used default/ISO coding */
/* Joergen Thomsen changed to UTF8 */
void NOKIA_EncodeWAPBookmarkSMSText(unsigned char *Buffer, int *Length, GSM_WAPBookmark *bookmark)
{
	unsigned char	buffer[100];

//	bool		UnicodeCoding = false;
//	EncodeUTF8QuotedPrintable(buffer,bookmark->Title);
//	if (UnicodeLength(bookmark->Title)!=strlen(buffer)) UnicodeCoding = true;

	Buffer[(*Length)++] = 0x01; 			//Push ID
	Buffer[(*Length)++] = 0x06; 			//PDU Type (push)
	Buffer[(*Length)++] = 0x2D; 			//Headers length (content type + headers)
	strcpy(Buffer+(*Length),"\x1F\x2B");
	(*Length)=(*Length)+2;				//Value length
	strcpy(Buffer+(*Length),"application/x-wap-prov.browser-bookmarks");
	(*Length)=(*Length)+40;				//MIME-Type
	Buffer[(*Length)++] = 0x00; 			//end inline string
	strcpy(Buffer+(*Length),"\x81\xEA");
	(*Length)=(*Length)+2;				//charset UTF-8 short int.

	/* removed by Joergen Thomsen */
	/* Block from sniffs. UNKNOWN */
//	if (!UnicodeCoding) {
//		Buffer[(*Length)++] = 0x00;
//		Buffer[(*Length)++] = 0x01;
//	} else {
//		strcpy(Buffer+(*Length),"\x01\x01\x87\x68");
//		(*Length)=(*Length)+4;
//	}
//	Buffer[(*Length)++] = 0x00;

	/* added by Joergen Thomsen */
	Buffer[(*Length)++] = 0x01;			// Version WBXML 1.1
	Buffer[(*Length)++] = 0x01;			// Unknown public identifier
	Buffer[(*Length)++] = 0x6A;			// charset UTF-8
	Buffer[(*Length)++] = 0x00;			// string table length

	Buffer[(*Length)++] = 0x45; 			//CHARACTERISTIC-LIST with content
		/* URL */
		Buffer[(*Length)++] = 0xC6; 		//CHARACTERISTIC with content and attributes
		Buffer[(*Length)++] = 0x7F;             //TYPE = BOOKMARK
		Buffer[(*Length)++] = 0x01; 		//END PARMeter

			/* removed by Joergen Thomsen */
//			if (!UnicodeCoding) {
//				/* TITLE */
//				AddWAPSMSParameterText(Buffer, Length, 0x15, DecodeUnicodeString(bookmark->Title), UnicodeLength(bookmark->Title));
//				/* URL */
//				AddWAPSMSParameterText(Buffer, Length, 0x17, DecodeUnicodeString(bookmark->Address), UnicodeLength(bookmark->Address));
//			} else {
//				/* TITLE */
//				AddWAPSMSParameterText(Buffer, Length, 0x15, bookmark->Title, UnicodeLength(bookmark->Title)*2+1);
//				/* URL */
//				AddWAPSMSParameterText(Buffer, Length, 0x17, bookmark->Address, UnicodeLength(bookmark->Address)*2+1);
//			}

			/* added by Joergen Thomsen */
			/* TITLE */
			EncodeUTF8(buffer, bookmark->Title);
			AddWAPSMSParameterText(Buffer, Length, 0x15, buffer, strlen(buffer));
			/* URL */
			EncodeUTF8(buffer, bookmark->Address);
			AddWAPSMSParameterText(Buffer, Length, 0x17, buffer, strlen(buffer));

		Buffer[(*Length)++] = 0x01;		//END (CHARACTERISTIC)
	Buffer[(*Length)++] = 0x01;			//END (CHARACTERISTIC-LIST)
}

void GSM_EncodeWAPIndicatorSMSText(unsigned char *Buffer, int *Length, char *Text, char *URL)
{
	int i;

	Buffer[(*Length)++] = 0x01; 			//Push ID
	Buffer[(*Length)++] = 0x06; 			//PDU Type (push)
	Buffer[(*Length)++] = 28; 			//Headers length (content type + headers)
	strcpy(Buffer+(*Length),"\x1F\x23");
	(*Length)=(*Length)+2;				//Value length
	strcpy(Buffer+(*Length),"application/vnd.wap.sic");
	(*Length)=(*Length)+23;				//MIME-Type
	Buffer[(*Length)++] = 0x00; 			//end inline string
	strcpy(Buffer+(*Length),"\x81\xEA");
	(*Length)=(*Length)+2;				//charset UTF-8 short int.

	Buffer[(*Length)++] = 0x02; 			// WBXML 1.2
	Buffer[(*Length)++] = 0x05; 			// SI 1.0 Public Identifier
	Buffer[(*Length)++] = 0x6A;			// charset UTF-8
	Buffer[(*Length)++] = 0x00;			// string table length
	Buffer[(*Length)++] = 0x45;			// SI with content
		Buffer[(*Length)++] = 0xC6;		// indication with content and attributes
			Buffer[(*Length)++] = 0x0B;	// address
			Buffer[(*Length)++] = 0x03; 	// Inline string
			for (i=0;i<(int)strlen(URL);i++) {
				Buffer[(*Length)++] = URL[i];//Text
			}
			Buffer[(*Length)++] = 0x00; 	// END Inline string

#ifdef XXX
			Buffer[(*Length)++] = 0x0A;	// created...
			Buffer[(*Length)++] = 0xC3;	// OPAQUE
			Buffer[(*Length)++] = 0x07;	// length
			Buffer[(*Length)++] = 0x19;	// year
			Buffer[(*Length)++] = 0x80;	// year
			Buffer[(*Length)++] = 0x21;	// month
			Buffer[(*Length)++] = 0x12;	// ..
			Buffer[(*Length)++] = 0x00;	// ..
			Buffer[(*Length)++] = 0x00;	// ..
			Buffer[(*Length)++] = 0x00;	// ..
			Buffer[(*Length)++] = 0x10;	// expires
			Buffer[(*Length)++] = 0xC3;	// OPAQUE
			Buffer[(*Length)++] = 0x04;	// length
			Buffer[(*Length)++] = 0x20;	// year
			Buffer[(*Length)++] = 0x10;	// year
			Buffer[(*Length)++] = 0x06;	// month
			Buffer[(*Length)++] = 0x25;	// day
#endif

		Buffer[(*Length)++] = 0x01;		// END (indication)
		Buffer[(*Length)++] = 0x03; 		// Inline string
		for (i=0;i<(int)strlen(Text);i++) {
			Buffer[(*Length)++] = Text[i];	//Text
		}
		Buffer[(*Length)++] = 0x00; 		// END Inline string
		Buffer[(*Length)++] = 0x01;		// END (indication)
	Buffer[(*Length)++] = 0x01;			// END (SI)
}

GSM_Error GSM_EncodeURLFile(unsigned char *Buffer, int *Length, GSM_WAPBookmark *bookmark)
{
	*Length+=sprintf(Buffer+(*Length), "BEGIN:VBKM%c%c",13,10);
	*Length+=sprintf(Buffer+(*Length), "VERSION:1.0%c%c",13,10);
	*Length+=sprintf(Buffer+(*Length), "TITLE:%s%c%c",DecodeUnicodeString(bookmark->Title),13,10);
	*Length+=sprintf(Buffer+(*Length), "URL:%s%c%c",DecodeUnicodeString(bookmark->Address),13,10);
	*Length+=sprintf(Buffer+(*Length), "BEGIN:ENV%c%c",13,10);
	*Length+=sprintf(Buffer+(*Length), "X-IRMC-URL;QUOTED-PRINTABLE:=%c%c",13,10);
	*Length+=sprintf(Buffer+(*Length), "[InternetShortcut] =%c%c",13,10);
	*Length+=sprintf(Buffer+(*Length), "URL=%s%c%c",DecodeUnicodeString(bookmark->Address),13,10);
	*Length+=sprintf(Buffer+(*Length), "END:ENV%c%c",13,10);
	*Length+=sprintf(Buffer+(*Length), "END:VBKM%c%c",13,10);

	return ERR_NONE;
}

/* -------------------------------- MMS ------------------------------------ */

/* SNIFFS, specs somewhere in http://www.wapforum.org */
void GSM_EncodeMMSIndicatorSMSText(unsigned char *Buffer, int *Length, GSM_MMSIndicator Indicator)
{
	unsigned char 	buffer[200];
	int		i;

	strcpy(Buffer+(*Length),"\xE6\x06\"");
	(*Length)=(*Length)+3;
	strcpy(Buffer+(*Length),"application/vnd.wap.mms-message");
	(*Length)=(*Length)+31;
	Buffer[(*Length)++] = 0x00;

	strcpy(Buffer+(*Length),"\xAF\x84\x8C\x82\x98");
	(*Length)=(*Length)+5;

	i = strlen(Indicator.Address);
	while (Indicator.Address[i] != '/' && i!=0) i--;
	strcpy(Buffer+(*Length),Indicator.Address+i+1);
	(*Length)=(*Length)+strlen(Indicator.Address+i+1);
	Buffer[(*Length)++] = 0x00;

	strcpy(Buffer+(*Length),"\x8D\x90\x89");
	(*Length)=(*Length)+3;

	sprintf(buffer,"%s/TYPE=PLMN",Indicator.Sender);
	Buffer[(*Length)++] = strlen(buffer);
	Buffer[(*Length)++] = 0x80;
	strcpy(Buffer+(*Length),buffer);
	(*Length)=(*Length)+strlen(buffer);
	Buffer[(*Length)++] = 0x00;

	Buffer[(*Length)++] = 0x96;
	strcpy(Buffer+(*Length),Indicator.Title);
	(*Length)=(*Length)+strlen(Indicator.Title);
	Buffer[(*Length)++] = 0x00;

	strcpy(Buffer+(*Length),"\x8A\x80\x8E\x02\x47\xBB\x88\x05\x81\x03\x02\xA3");
	(*Length)=(*Length)+12;
	Buffer[(*Length)++] = 0x00;

	Buffer[(*Length)++] = 0x83;
	strcpy(Buffer+(*Length),Indicator.Address);
	(*Length)=(*Length)+strlen(Indicator.Address);
	Buffer[(*Length)++] = 0x00;
}

GSM_Error GSM_ClearMMSMultiPart(GSM_EncodedMultiPartMMSInfo *info)
{
	int i;

	for (i=0;i<GSM_MAX_MULTI_MMS;i++) {
		if (info->Entries[i].File.Buffer != NULL) free(info->Entries[i].File.Buffer);
	}

	memset(info,0,sizeof(GSM_EncodedMultiPartMMSInfo));

	for (i=0;i<GSM_MAX_MULTI_MMS;i++) info->Entries[i].File.Buffer = NULL;
	info->DateTimeAvailable = false;

	return ERR_NONE;
}

void GSM_AddWAPMIMEType(int type, unsigned char *buffer)
{
	switch (type) {
	case  3:sprintf(buffer,"%stext/plain",buffer);					break;
	case  6:sprintf(buffer,"%stext/x-vCalendar",buffer);				break;
	case  7:sprintf(buffer,"%stext/x-vCard",buffer);				break;
	case 29:sprintf(buffer,"%simage/gif",buffer);					break;
	case 30:sprintf(buffer,"%simage/jpeg",buffer);					break;
	case 35:sprintf(buffer,"%sapplication/vnd.wap.multipart.mixed",buffer);		break;
	case 51:sprintf(buffer,"%sapplication/vnd.wap.multipart.related",buffer); 	break;
	default:sprintf(buffer,"%sMIME %i",buffer,type);				break;
	}
}

GSM_Error GSM_DecodeMMSFileToMultiPart(GSM_File *file, GSM_EncodedMultiPartMMSInfo *info)
{
	int 		pos=0,type=0,parts,j;
	int		i,len2,len3,len4,value2;
	long 		value;
	time_t 		timet;
	GSM_DateTime 	Date;
	char		buff[200],buff2[200];

	//header
	while(1) {
		if (pos > file->Used) break;
		if (!(file->Buffer[pos] & 0x80)) break;
		switch (file->Buffer[pos++] & 0x7F) {
		case 0x01:
			dbgprintf("  BCC               : not done yet\n");
			return ERR_FILENOTSUPPORTED;
		case 0x02:
			dbgprintf("  CC                : ");
			i = 0;
			while (file->Buffer[pos]!=0x00) {
				buff[i++] = file->Buffer[pos++];
			}
			buff[i] = 0;
			pos++;
			if (strstr(buff,"/TYPE=PLMN")!=NULL) {
				buff[strlen(buff)-10] = 0;
				info->CCType = MMSADDRESS_PHONE;
				dbgprintf("phone %s\n",buff);
			} else {
				info->CCType = MMSADDRESS_UNKNOWN;
				dbgprintf("%s\n",buff);
			}
			EncodeUnicode(info->CC,buff,strlen(buff));
			break;
		case 0x03:
			dbgprintf("  Content location  : not done yet\n");
			return ERR_FILENOTSUPPORTED;
		case 0x04:
			dbgprintf("  Content type      : ");
			buff[0] = 0;
			if (file->Buffer[pos] <= 0x1E) {
				len2 = file->Buffer[pos++];
				type = file->Buffer[pos++] & 0x7f;
				GSM_AddWAPMIMEType(type, buff);
				i=0;
				while (i<len2) {
					switch (file->Buffer[pos+i]) {
					case 0x89:
						sprintf(buff,"%s; type=",buff);
						i++;
						while (file->Buffer[pos+i]!=0x00) {
							buff[strlen(buff)+1] = 0;
							buff[strlen(buff)]   = file->Buffer[pos+i];
							i++;
						}
						i++;
						break;
					case 0x8A:
						sprintf(buff,"%s; start=",buff);
						i++;
						while (file->Buffer[pos+i]!=0x00) {
							buff[strlen(buff)+1] = 0;
							buff[strlen(buff)]   = file->Buffer[pos+i];
							i++;
						}
						i++;
						break;
					default:
						i++;
						break;
					}
				}
				pos+=len2-1;
			} else if (file->Buffer[pos] == 0x1F) {
				//hack from coded files
				len2 = file->Buffer[pos++];
				type = file->Buffer[pos++] & 0x7f;
				type +=2;
				GSM_AddWAPMIMEType(type, buff);
				i=0;
				while (i<len2) {
					switch (file->Buffer[pos+i]) {
					case 0x89:
						sprintf(buff,"%s; type=",buff);
						i++;
						while (file->Buffer[pos+i]!=0x00) {
							buff[strlen(buff)+1] = 0;
							buff[strlen(buff)]   = file->Buffer[pos+i];
							i++;
						}
						i++;
						break;
					case 0x8A:
						sprintf(buff,"%s; start=",buff);
						i++;
						while (file->Buffer[pos+i]!=0x00) {
							buff[strlen(buff)+1] = 0;
							buff[strlen(buff)]   = file->Buffer[pos+i];
							i++;
						}
						i++;
						break;
					default:
						i++;
						break;
					}
				}
				pos+=len2+2;
			} else if (file->Buffer[pos] >= 0x20 && file->Buffer[pos] <= 0x7F) {
				dbgprintf("not done yet 2\n");
				return ERR_FILENOTSUPPORTED;
			} else if (file->Buffer[pos] >= 0x80 && file->Buffer[pos] < 0xFF) {
				type = file->Buffer[pos++] & 0x7f;
				GSM_AddWAPMIMEType(type, buff);
			}
			dbgprintf("%s\n",buff);
			EncodeUnicode(info->ContentType,buff,strlen(buff));
			break;
		case 0x05:
			dbgprintf("  Date              : ");
			value=0;
			len2 = file->Buffer[pos++];
			for (i=0;i<len2;i++) {
				value=value<<8;
				value |= file->Buffer[pos++];
			}
			timet = value;
			Fill_GSM_DateTime(&Date, timet);
			Date.Year = Date.Year + 1900;
			dbgprintf("%s\n",OSDateTime(Date,0));
			info->DateTimeAvailable = true;
			memcpy(&info->DateTime,&Date,sizeof(GSM_DateTime));
			break;
		case 0x06:
			dbgprintf("  Delivery report   : ");
			info->MMSReportAvailable = true;
			switch(file->Buffer[pos++]) {
				case 0x80:
					dbgprintf("yes\n");
					info->MMSReport = true;
					break;
				case 0x81:
					dbgprintf("no\n");
					info->MMSReport = false;
					break;
				default:
					dbgprintf("unknown\n");
					return ERR_FILENOTSUPPORTED;
			}
			break;
		case 0x08:
			dbgprintf("  Expiry            : ");
			pos++; //length?
			switch (file->Buffer[pos]) {
				case 0x80: dbgprintf("date - ignored\n");	 	 break;
				case 0x81: dbgprintf("seconds - ignored\n");	 break;
				default  : dbgprintf("unknown %02x\n",file->Buffer[pos]);	 break;
			}
			pos++;
			pos++; //expiry
			pos++; //expiry
			pos++; //expiry
			pos++; //expiry
			break;
		case 0x09:
			pos++;
			pos++;
			if (file->Buffer[pos-1] == 128) {
				dbgprintf("  From              : ");
				len2=file->Buffer[pos-2]-1;
				for (i=0;i<len2;i++) {
					buff[i] = file->Buffer[pos++];
				}
				buff[i] = 0;
				if (strstr(buff,"/TYPE=PLMN")!=NULL) {
					buff[strlen(buff)-10] = 0;
					info->SourceType = MMSADDRESS_PHONE;
					dbgprintf("phone %s\n",buff);
				} else {
					info->SourceType = MMSADDRESS_UNKNOWN;
					dbgprintf("%s\n",buff);
				}
				EncodeUnicode(info->Source,buff,strlen(buff));
			}
			break;
		case 0x0A:
			dbgprintf("  Message class     : ");
			switch (file->Buffer[pos++]) {
				case 0x80: dbgprintf("personal\n");	 break;
				case 0x81: dbgprintf("advertisment\n");	 break;
				case 0x82: dbgprintf("informational\n"); break;
				case 0x83: dbgprintf("auto\n");		 break;
				default  : dbgprintf("unknown\n");	 break;
			}
			break;
		case 0x0B:
			dbgprintf("  Message ID        : ");
			while (file->Buffer[pos]!=0x00) {
				dbgprintf("%c",file->Buffer[pos]);
				pos++;
			}
			dbgprintf("\n");
			pos++;
			break;
		case 0x0C:
			dbgprintf("  Message type      : ");
			switch (file->Buffer[pos++]) {
				case 0x80: sprintf(info->MSGType,"m-send-req");  	   	break;
				case 0x81: sprintf(info->MSGType,"m-send-conf"); 	   	break;
				case 0x82: sprintf(info->MSGType,"m-notification-ind"); 	break;
				case 0x83: sprintf(info->MSGType,"m-notifyresp-ind");   	break;
				case 0x84: sprintf(info->MSGType,"m-retrieve-conf");		break;
				case 0x85: sprintf(info->MSGType,"m-acknowledge-ind");  	break;
				case 0x86: sprintf(info->MSGType,"m-delivery-ind");		break;
				default  : dbgprintf("unknown\n"); 	   			return ERR_FILENOTSUPPORTED;
			}
			dbgprintf("%s\n",info->MSGType);
			break;
		case 0x0D:
			value2 = file->Buffer[pos] & 0x7F;
			dbgprintf("  MMS version       : %i.%i\n", (value2 & 0x70) >> 4, value2 & 0x0f);
			pos++;
			break;
		case 0x0E:
			dbgprintf("  Message size      : not done yet\n");
			return ERR_FILENOTSUPPORTED;
		case 0x0F:
			dbgprintf("  Priority          : ");
			switch (file->Buffer[pos++]) {
				case 0x80: dbgprintf("low\n");		break;
				case 0x81: dbgprintf("normal\n");	break;
				case 0x82: dbgprintf("high\n");		break;
				default  : dbgprintf("unknown\n");	break;
			}
			break;
		case 0x10:
			dbgprintf("  Read reply        : ");
			switch(file->Buffer[pos++]) {
				case 0x80: dbgprintf("yes\n"); 		break;
				case 0x81: dbgprintf("no\n");  		break;
				default  : dbgprintf("unknown\n");
			}
			break;
		case 0x11:
			dbgprintf("  Report allowed    : not done yet\n");
			return ERR_FILENOTSUPPORTED;
		case 0x12:
			dbgprintf("  Response status   : not done yet\n");
			return ERR_FILENOTSUPPORTED;
		case 0x13:
			dbgprintf("  Response text     : not done yet\n");
			return ERR_FILENOTSUPPORTED;
		case 0x14:
			dbgprintf("  Sender visibility : not done yet\n");
			return ERR_FILENOTSUPPORTED;
		case 0x15:
			dbgprintf("  Status            : ");
			switch (file->Buffer[pos++]) {
				case 0x80: dbgprintf("expired\n");	break;
				case 0x81: dbgprintf("retrieved\n");	break;
				case 0x82: dbgprintf("rejected\n");	break;
				case 0x83: dbgprintf("deferred\n");	break;
				case 0x84: dbgprintf("unrecognized\n");	break;
				default  : dbgprintf("unknown\n");
			}
			pos++;
			pos++;
			break;
		case 0x16:
			dbgprintf("  Subject           : ");
			if (file->Buffer[pos+1]==0xEA) {
				pos+=2;
			}
			i = 0;
			while (file->Buffer[pos]!=0x00) {
				buff[i++] = file->Buffer[pos++];
			}
			buff[i] = 0;
			dbgprintf("%s\n",buff);
			EncodeUnicode(info->Subject,buff,strlen(buff));
			pos++;
			break;
		case 0x17:
			dbgprintf("  To                : ");
			i = 0;
			while (file->Buffer[pos]!=0x00) {
				buff[i++] = file->Buffer[pos++];
			}
			buff[i] = 0;
			if (strstr(buff,"/TYPE=PLMN")!=NULL) {
				buff[strlen(buff)-10] = 0;
				info->DestinationType = MMSADDRESS_PHONE;
				dbgprintf("phone %s\n",buff);
			} else {
				info->DestinationType = MMSADDRESS_UNKNOWN;
				dbgprintf("%s\n",buff);
			}
			EncodeUnicode(info->Destination,buff,strlen(buff));
			pos++;
			break;
		case 0x18:
			dbgprintf("  Transaction ID    : ");
			while (file->Buffer[pos]!=0x00) {
				dbgprintf("%c",file->Buffer[pos]);
				pos++;
			}
			dbgprintf("\n");
			pos++;
			break;
		default:
			dbgprintf("  unknown1\n");
			break;
		}
	}

	//if we don't have any parts, we exit
	if (type != 35 && type != 51) return ERR_NONE;

	value = 0;
	while (true) {
		value = value << 7;
		value |= file->Buffer[pos] & 0x7F;
		pos++;
		if (!(file->Buffer[pos-1] & 0x80)) break;
	}
	value2 = value;
	dbgprintf("  Parts             : %i\n",value2);
	parts = value;

	for (j=0;j<parts;j++) {
		value = 0;
		while (true) {
			value = value << 7;
			value |= file->Buffer[pos] & 0x7F;
			pos++;
			if (!(file->Buffer[pos-1] & 0x80)) break;
		}
		dbgprintf("    Header len: %li",value);
		len2 = value;

		value = 0;
		while (true) {
			value = value << 7;
			value |= file->Buffer[pos] & 0x7F;
			pos++;
			if (!(file->Buffer[pos-1] & 0x80)) break;
		}
		dbgprintf(", data len: %li\n",value);
		len3 = value;

		//content type
		i 	= 0;
		buff[0] = 0;
		dbgprintf("    Content type    : ");
		if (file->Buffer[pos] >= 0x80) {
			type = file->Buffer[pos] & 0x7f;
			GSM_AddWAPMIMEType(type, buff);
		} else if (file->Buffer[pos+i] == 0x1F) {
			i++;
			buff[0] = 0;
			len4 	= file->Buffer[pos+i];
			i++;
			if (!(file->Buffer[pos+i] & 0x80)) {
				while (file->Buffer[pos+i]!=0x00) {
					buff[strlen(buff)+1] = 0;
					buff[strlen(buff)]   = file->Buffer[pos+i];
					i++;
				}
				i++;
			} else {
				value = file->Buffer[pos+i] & 0x7F;
				GSM_AddWAPMIMEType(value, buff);
				i++;
			}
		} else if (file->Buffer[pos+i] < 0x1F) {
			i++;
			if (file->Buffer[pos+i] & 0x80) {
				type = file->Buffer[pos+i] & 0x7f;
				GSM_AddWAPMIMEType(type, buff);
				i++;
			} else {
				while (file->Buffer[pos+i]!=0x00) {
					buff[strlen(buff)+1] = 0;
					buff[strlen(buff)]   = file->Buffer[pos+i];
					i++;
				}
				i++;
			}
		} else {
			while (file->Buffer[pos+i]!=0x00) {
				buff[strlen(buff)+1] = 0;
				buff[strlen(buff)]   = file->Buffer[pos+i];
				i++;
			}
		}
		dbgprintf("%s\n",buff);
		EncodeUnicode(info->Entries[info->EntriesNum].ContentType,buff,strlen(buff));

		pos+=i;
		len2-=i;

		i=0;
		while (i<len2) {
//			dbgprintf("%02x\n",file->Buffer[pos+i]);
			switch (file->Buffer[pos+i]) {
			case 0x81:
				i++;
				break;
			case 0x83:
				break;
			case 0x85:
				//mms 1.0 file from GSM operator
				buff2[0] = 0;
				i++;
				while (file->Buffer[pos+i]!=0x00) {
					buff2[strlen(buff2)+1] = 0;
					buff2[strlen(buff2)]   = file->Buffer[pos+i];
					i++;
				}
				EncodeUnicode(info->Entries[info->EntriesNum].File.Name,buff2,strlen(buff2));
//				i++;
				break;
			case 0x86:
				while (file->Buffer[pos+i]!=0x00) i++;
				break;
			case 0x89:
				buff[0] = 0;
				sprintf(buff,"%s; type=",buff);
				i++;
				while (file->Buffer[pos+i]!=0x00) {
					buff[strlen(buff)+1] = 0;
					buff[strlen(buff)]   = file->Buffer[pos+i];
					i++;
				}
				i++;
				break;
			case 0x8A:
				buff[0] = 0;
				sprintf(buff,"%s; start=",buff);
				i++;
				while (file->Buffer[pos+i]!=0x00) {
					buff[strlen(buff)+1] = 0;
					buff[strlen(buff)]   = file->Buffer[pos+i];
					i++;
				}
				i++;
				break;
			case 0x8E:
				i++;
				buff[0] = 0;
				dbgprintf("      Name          : ");
				while (file->Buffer[pos+i]!=0x00) {
					buff[strlen(buff)+1] = 0;
					buff[strlen(buff)]   = file->Buffer[pos+i];
					i++;
				}
				dbgprintf("%s\n",buff);
				EncodeUnicode(info->Entries[info->EntriesNum].File.Name,buff,strlen(buff));
				break;
			case 0xAE:
				while (file->Buffer[pos+i]!=0x00) i++;
				break;
			case 0xC0:
				i++;
				i++;
				buff[0] = 0;
				dbgprintf("      SMIL CID      : ");
				while (file->Buffer[pos+i]!=0x00) {
					buff[strlen(buff)+1] = 0;
					buff[strlen(buff)]   = file->Buffer[pos+i];
					i++;
				}
				dbgprintf("%s\n",buff);
				EncodeUnicode(info->Entries[info->EntriesNum].SMIL,buff,strlen(buff));
				break;
			default:
				dbgprintf("unknown3 %02x\n",file->Buffer[pos+i]);
			}
			i++;
		}
		pos+=i;

		//data
		info->Entries[info->EntriesNum].File.Buffer = realloc(info->Entries[info->EntriesNum].File.Buffer,len3);
		info->Entries[info->EntriesNum].File.Used   = len3;
		memcpy(info->Entries[info->EntriesNum].File.Buffer,file->Buffer+pos,len3);

		info->EntriesNum++;
		pos+=len3;
	}
	return ERR_NONE;
}

/* How should editor hadle tabs in this file? Add editor commands here.
 * vim: noexpandtab sw=8 ts=8 sts=8:
 */
