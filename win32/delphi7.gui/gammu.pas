unit Gammu;

interface

uses Classes,Forms,SysUtils;

const	GSM_MAX_SMSC_NAME_LENGTH	= 30;
	GSM_MAX_SMS_NAME_LENGTH		= 40;
	GSM_MAX_NUMBER_LENGTH		= 50;

	GSM_MAX_UDH_LENGTH		= 140;
	GSM_MAX_SMS_LENGTH		= 160;
	GSM_MAX_8BIT_SMS_LENGTH		= 140;

	MAX_MULTI_SMS			= 10;

{$MINENUMSIZE 4}

type    GSM_NetworkInfo_State = (
        	GSM_HomeNetwork = 1,     //Home network for used SIM card.
        	GSM_NoNetwork,           //No network available for used SIM card.
        	GSM_RoamingNetwork,      //SIM card uses roaming.
		GSM_RegistrationDenied,
		GSM_NetworkStatusUnknown,
        	GSM_RequestingNetwork);  //Network explicitely requested by user.

	GSM_NetworkInfo = record
		CID	  	: array[1..10] of char;  // CellID (CID)
      		NetworkCode 	: array[1..10] of char;  // GSM network code
		State		: GSM_NetworkInfo_State; // network status
		LAC	  	: array[1..10] of char;  // LAC
		NetworkName	: array[1..30] of char;  // network name showed in some phones on display
	end;
	PGSM_NetworkInfo = ^GSM_NetworkInfo;

type	GSM_Error = (
	        ERR_NONE = 1,
       	 	ERR_DEVICEOPENERROR,		// Error during opening device
		ERR_DEVICELOCKED,
		ERR_DEVICENOTEXIST,
		ERR_DEVICEBUSY,
		ERR_DEVICENOPERMISSION,
		ERR_DEVICENODRIVER,
		ERR_DEVICENOTWORK,
        	ERR_DEVICEDTRRTSERROR,		// Error during setting DTR/RTS in device
        	ERR_DEVICECHANGESPEEDERROR,	// Error during changing speed in device
       		ERR_DEVICEWRITEERROR,		// Error during writing device 
		ERR_DEVICEREADERROR,		// Error during reading device
		ERR_DEVICEPARITYERROR,		// Can't set parity on device
        	ERR_TIMEOUT,			// Command timed out 
        	ERR_FRAMENOTREQUESTED,		// Frame handled, but not requested in this moment //10
        	ERR_UNKNOWNRESPONSE,		// Response not handled by gammu
		ERR_UNKNOWNFRAME,		// Frame not handled by gammu
		ERR_UNKNOWNCONNECTIONTYPESTRING,	// Unknown connection type given by user 
		ERR_UNKNOWNMODELSTRING,		// Unknown model given by user 
		ERR_SOURCENOTAVAILABLE,		// Some functions not compiled in your OS
		ERR_NOTSUPPORTED,		// Not supported by phone
		ERR_EMPTY,			// Empty phonebook entry, ...
		ERR_SECURITYERROR,		// Not allowed
		ERR_INVALIDLOCATION,		// Too high or too low location...
		ERR_NOTIMPLEMENTED,		// Function not implemented //20
		ERR_FULL,			// Memory is full 
		ERR_UNKNOWN,
		ERR_CANTOPENFILE, 		// Error during opening file
		ERR_MOREMEMORY,			// More memory required
		ERR_PERMISSION,			// No permission
		ERR_EMPTYSMSC,			// SMSC number is empty
		ERR_INSIDEPHONEMENU,		// Inside phone menu - can't make something
		ERR_NOTCONNECTED,		// Phone NOT connected - can't make something
		ERR_WORKINPROGRESS,		// Work in progress
      		ERR_PHONEOFF,			// Phone is disabled and connected to charger //30
		ERR_FILENOTSUPPORTED,		// File format not supported by Gammu
		ERR_BUG,                  	// Found bug in implementation or phone //32
		ERR_CANCELED,
		ERR_NEEDANOTHERANSWER,
		ERR_OTHERCONNECTIONREQUIRED,
		ERR_WRONGCRC);

	GSM_DateTime = record
		Timezone : integer;
		Second	 : integer;
		Minute	 : integer;
		Hour	 : integer;
		Day	 : integer;
		Month	 : integer;
		Year	 : integer; //full year (for example,2002)
	end;
	PGSM_DateTime = ^GSM_DateTime;

	GSM_ValidityPeriodFormat = (
		SMS_Validity_NotAvailable = 1, //sms doesn't have validity info
		SMS_Validity_RelativeFormat);  //sms has relative validity (starting from the sending moment). Example, one day, one week, 1 hour

	// Enum defines some the most often used validity lengths for SMS messages
	// for relative validity format.
        GSM_ValidityPeriod = (
		SMS_VALID_1_Hour   = 11,
		SMS_VALID_6_Hours  = 71,
		SMS_VALID_24_Hours = 167,
		SMS_VALID_72_Hours = 169,
		SMS_VALID_1_Week   = 173,
		SMS_VALID_Max_Time = 255);

        GSM_SMSValidity = record
                Format   :      GSM_ValidityPeriodFormat; //type of sms validity
                Relative :      GSM_ValidityPeriod;       //how long sms is valid. example: 1 week or 3 days
        end;

	// Enum defines format of SMS messages. See GSM 03.40 section 9.2.3.9
	GSM_SMSFormat = (
		SMS_FORMAT_Pager = 1,
		SMS_FORMAT_Fax,
		SMS_FORMAT_Email,
		SMS_FORMAT_Text);

	// Structure for SMSC (SMS Center) information.
	GSM_SMSC = record
		Location 	: integer;		                            // Number of the SMSC on SIM
		Name		: array[1..(GSM_MAX_SMSC_NAME_LENGTH+1)*2] of char; // Name of the SMSC
		Number		: array[1..(GSM_MAX_NUMBER_LENGTH+1)*2] of char;    // SMSC phone number
		Validity	: GSM_SMSValidity;	                            // Validity of SMS Messages
		Format		: GSM_SMSFormat;	                            // For of sent SMS messages
		DefaultNumber	: array[1..(GSM_MAX_NUMBER_LENGTH+1)*2] of char;    // Default recipient number
	end;

	// types of UDH (User Data Header)
	GSM_UDH = (
		UDH_NoUDH = 1, //no user header. SMS has only "normal" text
		UDH_ConcatenatedMessages,
		UDH_ConcatenatedMessages16Bit,
		UDH_DisableVoice,
		UDH_DisableFax,
		UDH_DisableEmail,
		UDH_EnableVoice,
		UDH_EnableFax,
		UDH_EnableEmail,
		UDH_VoidSMS,
		UDH_NokiaRingtone,
		UDH_NokiaRingtoneLong,
		UDH_NokiaOperatorLogo,
		UDH_NokiaOperatorLogoLong,
		UDH_NokiaCallerLogo,
		UDH_NokiaWAP,
		UDH_NokiaWAPLong,
		UDH_NokiaCalendarLong,
		UDH_NokiaProfileLong,
		UDH_NokiaPhonebookLong,
		UDH_UserUDH,			// Other user UDH
		UDH_MMSIndicatorLong);

	// Structure for User Data Header.
        GSM_UDHHeader = record
                UDHType         : GSM_UDH;                              //Type
                Length          : integer;                              //Length
                Text            : array[1..GSM_MAX_UDH_LENGTH] of char; // Text
 		ID8Bit		: integer;
 		ID16Bit		: integer;
		PartNumber	: integer;
		AllParts	: integer;
        end;

	GSM_SMS_State = (
		GSM_Sent = 1, //for outbox sms - sent
		GSM_UnSent,   //for outbox sms - unsent
		GSM_Read,     //for inbox sms - read
		GSM_UnRead);  //for inbox sms - unread

	GSM_Coding_Type = (
		GSM_Coding_Unicode = 1, // 70 Unicode chars
		GSM_Coding_Default,     // 160 chars from Default GSM alphabet
		GSM_Coding_8bit);       // 140 8 bit chars

	GSM_SMSMessageType = (
		SMS_Deliver = 1, 	// when we save SMS in Inbox
		SMS_Status_Report,	// Delivery Report received by phone
		SMS_Submit);		// when we send SMS or save it in Outbox

	GSM_SMSMessage = record
                ReplaceMessage   : char;		// 0, when don't use this feature. 1 - 7 set SMS ID
		RejectDuplicates : ByteBool;		// true, if set this flag. Normally false
		UDH		 : GSM_UDHHeader;       // User Data Header
		Number		 : array[1..(GSM_MAX_NUMBER_LENGTH+1)*2]   of char; // Sender or recipient number
		SMSC		 : GSM_SMSC;		// SMSC (SMS Center)
		Memory		 : integer;		// For saved SMS: where exactly it's saved (SIM/phone)
		Location	 : integer;             // For saved SMS: location of SMS in memory.
		Folder		 : integer;             // For saved SMS: number of folder, where SMS is saved
		InboxFolder	 : ByteBool;		// For saved SMS: whether SMS is really in Inbox
		Length		 : integer;		// Length of the SMS message.
                                                        // for 8 bit sms number of 8 bit chars
		State		 : GSM_SMS_State;	// Read, UnRead, etc.
		Name		 : array[1..(GSM_MAX_SMS_NAME_LENGTH+1)*2] of char; // Name in Nokia 6210/7110, etc. Ignored in other
		Text		 : array[1..(GSM_MAX_SMS_LENGTH+1)*2] 	   of char; // Text for SMS
		PDU		 : GSM_SMSMessageType;	// Type of message
		Coding		 : GSM_Coding_Type;	// Type of coding
		DateTime	 : GSM_DateTime;	// Date and time, when SMS was saved or sent
		SMSCTime	 : GSM_DateTime;	// Date of SMSC response in DeliveryReport messages.
		DeliveryStatus	 : char;		// In delivery reports: status
		ReplyViaSameSMSC : ByteBool;		// Indicates whether "Reply via same center" is set
		SMSClass	 : shortint;		// SMS class. Normally -1.
                MessageReference : char;		// SMS Reference Number in SMS_Submit. 0, when don't used
	end;
	PGSM_SMSMessage = ^GSM_SMSMessage;

	GSM_MultiSMSMessage = record
		Number		 : char;  //how many sms we have in collection now
		SMS		 : array[1..MAX_MULTI_SMS] of GSM_SMSMessage;
	end;
	PGSM_MultiSMSMessage = ^GSM_MultiSMSMessage;

	GSM_SMSMemoryStatus = record
		SIMUnRead	 : integer; //Number of unread messages on SIM.
		SIMUsed		 : integer; //Number of all saved messages (including unread) on SIM.
		SIMSize		 : integer; //Number of all possible messages on SIM.
		PhoneUnRead	 : integer;
		PhoneUsed	 : integer;
		PhoneSize	 : integer;
		TemplatesUsed	 : integer;
	end;
	PGSM_SMSMemoryStatus = ^GSM_SMSMemoryStatus;

        GSM_SecurityCodeType = (
		SEC_UNKNOWN,
                SEC_SecurityCode, 		// Security code.
                SEC_Pin,                 	// PIN.
                SEC_Pin2,                	// PIN 2.
                SEC_Puk,                 	// PUK.
                SEC_Puk2,			// PUK 2.
                SEC_None);			// Code not needed.

	GSM_SecurityCode = record
		Code	        : array[1..10] of char; //code (without unicode !)
		CodeType	: GSM_SecurityCodeType; //type of code
	end;
	PGSM_SecurityCode = ^GSM_SecurityCode;

type
        //callback, which is called, when phone is connected or disconnected
        PhoneCallBackProc          = procedure(x:integer;ID:integer;connected:ByteBool);stdcall;
        PPhoneCallBackProc         = ^PhoneCallBackProc;
        //this definition is used, when call back is defined under Class
        PhoneCallBackProcClass     = procedure(ID:integer;connected:ByteBool);stdcall;
        PPhoneCallBackProcClass    = ^PhoneCallBackProcClass;

        //called, when phone needs PIN, PUK, etc.
        SecurityCallBackProc       = procedure(x:integer;ID:integer;SecurityState:GSM_SecurityCodeType);stdcall;
        PSecurityCallBackProc      = ^SecurityCallBackProc;
        //this definition is used, when call back is defined under Class
        SecurityCallBackProcClass  = procedure(ID:integer;SecurityState:GSM_SecurityCodeType);stdcall;
        PSecurityCallBackProcClass = ^SecurityCallBackProcClass;

        //called, when there are ANY SMS on sim
        SMSCallBackProc            = procedure(x:integer;ID:integer);stdcall;
        PSMSCallBackProc           = ^SMSCallBackProc;
        //this definition is used, when call back is defined under Class
        SMSCallBackProcClass       = procedure(ID:integer);stdcall;
        PSMSCallBackProcClass      = ^SMSCallBackProcClass;

function GSM_StartConnection	        (Phone : Pinteger; Device: PChar;Connection: PChar; Model : PChar; LogFile: PChar; LogFileType: PChar; CallBack1 : PPhoneCallBackProc; CallBack2: PSecurityCallBackProc; CallBack3: PSMSCallBackProc): GSM_Error; stdcall; external 'gammu.dll' name 'mystartconnection';
function GSM_StartConnectionClass	(Phone : Pinteger; Device: PChar;Connection: PChar; Model : PChar; LogFile: PChar; LogFileType: PChar; CallBack1 : PPhoneCallBackProcClass; CallBack2: PSecurityCallBackProcClass; CallBack3: PSMSCallBackProcClass): GSM_Error; stdcall; external 'gammu.dll' name 'mystartconnection';
function GSM_EndConnection	        (Phone : integer): GSM_Error; stdcall; external 'gammu.dll' name 'myendconnection';
function GSM_GetNetworkInfo	        (Phone : integer; NetworkInfo : PGSM_NetworkInfo): GSM_Error; stdcall; external 'gammu.dll' name 'mygetnetworkinfo';
function GSM_GetSMSStatus	        (Phone : integer; status : PGSM_SMSMemoryStatus): GSM_Error; stdcall; external 'gammu.dll' name 'mygetsmsstatus';
function GSM_GetNextSMSMessage	        (Phone : integer; SMS : PGSM_MultiSMSMessage;start : ByteBool): GSM_Error; stdcall; external 'gammu.dll' name 'mygetnextsmsmessage';
function GSM_DeleteSMSMessage	        (Phone : integer; SMS : PGSM_SMSMessage): GSM_Error; stdcall; external 'gammu.dll' name 'mydeletesmsmessage';
function GSM_SendSMSMessage	        (Phone : integer; SMS : PGSM_SMSMessage;timeout:integer): GSM_Error; stdcall; external 'gammu.dll' name 'mysendsmsmessage';
function GSM_AddSMSMessage	        (Phone : integer; SMS : PGSM_SMSMessage): GSM_Error; stdcall; external 'gammu.dll' name 'myaddsmsmessage';
function GSM_EnterSecurityCode	        (Phone : integer; Code : PGSM_SecurityCode): GSM_Error; stdcall	; external 'gammu.dll' name 'myentersecuritycode';
function GSM_GetIMEI	                (Phone : integer; IMEI:PAnsiString): GSM_Error; stdcall; external 'gammu.dll' name 'mygetimei';
function GSM_GetDCT4SimlockNetwork	(Phone : integer; Info:PAnsiString): GSM_Error; stdcall; external 'gammu.dll' name 'mygetdct4simlocknetwork';
function GSM_GetDCT4SecurityCode	(Phone : integer; Code:PAnsiString): GSM_Error; stdcall; external 'gammu.dll' name 'mygetdct4securitycode';
function GSM_GetManufacturer	        (Phone : integer; IMEI:PAnsiString): GSM_Error; stdcall; external 'gammu.dll' name 'mygetmanufacturer';
function GSM_GetModel	                (Phone : integer; Model:PAnsiString): GSM_Error; stdcall; external 'gammu.dll' name 'mygetmodel';
function GSM_GetModelName	        (Phone : integer; Model:PAnsiString): GSM_Error; stdcall; external 'gammu.dll' name 'mygetmodelname';
function GSM_GetFirmwareVersion         (Phone : integer; Version: PDouble): GSM_Error; stdcall; external 'gammu.dll' name 'mygetfirmwareversion';
function GSM_Reset                      (Phone : integer; Hard: ByteBool): GSM_Error; stdcall; external 'gammu.dll' name 'myreset';
function GSM_SMSCounter			(MessageLength:Integer;MessageBuffer:PAnsiString;UDH:GSM_UDH;Coding:GSM_Coding_Type;SMSNum:PInteger;CharsLeft:PInteger): GSM_Error; stdcall; external 'gammu.dll' name 'mysmscounter';
function GSM_MakeMultiPartSMS           (MessageBuffer:PAnsiString;MessageLength:Integer;UDHType:GSM_UDH;Coding:GSM_Coding_Type;MyClass:Integer;ReplaceMessage:ShortInt;SMS:PGSM_MultiSMSMessage): GSM_Error; stdcall; external 'gammu.dll' name 'mymakemultipartsms';
function GSM_GetStructureSize		(i: integer): integer; stdcall; external 'gammu.dll' name 'mygetstructuresize';
procedure GSM_GetNetworkName	        (NetworkCode: PAnsiString; NetworkName: PAnsiString); stdcall; external 'gammu.dll' name 'mygetnetworkname';
procedure GSM_GetGammuVersion		(Version: PAnsiString); stdcall; external 'gammu.dll' name 'mygetgammuversion';

function GetGammuUnicodeString		(Source : AnsiString):string;

type
  TGammuGSMDevice = class(TObject)
  private
    fDeviceID                 : integer;
    fDevice                   : PChar;
    fConnection               : PChar;
    fUsed                     : Boolean;
    fConnected		      : Boolean;
    fPhoneCallBackPointer     : PPhoneCallBackProcClass;
    fSecurityCallBackPointer  : PSecurityCallBackProcClass;
    fSMSCallBackPointer       : PSMSCallBackProcClass;
  public
    function 			StartConnection(DeviceString:String;ConnectionString:String;PhoneCallBack:PPhoneCallBackProcClass;SecurityCallBack:PSecurityCallBackProcClass;SMSCallBack:PSMSCallBackProcClass;LogFile:String;LogFormat:String):GSM_Error;
    function 			EndConnection:GSM_Error;
    constructor 		Create;

    property Device           : PChar   read fDevice;
    property DeviceID         : integer read fDeviceID;
    property Connection       : PChar   read fConnection;
    property Used             : Boolean read fUsed write fUsed;
    property Connected        : Boolean read fConnected write fConnected;
  end;

implementation //here all private variables, stuff, etc.

constructor TGammuGSMDevice.Create;
begin
  Connected := False;
  Used := False;
  inherited;
end;

function TGammuGSMDevice.EndConnection():GSM_Error;
begin
  EndConnection:=GSM_EndConnection(fDeviceID);
end;

function TGammuGSMDevice.StartConnection(DeviceString:String;ConnectionString:String;PhoneCallBack:PPhoneCallBackProcClass;SecurityCallBack:PSecurityCallBackProcClass;SMSCallBack:PSMSCallBackProcClass;LogFile:String;LogFormat:String):GSM_Error;
begin
  GetMem(fDevice,Length(DeviceString) + 1);
  StrCopy(fDevice,PChar(DeviceString));
  GetMem(fConnection,50);
  StrCopy(fConnection,PChar(ConnectionString));

  fPhoneCallBackPointer    := PhoneCallBack;
  fSecurityCallBackPointer := SecurityCallBack;
  fSMSCallBackPointer      := SMSCallback;

  StartConnection:=GSM_StartConnection(@fDeviceID,fDevice,fConnection,'',PChar(LogFile),PChar(LogFormat),@fPhoneCallBackPointer,@fSecurityCallBackPointer,@fSMSCallBackPointer);
end;

function GetGammuUnicodeString(Source:AnsiString):string;
var
        i : integer;
        Dest : array[1..255] of Word;
begin
        i := 1;
        while ((Source[i*2-1]<>chr(0)) or (Source[i*2]<>chr(0))) do
        begin
                Dest[i] := ord(Source[i*2])*256+ord(Source[i*2-1]);
                i := i + 1;
        end;
        Dest[i] := 0;
        GetGammuUnicodeString := WideCharLenToString(@Dest,i-1);
end;

begin
  if (GSM_GetStructureSize(0) <> sizeof(gsm_smsmessage))  then application.MessageBox('gsm_smsmessage','',0);
  if (GSM_GetStructureSize(1) <> sizeof(gsm_smsc))        then application.MessageBox('gsm_smsc','',0);
  if (GSM_GetStructureSize(2) <> sizeof(gsm_sms_state))   then application.MessageBox('gsm_sms_state','',0);
  if (GSM_GetStructureSize(3) <> sizeof(gsm_UDHHeader))   then application.MessageBox('gsm_udhheader','',0);
  if (GSM_GetStructureSize(4) <> sizeof(ByteBool))        then application.MessageBox('bool','',0);
  if (GSM_GetStructureSize(5) <> sizeof(gsm_datetime))    then application.MessageBox('gsm_datetime','',0);
  if (GSM_GetStructureSize(6) <> sizeof(integer))         then application.MessageBox('int','',0);
  if (GSM_GetStructureSize(7) <> sizeof(gsm_networkinfo)) then application.MessageBox('gsm_networinfo','',0);
  if (GSM_GetStructureSize(8) <> sizeof(gsm_UDH))   then application.MessageBox('gsm_udhtype','',0);
end.
