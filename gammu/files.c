#define _GNU_SOURCE /* For strcasestr */
#include <string.h>
#include <gammu.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

#ifdef WIN32
#  include <windows.h>
#  include <process.h>
#  ifdef _MSC_VER
#    include <sys/utime.h>
#  else
#    include <utime.h>
#  endif
#else
#  include <utime.h>
#endif

#include "../common/misc/locales.h"

#include "files.h"
#include "memory.h"
#include "message.h"
#include "common.h"
#include "formats.h"


/**
 * Displays status of filesystem (if available).
 */
GSM_Error PrintFileSystemStatus()
{
	GSM_FileSystemStatus	Status;

	error = GSM_GetFileSystemStatus(s,&Status);
	if (error != ERR_NOTSUPPORTED && error != ERR_NOTIMPLEMENTED) {
	    	Print_Error(error);
		printf("\n");
		printf(_("Phone memory: %i bytes (free %i bytes, used %i bytes)"),Status.Free+Status.Used,Status.Free,Status.Used);
		printf("\n");
		if (Status.UsedImages != 0 || Status.UsedSounds != 0 || Status.UsedThemes != 0) {
			printf(_("Used by: Images: %i, Sounds: %i, Themes: %i"), Status.UsedImages, Status.UsedSounds, Status.UsedThemes);
			printf("\n");
		}
	}
	return error;
}

void GetFileSystemStatus(int argc UNUSED, char *argv[] UNUSED)
{
	GSM_Init(true);

	PrintFileSystemStatus();

	GSM_Terminate();
}

void GetFileSystem(int argc, char *argv[])
{
	bool 			Start = true, MemoryCard = false;
	GSM_File	 	Files;
	int			j;
	long			usedphone=0,usedcard=0;
	char 			FolderName[256],IDUTF[200];

	GSM_Init(true);

	while (1) {
		error = GSM_GetNextFileFolder(s,&Files,Start);
		if (error == ERR_EMPTY) break;
	    	if (error != ERR_FOLDERPART) Print_Error(error);

		if (!Files.Folder) {
			if (GSM_IsPhoneFeatureAvailable(GSM_GetModelInfo(s), F_FILES2)) {
				if (DecodeUnicodeString(Files.ID_FullName)[0] == 'a') {
					MemoryCard = true;
					usedcard+=Files.Used;
				} else {
					usedphone+=Files.Used;
				}
			} else {
				usedphone+=Files.Used;
			}
		}

		if (argc <= 2 || !strcasecmp(argv[2],"-flatall") == 0) {
			//Nokia filesystem 1
			if (UnicodeLength(Files.ID_FullName) != 0 &&
			    (DecodeUnicodeString(Files.ID_FullName)[0]=='C' ||
			    DecodeUnicodeString(Files.ID_FullName)[0]=='c')) {
				printf("%8s.",DecodeUnicodeString(Files.ID_FullName));
			}
			if (Files.Protected) {
				/* l10n: One char to indicate protected file */
				printf(_("P"));
			} else {
				printf(" ");
			}
			if (Files.ReadOnly) {
				/* l10n: One char to indicate read only file */
				printf(_("R"));
			} else {
				printf(" ");
			}
			if (Files.Hidden) {
				/* l10n: One char to indicate hidden file */
				printf(_("H"));
			} else {
				printf(" ");
			}
			if (Files.System) {
				/* l10n: One char to indicate system file */
				printf(_("S"));
			} else {
				printf(" ");
			}
			if (argc > 2 &&  strcasecmp(argv[2],"-flat") == 0) {
				if (!Files.Folder) {
					if (strcasecmp(argv[2],"-flatall") == 0) {
						if (!Files.ModifiedEmpty) {
							printf(" %30s",OSDateTime(Files.Modified,false));
						} else printf(" %30c",0x20);
						printf(" %9zi",Files.Used);
						printf(" ");
					} else printf("|-- ");
				} else {
					if (error == ERR_FOLDERPART) {
						printf(_("Part of folder "));
					} else {
						printf(_("Folder "));
					}
				}
			} else {
				if (Files.Level != 1) {
					for (j=0;j<Files.Level-2;j++) printf(" |   ");
					printf(" |-- ");
				}
				if (Files.Folder) {
					if (error == ERR_FOLDERPART) {
						printf(_("Part of folder "));
					} else {
						printf(_("Folder "));
					}
				}
			}
			printf("\"%s\"\n",DecodeUnicodeConsole(Files.Name));
		} else if (argc > 2 && strcasecmp(argv[2],"-flatall") == 0) {
			/* format for a folder ID;Folder;FOLDER_NAME;[FOLDER_PARAMETERS]
			 * format for a file   ID;File;FOLDER_NAME;FILE_NAME;DATESTAMP;FILE_SIZE;[FILE_PARAMETERS]  */
			EncodeUTF8QuotedPrintable(IDUTF,Files.ID_FullName);
			printf("%s;",IDUTF);
			if (!Files.Folder) {
				printf(_("File;"));
				printf("\"%s\";",FolderName);
				printf("\"%s\";",DecodeUnicodeConsole(Files.Name));
				if (!Files.ModifiedEmpty) {
					printf("\"%s\";",OSDateTime(Files.Modified,false));
				} else  printf("\"%c\";",0x20);
				printf("%zi;",Files.Used);
			} else {
				if (error == ERR_FOLDERPART) {
					printf(_("Part of folder;"));
				} else {
					printf(_("Folder;"));
				}
				printf("\"%s\";",DecodeUnicodeConsole(Files.Name));
				strcpy(FolderName,DecodeUnicodeConsole(Files.Name));
			}

			if (Files.Protected)  	printf(_("P"));
			if (Files.ReadOnly)  	printf(_("R"));
			if (Files.Hidden)  	printf(_("H"));
			if (Files.System)  	printf(_("S"));
			printf("\n");
		}
		Start = false;
	}

	error = PrintFileSystemStatus();

	if (error == ERR_NOTSUPPORTED || error == ERR_NOTIMPLEMENTED) {
		printf("\n");
		printf(_("Used in phone: %li bytes"),usedphone);
		if (MemoryCard) printf(_(", used in card: %li bytes"),usedcard);
		printf("\n");
	}

	GSM_Terminate();
}

void SetFileAttrib(int argc, char *argv[])
{
	GSM_File	 	Files;
	int			i;

	Files.ReadOnly  = false;
	Files.Protected = false;
	Files.System    = false;
	Files.Hidden    = false;

	DecodeUTF8QuotedPrintable(Files.ID_FullName,argv[2],strlen(argv[2]));

	for (i=3;i<argc;i++) {
		if (strcasecmp(argv[i],"-readonly") == 0) {
			Files.ReadOnly = true;
		} else if (strcasecmp(argv[i],"-protected") == 0) {
			Files.Protected = true;
		} else if (strcasecmp(argv[i],"-system") == 0) {
			Files.System = true;
		} else if (strcasecmp(argv[i],"-hidden") == 0) {
			Files.Hidden = true;
		} else {
			fprintf(stderr, _("Unknown attribute (%s)\n"),argv[i]);
		}
	}

	GSM_Init(true);

	error = GSM_SetFileAttributes(s,&Files);
    	Print_Error(error);

	GSM_Terminate();
}

void GetRootFolders(int argc UNUSED, char *argv[] UNUSED)
{
	GSM_File 	File;
	char 		IDUTF[200];

	GSM_Init(true);

	File.ID_FullName[0] = 0;
	File.ID_FullName[1] = 0;

	while (1) {
		if (GSM_GetNextRootFolder(s,&File)!=ERR_NONE) break;
		EncodeUTF8QuotedPrintable(IDUTF,File.ID_FullName);
		printf("%s ",IDUTF);
		printf("- %s\n",DecodeUnicodeString(File.Name));
	}

	GSM_Terminate();
}

void GetFolderListing(int argc UNUSED, char *argv[])
{
	bool 			Start = true;
	GSM_File	 	Files;
	char 			IDUTF[200];

	GSM_Init(true);

	DecodeUTF8QuotedPrintable(Files.ID_FullName,argv[2],strlen(argv[2]));

	while (1) {
		error = GSM_GetFolderListing(s,&Files,Start);
		if (error == ERR_EMPTY) break;
		if (error != ERR_FOLDERPART) {
			Print_Error(error);
		} else {
			printf("%s\n\n", _("Part of folder only"));
		}

		/* format for a folder ID;Folder;[FOLDER_PARAMETERS]
		 * format for a file   ID;File;FILE_NAME;DATESTAMP;FILE_SIZE;[FILE_PARAMETERS]  */
		EncodeUTF8QuotedPrintable(IDUTF,Files.ID_FullName);
		printf("%s;",IDUTF);
		if (!Files.Folder) {
			printf(_("File;"));
			printf("\"%s\";",DecodeUnicodeConsole(Files.Name));
			if (!Files.ModifiedEmpty) {
				printf("\"%s\";",OSDateTime(Files.Modified,false));
			} else  printf("\"%c\";",0x20);
			printf("%zi;",Files.Used);
		} else {
			printf(_("Folder"));
			printf(";\"%s\";",DecodeUnicodeConsole(Files.Name));
		}

		if (Files.Protected)  	printf(_("P"));
		if (Files.ReadOnly)  	printf(_("R"));
		if (Files.Hidden)  	printf(_("H"));
		if (Files.System)  	printf(_("S"));
		printf("\n");

		Start = false;
	}

	GSM_Terminate();
}

void GetOneFile(GSM_File *File, bool newtime, int i)
{
	FILE			*file;
	bool			start;
	unsigned char		buffer[5000];
	struct utimbuf		filedate;
	int			Handle,Size,p,q,j,old1;
	time_t     		t_time1,t_time2;
	long			diff;

	if (File->Buffer != NULL) {
		free(File->Buffer);
		File->Buffer = NULL;
	}
	File->Used 	= 0;
	start		= true;

	t_time1 	= time(NULL);
	old1 		= 65536;

	error = ERR_NONE;
	while (error == ERR_NONE) {
		error = GSM_GetFilePart(s,File,&Handle,&Size);
		if (error == ERR_NONE || error == ERR_EMPTY || error == ERR_WRONGCRC) {
			if (start) {
				printf(_("Getting \"%s\"\n"), DecodeUnicodeConsole(File->Name));
				start = false;
			}
			if (File->Folder) {
				free(File->Buffer);
				GSM_Terminate();
				printf("%s\n", _("it's folder. Please give only file names"));
				exit(-1);
			}
			if (Size==0) {
				printf("*");
			} else {
				fprintf(stderr, _("%c  %i percent"), 13, (int)(File->Used*100/Size));
				if (File->Used*100/Size >= 2) {
					t_time2 = time(NULL);
					diff 	= t_time2-t_time1;
					p 	= diff*(Size-File->Used)/File->Used;
					if (p != 0) {
						if (p<old1) old1 = p;
						q = old1/60;
						fprintf(stderr, _(" (%02i:%02i minutes left)"),q,old1-q*60);
					} else {
						fprintf(stderr, "%30c",0x20);
					}
				}
			}
			if (error == ERR_EMPTY) break;
			if (error == ERR_WRONGCRC) {
				printf_warn("%s\n", _("File checksum calculated by phone doesn't match with value calculated by Gammu. File damaged or error in Gammu"));
				break;
			}
		}
	    	Print_Error(error);
	}
	printf("\n");
	if (error == ERR_NONE || error == ERR_EMPTY || error == ERR_WRONGCRC) {
		if (File->Used != 0) {
			sprintf(buffer,"%s",DecodeUnicodeConsole(File->Name));
			for (j=strlen(buffer)-1;j>0;j--) {
				if (buffer[j] == '\\' || buffer[j] == '/') break;
			}
			if (buffer[j] == '\\' || buffer[j] == '/') {
				sprintf(buffer,"%s",DecodeUnicodeConsole(File->Name+j*2+2));
			}
			file = fopen(buffer,"wb");
			if (file == NULL) {
				sprintf(buffer,"file%s",DecodeUnicodeString(File->ID_FullName));
				file = fopen(buffer,"wb");
			}
			if (file == NULL) {
				sprintf(buffer,"file%i",i);
				file = fopen(buffer,"wb");
			}
			printf(_("  Saving to %s\n"),buffer);
			if (!file) Print_Error(ERR_CANTOPENFILE);
			fwrite(File->Buffer,1,File->Used,file);
			fclose(file);
			if (!newtime && !File->ModifiedEmpty) {
				/* access time */
				filedate.actime  = Fill_Time_T(File->Modified);
				/* modification time */
				filedate.modtime = Fill_Time_T(File->Modified);
				dbgprintf("Setting date of %s\n",buffer);
				utime(buffer,&filedate);
			}
		}
	}
}

void GetFiles(int argc, char *argv[])
{
	GSM_File		File;
	int			i;
	bool			newtime = false;

	File.Buffer = NULL;

	GSM_Init(true);

	for (i=2;i<argc;i++) {
		if (strcasecmp(argv[i],"-newtime") == 0) {
			newtime = true;
			continue;
		}

		DecodeUTF8QuotedPrintable(File.ID_FullName,argv[i],strlen(argv[i]));
		dbgprintf("grabbing '%s' '%s'\n",DecodeUnicodeString(File.ID_FullName),argv[i]);
		GetOneFile(&File, newtime, i);
	}

	free(File.Buffer);
	GSM_Terminate();
}

void GetFileFolder(int argc, char *argv[])
{
	bool 			Start = true;
	GSM_File	 	File;
	int			level=0,allnum=0,num=0,filelevel=0,i=0;
	bool			newtime = false, found;
	unsigned char		IDUTF[200];

	File.Buffer = NULL;

	GSM_Init(true);

	for (i=2;i<argc;i++) {
		if (strcasecmp(argv[i],"-newtime") == 0) {
			newtime = true;
			continue;
		}
		allnum++;
	}

	while (allnum != num) {
		error = GSM_GetNextFileFolder(s,&File,Start);
		if (error == ERR_EMPTY) break;
	    	Print_Error(error);

		if (level == 0) {
			/* We search for file or folder */
			found = false;
			for (i=2;i<argc;i++) {
				if (strcasecmp(argv[i],"-newtime") == 0) {
					continue;
				}
				dbgprintf("comparing %s %s\n",DecodeUnicodeString(File.ID_FullName),argv[i]);
				DecodeUTF8QuotedPrintable(IDUTF,argv[i],strlen(argv[i]));
				if (mywstrncasecmp(File.ID_FullName,IDUTF,0)) {
					dbgprintf("found folder");
					found = true;
					if (File.Folder) {
						level 	  = 1;
						filelevel = File.Level + 1;
						Start 	  = false;
					} else {
						level = 2;
					}
					break;
				}
			}
			if (found && File.Folder) continue;
		}
		if (level == 1) {
			/* We have folder */
			dbgprintf("%i %i\n",File.Level,filelevel);
			if (File.Level != filelevel) {
				level = 0;
				num++;
			}
		}

		if (level != 0 && !File.Folder) {
			GetOneFile(&File, newtime,i);
			i++;
		}

		if (level == 2) {
			level = 0;
			num++;
		}

		Start = false;
	}

	free(File.Buffer);
	GSM_Terminate();
}

void AddOneFile(GSM_File *File, char *text, bool send)
{
	int 		Pos,Handle,i,j,old1;
	time_t     	t_time1,t_time2;
	GSM_DateTime	dt;
	long		diff;

	GSM_GetCurrentDateTime(&dt);
	t_time1 = Fill_Time_T(dt);
	old1 = 65536;

	dbgprintf("Adding file to filesystem now\n");
	error 	= ERR_NONE;
	Pos	= 0;
	while (error == ERR_NONE) {
		if (send) {
			error = GSM_SendFilePart(s,File,&Pos,&Handle);
		} else {
			error = GSM_AddFilePart(s,File,&Pos,&Handle);
		}
	    	if (error != ERR_EMPTY && error != ERR_WRONGCRC) Print_Error(error);
		if (File->Used != 0) {
			fprintf(stderr, "\r");
			fprintf(stderr, "%s", text);
			fprintf(stderr, _("%3i percent"), (int)(Pos * 100 / File->Used));
			if (Pos*100/File->Used >= 2) {
				GSM_GetCurrentDateTime(&dt);
				t_time2 = Fill_Time_T(dt);
				diff = t_time2-t_time1;
				i = diff*(File->Used-Pos)/Pos;
				if (i != 0) {
					if (i<old1) old1 = i;
					j = old1/60;
					fprintf(stderr, _(" (%02i:%02i minutes left)"), j , old1 - (j * 60));
				} else {
					fprintf(stderr, "%30c", ' ');
				}
			}
		}
	}
	fprintf(stderr, "\n");
	if (error == ERR_WRONGCRC) {
		printf_warn("%s\n", _("File checksum calculated by phone doesn't match with value calculated by Gammu. File damaged or error in Gammu"));
	}
}

void AddSendFile(int argc, char *argv[])
{
	GSM_File		File;
	int			i,nextlong;
	char			IDUTF[200];
	bool			sendfile = false;
	int			optint = 2;

	if (strcasestr(argv[1], "sendfile") != NULL) {
		sendfile = true;
	}

	File.Buffer = NULL;
	if (!sendfile) {
		DecodeUTF8QuotedPrintable(File.ID_FullName,argv[optint],strlen(argv[optint]));
		optint++;
	}
	error = GSM_ReadFile(argv[optint], &File);
	Print_Error(error);
	EncodeUnicode(File.Name,argv[optint],strlen(argv[optint]));
	for (i=strlen(argv[optint])-1;i>0;i--) {
		if (argv[optint][i] == '\\' || argv[optint][i] == '/') break;
	}
	if (argv[optint][i] == '\\' || argv[optint][i] == '/') {
		EncodeUnicode(File.Name,argv[optint]+i+1,strlen(argv[optint])-i-1);
	}
	optint++;

	GSM_IdentifyFileFormat(&File);

	File.Protected 	= false;
	File.ReadOnly	= false;
	File.Hidden	= false;
	File.System	= false;

	if (argc > optint) {
		nextlong = 0;
		for (i = optint; i < argc; i++) {
			switch(nextlong) {
			case 0:
				if (strcasecmp(argv[i],"-type") == 0) {
					nextlong = 1;
					continue;
				}
				if (strcasecmp(argv[i],"-protected") == 0) {
					File.Protected = true;
					continue;
				}
				if (strcasecmp(argv[i],"-readonly") == 0) {
					File.ReadOnly = true;
					continue;
				}
				if (strcasecmp(argv[i],"-hidden") == 0) {
					File.Hidden = true;
					continue;
				}
				if (strcasecmp(argv[i],"-system") == 0) {
					File.System = true;
					continue;
				}
				if (strcasecmp(argv[i],"-newtime") == 0) {
					File.ModifiedEmpty = true;
					continue;
				}
				printf(_("Parameter \"%s\" unknown\n"),argv[i]);
				exit(-1);
			case 1:
				if (strcasecmp(argv[i],"JAR") == 0) {
					File.Type = GSM_File_Java_JAR;
				} else if (strcasecmp(argv[i],"JPG") == 0) {
					File.Type = GSM_File_Image_JPG;
				} else if (strcasecmp(argv[i],"BMP") == 0) {
					File.Type = GSM_File_Image_BMP;
				} else if (strcasecmp(argv[i],"WBMP") == 0) {
					File.Type = GSM_File_Image_WBMP;
				} else if (strcasecmp(argv[i],"GIF") == 0) {
					File.Type = GSM_File_Image_GIF;
				} else if (strcasecmp(argv[i],"PNG") == 0) {
					File.Type = GSM_File_Image_PNG;
                                } else if (strcasecmp(argv[i],"MIDI") == 0) {
                                        File.Type = GSM_File_Sound_MIDI;
                                } else if (strcasecmp(argv[i],"AMR") == 0) {
                                        File.Type = GSM_File_Sound_AMR;
                                } else if (strcasecmp(argv[i],"NRT") == 0) {
                                        File.Type = GSM_File_Sound_NRT;
                                } else if (strcasecmp(argv[i],"3GP") == 0) {
                                        File.Type = GSM_File_Video_3GP;
				} else {
					printf(_("What file type (\"%s\") ?\n"),argv[i]);
					exit(-1);
				}
				nextlong = 0;
				break;
			}
		}
		if (nextlong!=0) {
			printf_err("%s\n", _("Parameter missing!"));
			exit(-1);
		}
	}

	GSM_Init(true);

	AddOneFile(&File, "Writing: ", sendfile);
	EncodeUTF8QuotedPrintable(IDUTF,File.ID_FullName);
	printf(_("ID of new file is \"%s\"\n"),IDUTF);

	free(File.Buffer);
	GSM_Terminate();
}

void AddFolder(int argc UNUSED, char *argv[])
{
	char			IDUTF[200];
	GSM_File 		File;

	DecodeUTF8QuotedPrintable(File.ID_FullName,argv[2],strlen(argv[2]));
	EncodeUnicode(File.Name,argv[3],strlen(argv[3]));
	File.ReadOnly = false;

	GSM_Init(true);

	error = GSM_AddFolder(s,&File);
    	Print_Error(error);
	EncodeUTF8QuotedPrintable(IDUTF,File.ID_FullName);
	printf(_("ID of new folder is \"%s\"\n"),IDUTF);

	GSM_Terminate();
}

void DeleteFolder(int argc UNUSED, char *argv[] UNUSED)
{
	unsigned char buffer[500];

	GSM_Init(true);

	DecodeUTF8QuotedPrintable(buffer,argv[2],strlen(argv[2]));

	error = GSM_DeleteFolder(s,buffer);
    	Print_Error(error);

	GSM_Terminate();
}

void DeleteFiles(int argc, char *argv[])
{
	int		i;
	unsigned char	buffer[500];

	GSM_Init(true);

	for (i=2;i<argc;i++) {
		DecodeUTF8QuotedPrintable(buffer,argv[i],strlen(argv[i]));
		error = GSM_DeleteFile(s,buffer);
	    	Print_Error(error);
	}

	GSM_Terminate();
}

/* How should editor hadle tabs in this file? Add editor commands here.
 * vim: noexpandtab sw=8 ts=8 sts=8:
 */

