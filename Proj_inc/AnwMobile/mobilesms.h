#if !defined(AFX_ANWTEST_H__BE2FA6BC_B648_45AF_87EB_1FE5E7A382F8__INCLUDED_)
#define AFX_ANWTEST_H__BE2FA6BC_B648_45AF_87EB_1FE5E7A382F8__INCLUDED_


/*#define INBOX_FOLDER		0x0001
#define OUTBOX_FOLDER		0x0002
#define BACKUP_FOLDER		0x0004
#define OTHER1_FOLDER		0x0008
#define OTHER2_FOLDER		0x0010*/





typedef struct _SMS_Tal_Num
{
	short  SIMSMSTotalNum;
	short  SIMSMStUsedNum;			// -1 �N�� N/A : Driver �L�k���o
	short  MESMSTotalNum;
	short  MESMSUsedNum ;			// -1 �N�� N/A : Driver �L�k���o
	short  SIMSMSFolderTypeFlag;	// EX: 0x0000 = Send  ; 0x0001 = Receive ;
	short  MESMSFolderTypeFlag;		// EX: 0x0000 = Send  ; 0x0001 = Receive ;
	bool   bSIMSMSReadOnly;
	bool   bMESMSReadOnly;
} SMS_Tal_Num;

#define MAX_TP_UD	1000
typedef struct _SMS_Data_Strc
{
	TCHAR SCA[16];			// �u�����A�Ȥ��߸��X(SMSC��})
	TCHAR TPA[40];			// �ؼи��X�Φ^�_���X(TP-DA��TP-RA)
	char TP_PID;			// �Τ��T��w����(TP-PID)
	char TP_DCS;			// �Τ��T�s�X�覡(TP-DCS)
	TCHAR TP_SCTS[40];		// �A�Ȯɶ��W�r��(TP_SCTS), �����ɥΨ�
	TCHAR TP_UD[MAX_TP_UD+2];		// ��l�Τ��T(�s�X�e�θѽX�᪺TP-UD)
	int  index;			// �u�����Ǹ��A�bŪ���ɥΨ�
	int  whichFolder;		// �s�bfolder
	int  memType;			// �x�smem
} SMS_Data_Strc;




int WINAPI OpenSMS(int MobileCompany , char *MobileID , char *ConnectMode , char *ConnectPortName ,char *IMEI, int (*ConnectStatusCallBack)(int State) );
int WINAPI GetMobileSMSInfo(SMS_Tal_Num *SMS_Tal_Num);

int WINAPI GetSMSStartData(int MemType, int NeedCount, SMS_Data_Strc *SMS_Data_Strc, int &RealCount); 
int WINAPI GetSMSOneData(int MemType, int FolderType, SMS_Data_Strc *SMS_Data_Strc, bool bStart);
int WINAPI GetSMSNextData (int MemType, int NeedCount, SMS_Data_Strc *SMS_Data_Strc, int &RealCount);
// Start : SyncDriver �q�Ĥ@���}�l read , �� keep index �� 
// Next : SyncDriver �q��NeedCount+1���}�l read , �� keep index ��
// If RealCount < NeedCount , �N��Ū���������

int WINAPI SendSMSData(int MemType, int SMSfolderType, SMS_Data_Strc  * SMS_Data_Strc);
int WINAPI PutSMSData(int MemType, int SMSfolderType, SMS_Data_Strc  * SMS_Data_Strc);
int WINAPI DeleteSMSData(int MemType, int SMSfolderType, int Index);
// Note : �Y����� Support delete , �� SyncDriver �t�dcontrol , ���h call delete // index �� mobile �W�� index ;
int WINAPI DeleteAllSMSData(int MemType, int SMSfolderType);

int WINAPI CloseSMS(void);
int WINAPI InitSMS(void);
int WINAPI TerminateSMS(void);


// Export Function Define
typedef int (WINAPI* anwOpenSMS)(int MobileCompany , TCHAR *MobileID , TCHAR *ConnectMode , TCHAR *ConnectPortName ,TCHAR *IMEI, int (*ConnectStatusCallBack)(int State) );
typedef void (WINAPI* anwCloseSMS)(void);
typedef int (WINAPI* anwGetMobileSMSInfo)(SMS_Tal_Num *SMS_Tal_Num);
typedef int (WINAPI* anwGetMobileAllSMS)(SMS_Tal_Num *SMS_Tal_Num);
typedef int (WINAPI* anwGetSMSStartData)(int MemType, int NeedCount, SMS_Data_Strc *SMS_Data_Strc, int &RealCount);
typedef int (WINAPI* anwGetSMSNextData)(int MemType, int NeedCount, SMS_Data_Strc *SMS_Data_Strc, int &RealCount);
typedef int (WINAPI* anwSendSMSData)(int MemType, int SMSfolderType, SMS_Data_Strc  * SMS_Data_Strc);
typedef int (WINAPI* anwPutSMSData)(int MemType, int SMSfolderType, SMS_Data_Strc  * SMS_Data_Strc);
typedef int (WINAPI* anwDeleteSMSData)(int MemType, int SMSfolderType, int Index);
typedef int (WINAPI* anwDeleteAllSMSData)(int MemType, int SMSfolderType);

typedef int (WINAPI* anwGetSMSOneData)(int MemType, int FolderType, SMS_Data_Strc *SMS_Data_Strc, bool bStart);

typedef int (WINAPI* anwInitSMS)(void);
typedef int (WINAPI* anwTerminateSMS)(void);

extern anwOpenSMS				ANWOpenSMS;
extern anwCloseSMS				ANWCloseSMS;
extern anwGetMobileSMSInfo		ANWGetMobileSMSInfo;
extern anwGetMobileAllSMS		ANWGetMobileAllSMS;
extern anwGetSMSStartData		ANWGetSMSStartData;
extern anwGetSMSNextData		ANWGetSMSNextData;
extern anwSendSMSData			ANWSendSMSData;
extern anwPutSMSData			ANWPutSMSData;
extern anwDeleteSMSData			ANWDeleteSMSData;
extern anwDeleteAllSMSData		ANWDeleteAllSMSData;
extern anwInitSMS 				ANWInitSMSfn;
extern anwTerminateSMS 			ANWTerminateSMSfn;

#endif