/* (c) 2002-2006 by Marcin Wiacek and Michal Cihar */
/* FM stuff by Walek */

#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <locale.h>
#include <signal.h>
#include <ctype.h>
#include <wchar.h>
#include <unistd.h>

#include <gammu.h>
#include <gammu-config.h>
#include "gammu.h"
#include "common.h"
#include "memory.h"
#include "message.h"
#include "search.h"
#include "nokia.h"
#include "backup.h"
#include "files.h"
#include "calendar.h"
#include "misc.h"

#include "smsd/smsdcore.h"
#include "../common/misc/locales.h"
#ifdef DEBUG
#  include "sniff.h"
#endif
#ifdef GSM_ENABLE_NOKIA_DCT3
#  include "depend/nokia/dct3.h"
#  include "depend/nokia/dct3trac/wmx.h"
#endif
#ifdef GSM_ENABLE_NOKIA_DCT4
#  include "depend/nokia/dct4.h"
#endif
#ifdef GSM_ENABLE_ATGEN
#  include "depend/siemens/dsiemens.h"
#endif

#ifdef HAVE_SYS_IOCTL_H
#  include <sys/ioctl.h>
#endif

#define ALL_MEMORY_TYPES "DC|MC|RC|ON|VM|SM|ME|MT|FD|SL"

#ifdef DEBUG
static void MakeConvertTable(int argc UNUSED, char *argv[])
{
	unsigned char InputBuffer[10000], Buffer[10000];
	FILE *file;
	int size, i, j = 0;

	file = fopen(argv[2], "rb");
	if (file == NULL)
		Print_Error(ERR_CANTOPENFILE);
	size = fread(InputBuffer, 1, 10000 - 1, file);
	fclose(file);
	InputBuffer[size] = 0;
	InputBuffer[size + 1] = 0;

	ReadUnicodeFile(Buffer, InputBuffer);

	for (i = 0; i < ((int)UnicodeLength(Buffer)); i++) {
		j++;
		if (j == 100) {
			printf("\"\\\n\"");
			j = 0;
		}
		printf("\\x%02x\\x%02x", Buffer[i * 2], Buffer[i * 2 + 1]);
	}
	printf("\\x00\\x00");
}
#endif

static void ListNetworks(int argc, char *argv[])
{
	extern unsigned char *GSM_Networks[];
	extern unsigned char *GSM_Countries[];
	int i = 0;
	char country[4] = "";

	if (argc > 2) {
		while (GSM_Countries[i * 2] != NULL) {
			if (strncmp
			    (GSM_Countries[i * 2 + 1], argv[2],
			     strlen(argv[2])) == 0
			    || strncmp(GSM_Countries[i * 2], argv[2],
				       strlen(argv[2])) == 0) {
				strcpy(country, GSM_Countries[i * 2]);
				printf(_("Networks for %s:"),
				       GSM_Countries[i * 2 + 1]);
				printf("\n\n");
				break;
			}
			i++;
		}
		if (!*country) {
			printf(_("Unknown country name: %s."), argv[2]);
			printf("\n");
			exit(-1);
		}
	}
	printf("%-10s %s\n", _("Network"), _("Name"));
	i = 0;
	while (GSM_Networks[i * 2] != NULL) {
		if (argc > 2) {
			if (!strncmp
			    (GSM_Networks[i * 2], country, strlen(country))) {
				printf("%-10s %s\n", GSM_Networks[i * 2],
				       GSM_Networks[i * 2 + 1]);
			}
		} else {
			printf("%-10s %s\n", GSM_Networks[i * 2],
			       GSM_Networks[i * 2 + 1]);
		}
		i++;
	}
}

static void PrintVersion()
{
	printf(_("[Gammu version %s built %s on %s using %s]"),
	       VERSION, __TIME__, __DATE__, GetCompiler());
	printf("\n\n");
}

static void Features(int argc UNUSED, char *argv[]UNUSED)
{
	PrintVersion();

	printf("%s\n", _("Compiled in features:"));

	printf(" * %s\n", _("Protocols"));
#ifdef GSM_ENABLE_MBUS2
	printf("  - %s\n", "MBUS2");
#endif
#ifdef GSM_ENABLE_FBUS2
	printf("  - %s\n", "FBUS2");
#endif
#ifdef GSM_ENABLE_FBUS2DLR3
	printf("  - %s\n", "FBUS2DLR3");
#endif
#ifdef GSM_ENABLE_FBUS2PL2303
	printf("  - %s\n", "FBUS2PL2303");
#endif
#ifdef GSM_ENABLE_FBUS2BLUE
	printf("  - %s\n", "FBUS2BLUE");
#endif
#ifdef GSM_ENABLE_FBUS2IRDA
	printf("  - %s\n", "FBUS2IRDA");
#endif
#ifdef GSM_ENABLE_DKU2PHONET
	printf("  - %s\n", "DKU2PHONET");
#endif
#ifdef GSM_ENABLE_DKU2AT
	printf("  - %s\n", "DKU2AT");
#endif
#ifdef GSM_ENABLE_DKU5FBUS2
	printf("  - %s\n", "DKU5FBUS2");
#endif
#ifdef GSM_ENABLE_PHONETBLUE
	printf("  - %s\n", "PHONETBLUE");
#endif
#ifdef GSM_ENABLE_AT
	printf("  - %s\n", "AT");
#endif
#ifdef GSM_ENABLE_ALCABUS
	printf("  - %s\n", "ALCABUS");
#endif
#ifdef GSM_ENABLE_IRDAPHONET
	printf("  - %s\n", "IRDAPHONET");
#endif
#ifdef GSM_ENABLE_IRDAAT
	printf("  - %s\n", "IRDAAT");
#endif
#ifdef GSM_ENABLE_IRDAOBEX
	printf("  - %s\n", "IRDAOBEX");
#endif
#ifdef GSM_ENABLE_IRDAGNAPBUS
	printf("  - %s\n", "IRDAGNAPBUS");
#endif
#ifdef GSM_ENABLE_BLUEGNAPBUS
	printf("  - %s\n", "BLUEGNAPBUS");
#endif
#ifdef GSM_ENABLE_BLUEFBUS2
	printf("  - %s\n", "BLUEFBUS2");
#endif
#ifdef GSM_ENABLE_BLUEPHONET
	printf("  - %s\n", "BLUEPHONET");
#endif
#ifdef GSM_ENABLE_BLUEAT
	printf("  - %s\n", "BLUEAT");
#endif
#ifdef GSM_ENABLE_BLUEOBEX
	printf("  - %s\n", "BLUEOBEX");
#endif

	printf(" * %s\n", _("Phones"));
#ifdef GSM_ENABLE_NOKIA650
	printf("  - %s\n", "NOKIA650");
#endif
#ifdef GSM_ENABLE_NOKIA3320
	printf("  - %s\n", "NOKIA3320");
#endif
#ifdef GSM_ENABLE_NOKIA6110
	printf("  - %s\n", "NOKIA6110");
#endif
#ifdef GSM_ENABLE_NOKIA7110
	printf("  - %s\n", "NOKIA7110");
#endif
#ifdef GSM_ENABLE_NOKIA9210
	printf("  - %s\n", "NOKIA9210");
#endif
#ifdef GSM_ENABLE_NOKIA6510
	printf("  - %s\n", "NOKIA6510");
#endif
#ifdef GSM_ENABLE_NOKIA3650
	printf("  - %s\n", "NOKIA3650");
#endif
#ifdef GSM_ENABLE_NOKIA_DCT3
	printf("  - %s\n", "DCT3");
#endif
#ifdef GSM_ENABLE_NOKIA_DCT4
	printf("  - %s\n", "DCT4");
#endif
#ifdef GSM_ENABLE_ATGEN
	printf("  - %s\n", "ATGEN");
#endif
#ifdef GSM_ENABLE_ALCATEL
	printf("  - %s\n", "ALCATEL");
#endif
#ifdef GSM_ENABLE_SONYERICSSON
	printf("  - %s\n", "SONYERICSSON");
#endif
#ifdef GSM_ENABLE_OBEXGEN
	printf("  - %s\n", "OBEXGEN");
#endif
#ifdef GSM_ENABLE_GNAPGEN
	printf("  - %s\n", "GNAPGEN");
#endif

	printf(" * %s\n", _("Miscellaneous"));
#ifdef GSM_ENABLE_CELLBROADCAST
	printf("  - %s\n", "CELLBROADCAST");
#endif
#ifdef GSM_ENABLE_BACKUP
	printf("  - %s\n", "BACKUP");
#endif
#ifdef GETTEXTLIBS_FOUND
	printf("  - %s\n", "GETTEXT");
#endif
#ifdef ICONV_FOUND
	printf("  - %s\n", "ICONV");
#endif
#ifdef HAVE_MYSQL_MYSQL_H
	printf("  - %s\n", "MYSQL");
#endif
#ifdef HAVE_POSTGRESQL_LIBPQ_FE_H
	printf("  - %s\n", "POSTGRESQL");
#endif
}

static void Version(int argc UNUSED, char *argv[]UNUSED)
{
	PrintVersion();

	printf("%s\n",
	       ("This is free software.  You may redistribute copies of it under the terms of"));
	printf("%s\n",
	       ("the GNU General Public License <http://www.gnu.org/licenses/gpl.html>."));
	printf("%s\n",
	       _("There is NO WARRANTY, to the extent permitted by law."));
	printf("\n\n");
}

#ifdef DEBUG
/**
 * Function for testing purposes.
 */
static void Foo(int argc UNUSED, char *argv[]UNUSED)
{
}
#endif

int ProcessParameters(char start, int argc, char *argv[]);

/**
 * Reads commands from file (argv[2]) or stdin and executes them
 * sequentially as if they were given on the command line. Also allows
 * recursive calling (nested batches in the batch files).
 */
static void RunBatch(int argc, char *argv[])
{
	FILE *bf;

	/**
	 * @todo Allocate memory dynamically.
	 */
	char ln[2000];
	size_t i;
	int j, c = 0, argsc;
	char *argsv[20];
	bool origbatch;
	char *name;
	char std_name[] = N_("standard input");

	if (argc == 2 || strcmp(argv[2], "-") == 0) {
		bf = stdin;
		name = gettext(std_name);
	} else {
		bf = fopen(argv[2], "r");
		name = argv[2];
	}

	if (bf == NULL) {
		printf(_("Batch file could not be opened: %s\n"), argv[2]);
		return;		/* not exit(), so that any parent batch can continue */
	}

	argsv[0] = argv[0];
	origbatch = batch;
	batch = true;
	while (!feof(bf)) {
		ln[0] = 0;
		fgets(ln, sizeof(ln) - 2, bf);
		if (ln[strlen(ln) - 2] == 0x0D) {
			/* reduce CRLF to LF so we have the same EOL on Windows and Linux */
			ln[strlen(ln) - 2] = 0x0A;
			ln[strlen(ln) - 1] = 0;
		}

		if (strlen(ln) < 1 || ln[0] == '#') {
			/* line is empty and is not a comment */
			continue;
		}
			
		/* split words into strings in the array argsv */
		i = 0;
		j = 0;
		argsc = 0;
		while (i < strlen(ln)) {
			if (ln[i] == ' ' || ln[i] == 0x0A) {
				argsc++;
				argsv[argsc] = malloc(i - j + 1);
				strncpy(argsv[argsc], ln + j, i - j);
				argsv[argsc][i - j] = 0;
				j = i + 1;
			}
			i++;
		}
		if (argsc > 0) {
			/* we have some usable command and parameters, send them into standard processing */
			printf ("----------------------------------------------------------------------------\n");
			printf(_("Executing batch \"%s\" - command %i: %s"), name, ++c, ln);
			/**
			 * @todo Handle return value from ProcessParameters.
			 */
			ProcessParameters(0, argsc + 1, argsv);
			for (j = 1; j <= argsc; j++) {
				free(argsv[j]);
			}
		}
	}
	if (!origbatch) {
		/* only close the batch if we are not in a nested batch */
		batch = false;
		if (batchConn) {
			GSM_Terminate();
		}
	}
	fclose(bf);
}

void Help(int argc, char *argv[]);

static GSM_Parameters Parameters[] = {
/* *INDENT-OFF* */
#ifdef DEBUG
	{"foo",			0, 0, Foo,			{0},				""},
#endif
	{"help",			0, 1, Help,			{H_Gammu,0},			""},
	{"identify",			0, 0, Identify,			{H_Info,0},			""},
	{"version",			0, 0, Version,			{H_Gammu,0},			""},
	{"features",			0, 0, Features,			{H_Gammu,0},			""},
	{"getdisplaystatus",		0, 0, GetDisplayStatus,		{H_Info,0},			""},
	{"monitor",			0, 1, Monitor,			{H_Info,H_Network,H_Call,0},	"[times]"},
	{"setautonetworklogin",	0, 0, SetAutoNetworkLogin,	{H_Network,0},			""},
	{"listnetworks",		0, 1, ListNetworks,		{H_Network,0},			"[country]"},
	{"getgprspoint",		1, 2, GetGPRSPoint,		{H_Network,0},			"start [stop]"},
	{"getfilesystemstatus",	0, 0, GetFileSystemStatus,	{H_Filesystem,0},		""},
	{"getfilesystem",		0, 1, GetFileSystem,		{H_Filesystem,0},		"[-flatall|-flat]"},
	{"getfilefolder",		1,40, GetFileFolder,		{H_Filesystem,0},		"ID1, ID2, ..."},
	{"addfolder",			2, 2, AddFolder,		{H_Filesystem,0},		"parentfolderID name"},
	{"deletefolder",		1, 1, DeleteFolder,		{H_Filesystem,0},		"name"},
	{"getfolderlisting",		1, 1, GetFolderListing,		{H_Filesystem,0},		"folderID"},
	{"getrootfolders",		0, 0, GetRootFolders,		{H_Filesystem,0},		""},
	{"setfileattrib",		1, 5, SetFileAttrib,		{H_Filesystem,0},		"folderID [-system] [-readonly] [-hidden] [-protected]"},
	{"getfiles",			1,40, GetFiles,			{H_Filesystem,0},		"ID1, ID2, ..."},
	{"addfile",			2, 6, AddSendFile,		{H_Filesystem,0},		"folderID name [-type JAR|BMP|PNG|GIF|JPG|MIDI|WBMP|AMR|3GP|NRT][-readonly][-protected][-system][-hidden][-newtime]"},
	{"sendfile",			1, 1, AddSendFile,		{H_Filesystem,0},		"name"},
	{"deletefiles",		1,20, DeleteFiles,		{H_Filesystem,0},		"fileID"},
#if defined(GSM_ENABLE_NOKIA_DCT3) || defined(GSM_ENABLE_NOKIA_DCT4)
	{"nokiaaddplaylists",		0, 0, NokiaAddPlayLists,	{H_Filesystem,H_Nokia,0},	""},
	{"nokiaaddfile",		2, 5, NokiaAddFile,		{H_Filesystem,H_Nokia,0},	"Application|Game file [-readonly][-overwrite]"},
	{"nokiaaddfile",		2, 5, NokiaAddFile,		{H_Filesystem,H_Nokia,0},	"Gallery|Gallery2|Camera|Tones|Tones2|Records|Video|Playlist|MemoryCard file [-name name][-protected][-readonly][-system][-hidden][-newtime]"},
	{"playsavedringtone",		1, 1, DCT4PlaySavedRingtone, 	{H_Ringtone,0},			"number"},
#endif
	{"playringtone",		1, 1, PlayRingtone, 		{H_Ringtone,0},			"file"},
	{"getdatetime",		0, 0, GetDateTime,		{H_DateTime,0},			""},
	{"setdatetime",		0, 2, SetDateTime,		{H_DateTime,0},			"[HH:MM[:SS]] [YYYY/MM/DD]"},
	{"getalarm",			0, 1, GetAlarm,			{H_DateTime,0},			"[start]"},
	{"setalarm",			2, 2, SetAlarm,			{H_DateTime,0},			"hour minute"},
	{"resetphonesettings",	1, 1, ResetPhoneSettings,	{H_Settings,0},			"PHONE|DEV|UIF|ALL|FACTORY"},
	{"getmemory",			2, 4, GetMemory,		{H_Memory,0},			ALL_MEMORY_TYPES " start [stop [-nonempty]]"},
	{"deletememory",		2, 3, DeleteMemory,		{H_Memory,0},			ALL_MEMORY_TYPES " start [stop]"},
	{"getallmemory",		1, 2, GetAllMemory,		{H_Memory,0},			ALL_MEMORY_TYPES},
	{"searchmemory",		1, 1, SearchMemory,		{H_Memory,0},			"text"},
	{"listmemorycategory",	1, 1, ListMemoryCategory,	{H_Memory, H_Category,0},	"text|number"},
	{"getfmstation",		1, 2, GetFMStation,		{H_FM,0},			"start [stop]"},
	{"getsmsc",			0, 2, GetSMSC,			{H_SMS,0},			"[start [stop]]"},
	{"getsms",			2, 3, GetSMS,			{H_SMS,0},			"folder start [stop]"},
	{"deletesms",			2, 3, DeleteSMS,		{H_SMS,0},			"folder start [stop]"},
	{"deleteallsms",		1, 1, DeleteAllSMS,		{H_SMS,0},			"folder"},
	{"getsmsfolders",		0, 0, GetSMSFolders,		{H_SMS,0},			""},
	{"getallsms",			0, 1, GetAllSMS,		{H_SMS,0},			"-pbk"},
	{"geteachsms",		0, 1, GetEachSMS,		{H_SMS,0},			"-pbk"},

#define SMS_TEXT_OPTIONS	"[-inputunicode][-16bit][-flash][-len len][-autolen len][-unicode][-enablevoice][-disablevoice][-enablefax][-disablefax][-enableemail][-disableemail][-voidsms][-replacemessages ID][-replacefile file]"
#define SMS_PICTURE_OPTIONS	"[-text text][-unicode][-alcatelbmmi]"
#define SMS_PROFILE_OPTIONS	"[-name name][-bitmap bitmap][-ringtone ringtone]"
#define SMS_EMS_OPTIONS		"[-unicode][-16bit][-format lcrasbiut][-text text][-unicodefiletext file][-defsound ID][-defanimation ID][-tone10 file][-tone10long file][-tone12 file][-tone12long file][-toneSE file][-toneSElong file][-fixedbitmap file][-variablebitmap file][-variablebitmaplong file][-animation frames file1 ...][-protected number]"
#define SMS_SMSTEMPLATE_OPTIONS	"[-unicode][-text text][-unicodefiletext file][-defsound ID][-defanimation ID][-tone10 file][-tone10long file][-tone12 file][-tone12long file][-toneSE file][-toneSElong file][-variablebitmap file][-variablebitmaplong file][-animation frames file1 ...]"
#define SMS_ANIMATION_OPTIONS	""
#define SMS_OPERATOR_OPTIONS	"[-netcode netcode][-biglogo]"
#define SMS_RINGTONE_OPTIONS	"[-long][-scale]"
#define SMS_SAVE_OPTIONS	"[-folder id][-unread][-read][-unsent][-sent][-sender number][-smsname name]"
#define SMS_SEND_OPTIONS	"[-report][-validity HOUR|6HOURS|DAY|3DAYS|WEEK|MAX][-save [-folder number]]"
#define SMS_COMMON_OPTIONS	"[-smscset number][-smscnumber number][-reply][-maxsms num]"

	{"savesms",			1,30, SendSaveDisplaySMS,	{H_SMS,0},			"TEXT " SMS_SAVE_OPTIONS SMS_COMMON_OPTIONS SMS_TEXT_OPTIONS},
	{"savesms",			1,30, SendSaveDisplaySMS,	{H_SMS,H_Ringtone,0},		"RINGTONE file " SMS_SAVE_OPTIONS SMS_COMMON_OPTIONS SMS_RINGTONE_OPTIONS},
	{"savesms",			1,30, SendSaveDisplaySMS,	{H_SMS,H_Logo,0},		"OPERATOR file " SMS_SAVE_OPTIONS SMS_COMMON_OPTIONS SMS_OPERATOR_OPTIONS},
	{"savesms",			1,30, SendSaveDisplaySMS,	{H_SMS,H_Logo,0},		"CALLER file " SMS_SAVE_OPTIONS SMS_COMMON_OPTIONS},
	{"savesms",			1,30, SendSaveDisplaySMS,	{H_SMS,H_Logo,0},		"PICTURE file " SMS_SAVE_OPTIONS SMS_COMMON_OPTIONS SMS_PICTURE_OPTIONS},
	{"savesms",			1,30, SendSaveDisplaySMS,	{H_SMS,H_Logo,0},		"ANIMATION frames file1 file2... " SMS_SAVE_OPTIONS SMS_COMMON_OPTIONS SMS_ANIMATION_OPTIONS},
	{"savesms",			1,30, SendSaveDisplaySMS,	{H_SMS,H_MMS,0},		"MMSINDICATOR URL Title Sender " SMS_SAVE_OPTIONS SMS_COMMON_OPTIONS},
	{"savesms",			1,30, SendSaveDisplaySMS,	{H_SMS,H_WAP,0},		"WAPINDICATOR URL Title " SMS_SAVE_OPTIONS SMS_COMMON_OPTIONS},
#ifdef GSM_ENABLE_BACKUP
	{"savesms",			1,30, SendSaveDisplaySMS,	{H_SMS,H_WAP,0},		"BOOKMARK file location " SMS_SAVE_OPTIONS SMS_COMMON_OPTIONS},
	{"savesms",			1,30, SendSaveDisplaySMS,	{H_SMS,H_WAP,0},		"WAPSETTINGS file location DATA|GPRS " SMS_SAVE_OPTIONS SMS_COMMON_OPTIONS},
	{"savesms",			1,30, SendSaveDisplaySMS,	{H_SMS,H_MMS,0},		"MMSSETTINGS file location  " SMS_SAVE_OPTIONS SMS_COMMON_OPTIONS},
	{"savesms",			1,30, SendSaveDisplaySMS,	{H_SMS,H_Calendar,0},		"CALENDAR file location " SMS_SAVE_OPTIONS SMS_COMMON_OPTIONS},
	{"savesms",			1,30, SendSaveDisplaySMS,	{H_SMS,H_ToDo,0},		"TODO file location " SMS_SAVE_OPTIONS SMS_COMMON_OPTIONS},
	{"savesms",			1,30, SendSaveDisplaySMS,	{H_SMS,H_Memory,0},		"VCARD10|VCARD21 file SM|ME location [-nokia]" SMS_SAVE_OPTIONS SMS_COMMON_OPTIONS},
#endif
	{"savesms",			1,30, SendSaveDisplaySMS,	{H_SMS,H_Settings,0},		"PROFILE " SMS_SAVE_OPTIONS SMS_COMMON_OPTIONS SMS_PROFILE_OPTIONS},
	{"savesms",			1,30, SendSaveDisplaySMS,	{H_SMS,0},			"EMS " SMS_SAVE_OPTIONS SMS_COMMON_OPTIONS SMS_EMS_OPTIONS},
	{"savesms",			1,30, SendSaveDisplaySMS,	{H_SMS,0},			"SMSTEMPLATE " SMS_SAVE_OPTIONS SMS_COMMON_OPTIONS SMS_SMSTEMPLATE_OPTIONS},

	{"sendsms",			2,30, SendSaveDisplaySMS,	{H_SMS,0},			"TEXT destination " SMS_SEND_OPTIONS SMS_COMMON_OPTIONS SMS_TEXT_OPTIONS},
	{"sendsms",			2,30, SendSaveDisplaySMS,	{H_SMS,H_Ringtone,0},		"RINGTONE destination file " SMS_SEND_OPTIONS SMS_COMMON_OPTIONS SMS_RINGTONE_OPTIONS},
	{"sendsms",			2,30, SendSaveDisplaySMS,	{H_SMS,H_Logo,0},		"OPERATOR destination file " SMS_SEND_OPTIONS SMS_COMMON_OPTIONS SMS_OPERATOR_OPTIONS},
	{"sendsms",			2,30, SendSaveDisplaySMS,	{H_SMS,H_Logo,0},		"CALLER destination file " SMS_SEND_OPTIONS SMS_COMMON_OPTIONS},
	{"sendsms",			2,30, SendSaveDisplaySMS,	{H_SMS,H_Logo,0},		"PICTURE destination file " SMS_SEND_OPTIONS SMS_COMMON_OPTIONS SMS_PICTURE_OPTIONS},
	{"sendsms",			2,30, SendSaveDisplaySMS,	{H_SMS,H_Logo,0},		"ANIMATION destination frames file1 file2... " SMS_SEND_OPTIONS SMS_COMMON_OPTIONS SMS_ANIMATION_OPTIONS},
	{"sendsms",			2,30, SendSaveDisplaySMS,	{H_SMS,H_MMS,0},		"MMSINDICATOR destination URL Title Sender " SMS_SEND_OPTIONS SMS_COMMON_OPTIONS},
	{"sendsms",			2,30, SendSaveDisplaySMS,	{H_SMS,H_WAP,0},		"WAPINDICATOR destination URL Title " SMS_SEND_OPTIONS SMS_COMMON_OPTIONS},
#ifdef GSM_ENABLE_BACKUP
	{"sendsms",			2,30, SendSaveDisplaySMS,	{H_SMS,H_WAP,0},		"BOOKMARK destination file location " SMS_SEND_OPTIONS SMS_COMMON_OPTIONS},
	{"sendsms",			2,30, SendSaveDisplaySMS,	{H_SMS,H_WAP,0},		"WAPSETTINGS destination file location DATA|GPRS " SMS_SEND_OPTIONS SMS_COMMON_OPTIONS},
	{"sendsms",			2,30, SendSaveDisplaySMS,	{H_SMS,H_MMS,0},		"MMSSETTINGS destination file location " SMS_SEND_OPTIONS SMS_COMMON_OPTIONS},
	{"sendsms",			2,30, SendSaveDisplaySMS,	{H_SMS,H_Calendar,0},		"CALENDAR destination file location " SMS_SEND_OPTIONS SMS_COMMON_OPTIONS},
	{"sendsms",			2,30, SendSaveDisplaySMS,	{H_SMS,H_ToDo,0},		"TODO destination file location " SMS_SEND_OPTIONS SMS_COMMON_OPTIONS},
	{"sendsms",			2,30, SendSaveDisplaySMS,	{H_SMS,H_Memory,0},		"VCARD10|VCARD21 destination file SM|ME location [-nokia]" SMS_SEND_OPTIONS SMS_COMMON_OPTIONS},
#endif
	{"sendsms",			2,30, SendSaveDisplaySMS,	{H_SMS,H_Settings,0},		"PROFILE destination " SMS_SEND_OPTIONS SMS_COMMON_OPTIONS ""SMS_PROFILE_OPTIONS},
	{"sendsms",			2,30, SendSaveDisplaySMS,	{H_SMS,0},			"EMS destination " SMS_SEND_OPTIONS SMS_COMMON_OPTIONS SMS_EMS_OPTIONS},
	{"sendsms",			2,30, SendSaveDisplaySMS,	{H_SMS,0},			"SMSTEMPLATE destination " SMS_SEND_OPTIONS SMS_COMMON_OPTIONS SMS_SMSTEMPLATE_OPTIONS},

	{"displaysms",		2,30, SendSaveDisplaySMS,	{H_SMS,0},			"... (options like in sendsms)"},

	{"addsmsfolder",		1, 1, AddSMSFolder,		{H_SMS,0},			"name"},
#ifdef HAVE_MYSQL_MYSQL_H
	{"smsd",			2, 2, SMSDaemon,		{H_SMS,H_SMSD,0},		"MYSQL configfile"},
#endif
#ifdef HAVE_POSTGRESQL_LIBPQ_FE_H
	{"smsd",			2, 2, SMSDaemon,		{H_SMS,H_SMSD,0},		"PGSQL configfile"},
#endif
	{"smsd",			2, 2, SMSDaemon,		{H_SMS,H_SMSD,0},		"FILES configfile"},
	{"sendsmsdsms",		2,30, SendSaveDisplaySMS,	{H_SMS,H_SMSD,0},		"TEXT|WAPSETTINGS|... destination FILES|MYSQL|PGSQL configfile ... (options like in sendsms)"},
	{"getmmsfolders",		0, 0, GetMMSFolders,		{H_MMS,0},			""},
	{"getallmms",			0, 1, GetEachMMS,		{H_MMS,0},			"[-save]"},
	{"geteachmms",		0, 1, GetEachMMS,		{H_MMS,0},			"[-save]"},
	{"getringtone",		1, 2, GetRingtone,		{H_Ringtone,0},			"location [file]"},
	{"getphoneringtone",		1, 2, GetRingtone,		{H_Ringtone,0},			"location [file]"},
	{"getringtoneslist",		0, 0, GetRingtonesList,		{H_Ringtone,0},			""},
	{"setringtone",		1, 6, SetRingtone,		{H_Ringtone,0},			"file [-location location][-scale][-name name]"},
#if defined(GSM_ENABLE_NOKIA_DCT3) || defined(GSM_ENABLE_NOKIA_DCT4)
	{"nokiacomposer",		1, 1, NokiaComposer,		{H_Ringtone,H_Nokia,0},		"file"},
#endif
	{"copyringtone",		2, 3, CopyRingtone,		{H_Ringtone,0},			"source destination [RTTL|BINARY]"},
	{"getussd",			1, 1, GetUSSD,			{H_Call,0},			"code"},
	{"dialvoice",			1, 2, DialVoice,		{H_Call,0},			"number [show|hide]"},
	{"maketerminatedcall",	2, 3, MakeTerminatedCall,	{H_Call,0},			"number length [show|hide]"},
	{"getspeeddial",		1, 2, GetSpeedDial,		{H_Call,H_Memory,0},		"start [stop]"},
	{"cancelcall",		0, 1, CancelCall,		{H_Call,0},			"[ID]"},
	{"answercall",		0, 1, AnswerCall,		{H_Call,0},			"[ID]"},
	{"unholdcall",		1, 1, UnholdCall,		{H_Call,0},			"ID"},
	{"holdcall",			1, 1, HoldCall,			{H_Call,0},			"ID"},
	{"conferencecall",		1, 1, ConferenceCall,		{H_Call,0},			"ID"},
	{"splitcall",			1, 1, SplitCall,		{H_Call,0},			"ID"},
	{"switchcall",		0, 1, SwitchCall,		{H_Call,0},			"[ID]"},
	{"transfercall",		0, 1, TransferCall,		{H_Call,0},			"[ID]"},
	{"divert",			3, 5, CallDivert,		{H_Call,0},			"get|set all|busy|noans|outofreach all|voice|fax|data [number timeout]"},
	{"canceldiverts",		0, 0, CancelAllDiverts,		{H_Call,0},			""},
	{"senddtmf",			1, 1, SendDTMF,			{H_Call,0},			"sequence"},
	{"getcalendarsettings",	0, 0, GetCalendarSettings,	{H_Calendar,H_Settings,0},	""},
	{"getalltodo",		0, 0, GetAllToDo,		{H_ToDo,0},			""},
	{"listtodocategory",		1, 1, ListToDoCategory,		{H_ToDo, H_Category,0},		"text|number"},
	{"gettodo",			1, 2, GetToDo,			{H_ToDo,0},			"start [stop]"},
	{"deletetodo",		1, 2, DeleteToDo,		{H_ToDo,0},			"start [stop]"},
	{"getallnotes",		0, 0, GetAllNotes,		{H_Note,0},			""},
	{"deletecalendar",		1, 2, DeleteCalendar,		{H_Calendar,0},			"start [stop]"},
	{"getallcalendar",		0, 0, GetAllCalendar,		{H_Calendar,0},			""},
	{"getcalendar",		1, 2, GetCalendar,		{H_Calendar,0},			"start [stop]"},
	{"addcategory",       	2, 2, AddCategory,       	{H_Category,H_ToDo,H_Memory,0},	"TODO|PHONEBOOK text"},
	{"getcategory",       	2, 3, GetCategory,       	{H_Category,H_ToDo,H_Memory,0},	"TODO|PHONEBOOK start [stop]"},
	{"getallcategory",	  	1, 1, GetAllCategories,  	{H_Category,H_ToDo,H_Memory,0},	"TODO|PHONEBOOK"},
	{"reset",			1, 1, Reset,			{H_Other,0},			"SOFT|HARD"},
	{"getprofile",		1, 2, GetProfile,		{H_Settings,0},			"start [stop]"},
	{"getsecuritystatus",		0, 0, GetSecurityStatus,	{H_Info,0},			""},
	{"entersecuritycode",		2, 2, EnterSecurityCode,	{H_Other,0},			"PIN|PUK|PIN2|PUK2 code"},
	{"deletewapbookmark", 	1, 2, DeleteWAPBookmark, 	{H_WAP,0},			"start [stop]"},
	{"getwapbookmark",		1, 2, GetWAPBookmark,		{H_WAP,0},			"start [stop]"},
	{"getwapsettings",		1, 2, GetWAPMMSSettings,	{H_WAP,0},			"start [stop]"},
	{"getmmssettings",		1, 2, GetWAPMMSSettings,	{H_MMS,0},			"start [stop]"},
	{"getsyncmlsettings",		1, 2, GetSyncMLSettings,	{H_WAP,0},			"start [stop]"},
	{"getchatsettings",		1, 2, GetChatSettings,		{H_WAP,0},			"start [stop]"},
	{"readmmsfile",		1, 2, ReadMMSFile,		{H_MMS,0},			"file [-save]"},
	{"getbitmap",			1, 3, GetBitmap,		{H_Logo,0},			"STARTUP [file]"},
	{"getbitmap",			1, 3, GetBitmap,		{H_Logo,0},			"CALLER location [file]"},
	{"getbitmap",			1, 3, GetBitmap,		{H_Logo,0},			"OPERATOR [file]"},
	{"getbitmap",			1, 3, GetBitmap,		{H_Logo,0},			"PICTURE location [file]"},
	{"getbitmap",			1, 3, GetBitmap,		{H_Logo,0},			"TEXT"},
	{"getbitmap",			1, 3, GetBitmap,		{H_Logo,0},			"DEALER"},
	{"setbitmap",			1, 4, SetBitmap,		{H_Logo,0},			"STARTUP file|1|2|3"},
	{"setbitmap",			1, 4, SetBitmap,		{H_Logo,0},			"COLOURSTARTUP [fileID]"},
	{"setbitmap",			1, 4, SetBitmap,		{H_Logo,0},			"WALLPAPER fileID"},
	{"setbitmap",			1, 4, SetBitmap,		{H_Logo,0},			"CALLER location [file]"},
	{"setbitmap",			1, 4, SetBitmap,		{H_Logo,0},			"OPERATOR [file [netcode]]"},
	{"setbitmap",			1, 4, SetBitmap,		{H_Logo,0},			"COLOUROPERATOR [fileID [netcode]]"},
	{"setbitmap",			1, 4, SetBitmap,		{H_Logo,0},			"PICTURE file location [text]"},
	{"setbitmap",			1, 4, SetBitmap,		{H_Logo,0},			"TEXT text"},
	{"setbitmap",			1, 4, SetBitmap,		{H_Logo,0},			"DEALER text"},
	{"copybitmap",		1, 3, CopyBitmap,		{H_Logo,0},			"inputfile [outputfile [OPERATOR|PICTURE|STARTUP|CALLER]]"},
	{"presskeysequence",		1, 1, PressKeySequence,		{H_Other,0},			"mMnNpPuUdD+-123456789*0#gGrR<>[]hHcCjJfFoOmMdD@"},
#if defined(WIN32) || defined(HAVE_PTHREAD)
	{"searchphone",		0, 1, SearchPhone,		{H_Other,0},			"[-debug]"},
#endif
#ifdef GSM_ENABLE_BACKUP
	{"savefile",			4, 5, SaveFile,			{H_Backup,H_Calendar,0},	"CALENDAR target.vcs file location"},
	{"savefile",			4, 5, SaveFile,			{H_Backup,H_ToDo,0},		"TODO target.vcs file location"},
	{"savefile",			4, 5, SaveFile,			{H_Backup,H_Memory,0},		"VCARD10|VCARD21 target.vcf file SM|ME location"},
	{"savefile",			4, 5, SaveFile,			{H_Backup,H_WAP,0},		"BOOKMARK target.url file location"},
	{"backup",			1, 2, Backup,			{H_Backup,H_Memory,H_Calendar,H_ToDo,H_Category,H_Ringtone,H_WAP,H_FM,0},			"file [-yes]"},
	{"backupsms",			1, 1, BackupSMS,		{H_Backup,H_SMS,0},		"file"},
	{"restore",			1, 2, Restore,			{H_Backup,H_Memory,H_Calendar,H_ToDo,H_Category,H_Ringtone,H_WAP,H_FM,0},			"file [-yes]"},
	{"addnew",			1, 2, AddNew,			{H_Backup,H_Memory,H_Calendar,H_ToDo,H_Category,H_Ringtone,H_WAP,H_FM,0},			"file [-yes]"},
	{"restoresms",		1, 1, RestoreSMS,		{H_Backup,H_SMS,0},		"file"},
	{"addsms",			2, 2, AddSMS,			{H_Backup,H_SMS,0},		"folder file"},
#endif
	{"clearall",			0, 0, ClearAll,			{H_Memory,H_Calendar,H_ToDo,H_Category,H_Ringtone,H_WAP,H_FM,0},	""},
	{"networkinfo",		0, 0, NetworkInfo,		{H_Network,0},			""},
#ifdef GSM_ENABLE_AT
	{"siemenssatnetmon",		0, 0, ATSIEMENSSATNetmon,	{H_Siemens,H_Network,0},	""},
	{"siemensnetmonact",		1, 1, ATSIEMENSActivateNetmon,	{H_Siemens,H_Network,0},	"netmon_type (1-full, 2-simple)"},
	{"siemensnetmonitor",		1, 1, ATSIEMENSNetmonitor,	{H_Siemens,H_Network,0},	"test"},
#endif
#ifdef GSM_ENABLE_NOKIA6110
	{"nokiagetoperatorname", 	0, 0, DCT3GetOperatorName,	{H_Nokia,H_Network,0},		""},
	{"nokiasetoperatorname", 	0, 2, DCT3SetOperatorName,	{H_Nokia,H_Network,0},		"[networkcode name]"},
	{"nokiadisplayoutput", 	0, 0, DCT3DisplayOutput,	{H_Nokia,0},			""},
#endif
#ifdef GSM_ENABLE_NOKIA_DCT3
	{"nokianetmonitor",		1, 1, DCT3netmonitor,		{H_Nokia,H_Network,0},		"test"},
	{"nokianetmonitor36",		0, 0, DCT3ResetTest36,		{H_Nokia,0},			""},
	{"nokiadebug",		1, 2, DCT3SetDebug,		{H_Nokia,H_Network,0},		"filename [[v11-22][,v33-44]...]"},
#endif
#ifdef GSM_ENABLE_NOKIA_DCT4
	{"nokiagetpbkfeatures",	1, 1, DCT4GetPBKFeatures,	{H_Nokia,H_Memory,0},		"memorytype"},
	{"nokiasetvibralevel",	1, 1, DCT4SetVibraLevel,	{H_Nokia,H_Other,0},		"level"},
	{"nokiagetvoicerecord",	1, 1, DCT4GetVoiceRecord,	{H_Nokia,H_Other,0},		"location"},
#ifdef GSM_ENABLE_NOKIA6510
	{"nokiasetlights",		2, 2, DCT4SetLight,		{H_Nokia,H_Tests,0},		"keypad|display|torch on|off"},
	{"nokiatuneradio",		0, 0, DCT4TuneRadio,		{H_Nokia,H_FM,0},		""},
#endif
	{"nokiamakecamerashoot",	0, 0, DCT4MakeCameraShoot,	{H_Nokia,H_Other,0},		""},
	{"nokiagetscreendump",	0, 0, DCT4GetScreenDump,	{H_Nokia,H_Other,0},		""},
#endif
#if defined(GSM_ENABLE_NOKIA_DCT3) || defined(GSM_ENABLE_NOKIA_DCT4)
	{"nokiavibratest",		0, 0, NokiaVibraTest,		{H_Nokia,H_Tests,0},		""},
	{"nokiagett9",		0, 0, NokiaGetT9,		{H_Nokia,H_SMS,0},		""},
	{"nokiadisplaytest",		1, 1, NokiaDisplayTest,		{H_Nokia,H_Tests,0},		"number"},
	{"nokiagetadc",		0, 0, NokiaGetADC,		{H_Nokia,H_Tests,0},		""},
	{"nokiasecuritycode",		0, 0, NokiaSecurityCode,	{H_Nokia,H_Info,0},		""},
	{"nokiaselftests",		0, 0, NokiaSelfTests,		{H_Nokia,H_Tests,0},		""},
	{"nokiasetphonemenus",	0, 0, NokiaSetPhoneMenus,	{H_Nokia,H_Other,0},		""},
#endif
#ifdef DEBUG
	{"decodesniff",		2, 3, decodesniff,		{H_Decode,0},			"MBUS2|IRDA file [phonemodel]"},
	{"decodebinarydump",		1, 2, decodebinarydump,		{H_Decode,0},			"file [phonemodel]"},
	{"makeconverttable",		1, 1, MakeConvertTable,		{H_Decode,0},			"file"},
#endif
	{"batch",			0, 1, RunBatch,			{H_Other,0},			"[file]"},
	{"",				0, 0, NULL,			{0}, ""}
};

static HelpCategoryDescriptions HelpDescriptions[] = {
	{H_Call,	"call",		N_("Calls")},
	{H_SMS,		"sms",		N_("SMS and EMS")},
	{H_Memory,	"memory",	N_("Memory (phonebooks and calls)")},
	{H_Filesystem,	"filesystem",	N_("Filesystem")},
	{H_Logo,	"logo",		N_("Logo and pictures")},
	{H_Ringtone,	"ringtone",	N_("Ringtones")},
	{H_Calendar,	"calendar",	N_("Calendar notes")},
	{H_ToDo,	"todo",		N_("To do lists")},
	{H_Note,	"note",		N_("Notes")},
	{H_DateTime,	"datetime",	N_("Date, time and alarms")},
	{H_Category,	"category",	N_("Categories")},
#ifdef GSM_ENABLE_BACKUP
	{H_Backup,	"backup",	N_("Backing up and restoring")},
#endif
#if defined(GSM_ENABLE_NOKIA_DCT3) || defined(GSM_ENABLE_NOKIA_DCT4)
	{H_Nokia,	"nokia",	N_("Nokia specific")},
#endif
#ifdef GSM_ENABLE_AT
	{H_Siemens,	"siemens",	N_("Siemens specific")},
#endif
	{H_Network,	"network",	N_("Network")},
	{H_WAP,		"wap",		N_("WAP settings and bookmarks")},
	{H_MMS,		"mms",		N_("MMS and MMS settings")},
	{H_Tests,	"tests",	N_("Phone tests")},
	{H_FM,		"fm",		N_("FM radio")},
	{H_Info,	"info",		N_("Phone information")},
	{H_Settings,	"settings",	N_("Phone settings")},
#ifdef DEBUG
	{H_Decode,	"decode",	N_("Dumps decoding")},
#endif
	{H_Other,	"other",	N_("Functions that don't fit elsewhere")},
	{H_Gammu,	"gammu",	N_("Gammu information")},
	{H_SMSD,	"smsd",		N_("SMS daemon")},
	{0,		NULL,		NULL}
/* *INDENT-ON* */
};

void HelpHeader(void)
{
	PrintVersion();
}

void HelpGeneral(void)
{
	int i = 0;

	HelpHeader();

	printf("%s\n\n",
	       ("Usage: gammu [confign] [nothing|text|textall|binary|errors] <command> [options]"));
	printf("%s\n",
	       ("First parameter optionally specifies which config section to use (all are probed by default)."));
	printf("%s\n\n",
	       ("Second parameter optionally controls debug level, next one specifies actions."));

	printf("%s\n\n",
	       _("Commands can be specified with or without leading --."));

	/* We might want to put here some most used commands */
	printf("%s\n\n",
	       ("For more details, call help on specific topic (gammu --help topic). Topics are:"));

	while (HelpDescriptions[i].category != 0) {
		printf("%11s - %s\n", HelpDescriptions[i].option,
		       gettext(HelpDescriptions[i].description));
		i++;
	}
	printf("\n");
}

void HelpSplit(int cols, int len, unsigned char *buff)
{
	int l, len2, pos, split;
	bool in_opt, first = true;
	char *remain, spaces[50], buffer[500];

	if (cols == 0) {
		printf(" %s\n", buff);
	} else {
		printf(" ");
		spaces[0] = 0;
		len2 = strlen(buff);
		if (len + len2 < cols) {
			printf("%s\n", buff);
		} else {
			for (l = 0; l < len; l++)
				strcat(spaces, " ");

			remain = buff;

			while (strlen(remain) > 0) {
				split = 0;
				pos = 0;
				in_opt = false;
				if (!first)
					printf(spaces);
				while (pos < cols - len && remain[pos] != 0) {
					if (in_opt && remain[pos] == ']') {
						in_opt = false;
						split = pos;
					} else if (remain[pos] == '[') {
						in_opt = true;
					} else if (!in_opt
						   && remain[pos] == ' ') {
						split = pos - 1;
					}
					pos++;
				}
				/* Can not be split */
				if (split == 0) {
					printf("%s\n", remain);
					remain += strlen(remain);
				} else {
					first = false;
					split++;
					strncpy(buffer, remain, split);
					buffer[split] = 0;
					printf("%s\n", buffer);
					remain += split;
					if (remain[0] == ' ')
						remain++;
				}
			}
		}
	}
}

void Help(int argc, char *argv[])
{
	int i = 0, j = 0, k, cols;
	bool disp;

#ifdef TIOCGWINSZ
	struct winsize w;
#endif
#if defined(WIN32) || defined(DJGPP)
#else
	char *columns;
#endif

	/* Just --help */
	if (argc == 2) {
		HelpGeneral();
		return;
	}

	if (!strcmp(argv[2], "all")) {
		HelpHeader();
	} else {
		while (HelpDescriptions[i].category != 0) {
			if (strcasecmp(argv[2], HelpDescriptions[i].option) ==
			    0)
				break;
			i++;
		}
		if (HelpDescriptions[i].category == 0) {
			HelpGeneral();
			printf("%s\n", _("Unknown help topic specified!"));
			return;
		}
		HelpHeader();
		printf(_("Gammu commands, topic: %s\n\n"),
		       HelpDescriptions[i].description);
	}

#if defined(WIN32) || defined(DJGPP)
	cols = 80;
#else
	cols = 0;
	/* If stdout is a tty, we will wrap to columns it has */
	if (isatty(1)) {
#ifdef TIOCGWINSZ
		if (ioctl(2, TIOCGWINSZ, &w) == 0) {
			if (w.ws_col > 0)
				cols = w.ws_col;
		}
#endif
		if (cols == 0) {
			columns = getenv("COLUMNS");
			if (columns != NULL) {
				cols = atoi(columns);
				if (cols <= 0)
					cols = 0;
			}
		}

		if (cols == 0) {
			/* Fallback */
			cols = 80;
		}
	}
#endif

	while (Parameters[j].Function != NULL) {
		k = 0;
		disp = false;
		if (!strcmp(argv[2], "all")) {
			if (j == 0)
				disp = true;
			if (j != 0) {
				if (strcmp
				    (Parameters[j].help,
				     Parameters[j - 1].help)) {
					disp = true;
				} else {
					if (strcmp
					    (Parameters[j].parameter,
					     Parameters[j - 1].parameter)) {
						disp = true;
					}
				}
			}
		} else {
			while (Parameters[j].help_cat[k] != 0) {
				if (Parameters[j].help_cat[k] ==
				    HelpDescriptions[i].category) {
					disp = true;
					break;
				}
				k++;
			}
		}
		if (disp) {
			printf("%s", Parameters[j].parameter);
			if (Parameters[j].help[0] == 0) {
				printf("\n");
			} else {
				HelpSplit(cols - 1,
					  strlen(Parameters[j].parameter) + 1,
					  gettext(Parameters[j].help));
			}
		}
		j++;
	}
}

int FoundVersion(unsigned char *Buffer)
{
	size_t retval = 0, pos = 0;

	retval = atoi(Buffer) * 10000;
	while (Buffer[pos] != '.') {
		pos++;
		if (pos == strlen(Buffer))
			return retval;
	}
	pos++;
	retval += atoi(Buffer + pos) * 100;
	while (Buffer[pos] != '.') {
		pos++;
		if (pos == strlen(Buffer))
			return retval;
	}
	pos++;
	return retval + atoi(Buffer + pos);
}

int ProcessParameters(char start, int argc, char *argv[])
{
	int z = 0;
	bool count_failed = false;

	/* Check parameters */
	while (Parameters[z].Function != NULL) {
		if (strcasecmp(Parameters[z].parameter, argv[1 + start]) == 0 ||
		    (strncmp(argv[1 + start], "--", 2) == 0 &&
		     strcasecmp(Parameters[z].parameter,
				argv[1 + start] + 2) == 0)
		    ) {
			if (argc - 2 - start < Parameters[z].min_arg) {
				if (!count_failed) {
					if (Parameters[z].min_arg ==
					    Parameters[z].max_arg) {
						printf(("More parameters required (function requires %d)\n"),
						       Parameters[z].min_arg);
					} else {
						printf(("More parameters required (function requires %d to %d)\n"),
						       Parameters[z].min_arg,
						       Parameters[z].max_arg);
					}
					if (Parameters[z].help[0] != 0) {
						printf("%s:\n",
						       _("Parameters help"));
					}
				}
				if (Parameters[z].help[0] != 0) {
					printf("%s\n",
					       gettext(Parameters[z].help));
				}
				count_failed = true;
			} else if (argc - 2 - start > Parameters[z].max_arg) {
				if (!count_failed) {
					if (Parameters[z].min_arg ==
					    Parameters[z].max_arg) {
						printf(("Too many parameters (function accepts %d)\n"),
						       Parameters[z].min_arg);
					} else {
						printf(("Too many parameters (function accepts %d to %d)\n"),
						       Parameters[z].min_arg,
						       Parameters[z].max_arg);
					}
					if (Parameters[z].help[0] != 0) {
						printf("%s:\n",
						       _("Parameters help"));
					}
				}
				if (Parameters[z].help[0] != 0) {
					printf("%s\n",
					       gettext(Parameters[z].help));
				}
				count_failed = true;
			} else {
				Parameters[z].Function(argc - start,
						       argv + start);
				break;
			}
		}
		z++;
	}

	/* Tell user when we did nothing */
	if (Parameters[z].Function == NULL) {
		if (!count_failed) {
			HelpGeneral();
			printf("%s\n", _("Bad option!"));
			return 2;
		}
		return 1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	GSM_File RSS;
	int rsslevel = 0, oldpos = 0;
	size_t pos = 0;
	int start = 0;
	unsigned int i;
	int only_config = -1;
	char *cp, *rss, buff[200];
	GSM_Config *smcfg;
	GSM_Config *smcfg0;
	GSM_Debug_Info *di;
	GSM_Error error;
	INI_Section *cfg = NULL;

	s = GSM_AllocStateMachine();

	GSM_InitLocales(NULL);

	di = GSM_GetGlobalDebug();

#ifdef DEBUG
	GSM_SetDebugFileDescriptor(stdout, di);
	GSM_SetDebugLevel("textall", di);
#endif

	/* Any parameters? */
	if (argc == 1) {
		HelpGeneral();
		printf("%s\n", _("Too few parameters!"));
		exit(1);
	}

	/* Help? */
	if (strcasecmp(argv[1 + start], "--help") == 0 ||
	    strcasecmp(argv[1 + start], "-h") == 0 ||
	    strcasecmp(argv[1 + start], "help") == 0) {
		Help(argc - start, argv + start);
		exit(1);
	}

	/* Is first parameter numeric? If so treat it as config that should be loaded. */
	if (isdigit(argv[1][0])) {
		only_config = atoi(argv[1]);
		if (only_config >= 0)
			start++;
		else
			only_config = -1;
	}

	error = GSM_FindGammuRC(&cfg);
	if (error != ERR_NONE) {
		if (error == ERR_FILENOTSUPPORTED) {
			printf_warn("%s\n",
				    _("Configuration could not be parsed!"));
		} else {
			printf_warn("%s\n", _("No configuration file found!"));
		}
	}
	if (cfg == NULL)
		printf_warn("%s\n",
			    ("No configuration read, using builtin defaults!"));

	smcfg0 = GSM_GetConfig(s, 0);

	for (i = 0; (smcfg = GSM_GetConfig(s, i)) != NULL; i++) {
		/* Wanted user specific configuration? */
		if (only_config != -1) {
			smcfg = smcfg0;
			/* Here we get only in first for loop */
			if (!GSM_ReadConfig(cfg, smcfg, only_config)) {
				printf_err(("Failed to read [gammu%d] from gammurc!\n"),
					   only_config);
				printf_warn("%s\n",
					    ("No configuration read, using builtin defaults!"));
				GSM_ReadConfig(NULL, smcfg, 0);
			}
		} else {
			if (!GSM_ReadConfig(cfg, smcfg, i) && i != 0)
				break;
		}
		GSM_SetConfigNum(s, GSM_GetConfigNum(s) + 1);

		if (cfg != NULL) {
			cp = INI_GetValue(cfg, "gammu", "gammucoding", false);
			if (cp) {
				GSM_SetDebugCoding(cp, di);
			}

			smcfg->Localize =
			    INI_GetValue(cfg, "gammu", "gammuloc", false);
			/* It is safe to pass NULL here */
			GSM_InitLocales(smcfg->Localize);
		}

		/* We want to use only one file descriptor for global and state machine debug output */
		smcfg->UseGlobalDebugFile = true;

		/* It makes no sense to open several debug logs... */
		if (i != 0) {
			strcpy(smcfg->DebugLevel, smcfg0->DebugLevel);
			free(smcfg->DebugFile);
			smcfg->DebugFile = strdup(smcfg0->DebugFile);
		} else {
			/* Just for first config */
			/* When user gave debug level on command line */
			if (argc > 1 + start
			    && GSM_SetDebugLevel(argv[1 + start], di)) {
				/* Debug level from command line will be used with phone too */
				strcpy(smcfg->DebugLevel, argv[1 + start]);
				start++;
			} else {
				/* Try to set debug level from config file */
				GSM_SetDebugLevel(smcfg->DebugLevel, di);
			}
			/* If user gave debug file in gammurc, we will use it */
			error = GSM_SetDebugFile(smcfg->DebugFile, di);
			Print_Error(error);
		}

		if (i == 0) {
			rss = INI_GetValue(cfg, "gammu", "rsslevel", false);
			if (rss) {
				if (strcasecmp(rss, "teststable") == 0) {
					rsslevel = 2;
				} else if (strcasecmp(rss, "stable") == 0) {
					rsslevel = 1;
				}
			}
			rss = INI_GetValue(cfg, "gammu", "usephonedb", false);
			if (rss && strcasecmp(rss, "yes") == 0)
				phonedb = true;
		}

		/* We wanted to read just user specified configuration. */
		if (only_config != -1) {
			break;
		}
	}

	/* Do we have enough parameters? */
	if (argc == 1 + start) {
		HelpGeneral();
		printf("%s\n", _("Too few parameters!"));
		exit(-2);
	}

	/* Check used version vs. compiled */
	if (!strcasecmp(GetGammuVersion(), VERSION) == 0) {
		printf_err(("Version of installed libGammu.so (%s) is different to version of Gammu (%s)\n"),
			   GetGammuVersion(), VERSION);
		exit(-1);
	}

	if (rsslevel > 0) {
		RSS.Buffer = NULL;
		if (GSM_ReadHTTPFile("blog.cihar.com", "archives/gammu_releases/index-rss.xml", &RSS)) {
			while (pos < RSS.Used) {
				if (RSS.Buffer[pos] != 10) {
					pos++;
					continue;
				}
				RSS.Buffer[pos] = 0;
				if (strstr(RSS.Buffer + oldpos, "<title>") ==
				    NULL
				    || strstr(RSS.Buffer + oldpos,
					      "</title>") == NULL
				    || strstr(RSS.Buffer + oldpos,
					      "win32") != NULL) {
					pos++;
					oldpos = pos;
					continue;
				}
				if (rsslevel > 0
				    && strstr(RSS.Buffer + oldpos,
					      "stable version") != NULL) {
					sprintf(buff,
						strstr(RSS.Buffer + oldpos,
						       "stable version") + 15);
					for (i = 0; i < strlen(buff); i++) {
						if (buff[i] == '<') {
							buff[i] = 0;
							break;
						}
					}
					if (FoundVersion(buff) >
					    FoundVersion(VERSION)) {
						printf(("INFO: there is later stable Gammu (%s instead of %s) available!\n"),
						       buff, VERSION);
						break;
					}
				}
				if (rsslevel == 2
				    && strstr(RSS.Buffer + oldpos,
					      "test version") != NULL) {
					sprintf(buff,
						strstr(RSS.Buffer + oldpos,
						       "test version") + 13);
					for (i = 0; i < strlen(buff); i++) {
						if (buff[i] == '<') {
							buff[i] = 0;
							break;
						}
					}
					if (FoundVersion(buff) >
					    FoundVersion(VERSION)) {
						printf(("INFO: there is later testing Gammu (%s instead of %s) available!\n"),
						       buff, VERSION);
						break;
					}
				}
				pos++;
				oldpos = pos;
			}
			free(RSS.Buffer);
		}
	}

	ProcessParameters(start, argc, argv);

	/* Close debug output if opened */
	GSM_SetDebugFileDescriptor(NULL, di);

	GSM_FreeStateMachine(s);

	exit(0);
}

/* How should editor hadle tabs in this file? Add editor commands here.
 * vim: noexpandtab sw=8 ts=8 sts=8:
 */
