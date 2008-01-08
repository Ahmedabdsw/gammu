#include <gammu.h>

extern volatile bool wasincomingsms;
extern void IncomingSMS(GSM_StateMachine * s, GSM_SMSMessage sms);
extern void IncomingCB(GSM_StateMachine * s, GSM_CBMessage CB);
extern void IncomingUSSD(GSM_StateMachine * s, GSM_USSDMessage ussd);
extern void IncomingUSSD2(GSM_StateMachine * s, GSM_USSDMessage ussd);
extern void DisplayIncomingSMS();
extern void DisplayMultiSMSInfo(GSM_MultiSMSMessage *sms, bool eachsms, bool ems,
				const GSM_Backup * Info);
extern void GetSMSC(int argc, char *argv[]);
extern void GetSMS(int argc, char *argv[]);
extern void DeleteSMS(int argc, char *argv[]);
extern void GetAllSMS(int argc, char *argv[]);
extern void GetEachSMS(int argc, char *argv[]);
extern void GetSMSFolders(int argc, char *argv[]);
extern void GetMMSFolders(int argc, char *argv[]);
extern void SendSaveDisplaySMS(int argc, char *argv[]);
extern void GetEachMMS(int argc, char *argv[]);
extern void GetUSSD(int argc, char *argv[]);
extern void ReadMMSFile(int argc, char *argv[]);
extern void AddSMSFolder(int argc, char *argv[]);
extern void DeleteAllSMS(int argc, char *argv[]);
