/*
 * This file part of smscgi
 *
 * Copyright (C) 2007  Kamanashis Roy (kamanashisroy@gmail.com)
 *
 * smscgi is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * smscgi is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with smscgi.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <gammu.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include "sms_cgi.h"

GSM_Error error;
char cgi_path[200];
static char buffer[400]; /**<                                   decode buffer */
static char buffer2[400]; /**<                                  decode buffer */
static GSM_MultiSMSMessage smsQ; /**<                    SMS Processing queue */
static GSM_SMSMessage smsSendBuffer; /**<       memory to keep sms to be sent */
// static int child_alive;

#define CGI_ENGINE " --== CGI Engine ==-- :: "


// static int cgi_child_end() {
//	child_alive = 0;
// }

#define ERR_SUFFIX ".err"
static int cgi_get_error_fd(GSM_StateMachine *s, const char*script_name) {
	char*err_file;
	int errfd;
	
	err_file = alloca(strlen(script_name)+sizeof(ERR_SUFFIX));
	if(!err_file) {
		smprintf_level(s, D_ERROR, CGI_ENGINE "memory allocation failed : %s\n", strerror(errno));
		return -1;
	}
	
	strcpy(err_file, script_name);
	strcat(err_file, ERR_SUFFIX);
	
	/* open an error log file .. */
	errfd = fileno(fopen(err_file, "a"));
	if(errfd == -1) {
		smprintf_level(s, D_ERROR, CGI_ENGINE "could not open error log file %s : %s\n", err_file, strerror(errno));
		return -1;
	}
	return errfd;
}

static void cgi_child(GSM_StateMachine *s) {
	int x;
	int errfd;
	char script_name[300];
	const char*data;
	strcpy(script_name, cgi_path); /**<                     prepend script path */
	
	/* ---------------------------------------------------- get the script name */
	if( !(data = strchr((char*)buffer, ' ')) ) {
		/* call the error script .. */
		strcat(script_name, "error");
	} else {
		/* we have found the script name */
		strncat(script_name, buffer, data - buffer);
	}
	
	
	
	/* ---------------------------------------------------- open error log file */
	errfd = cgi_get_error_fd(s, script_name);
	if(errfd == -1) {
		goto error;
	}
	
	/* -------------------------------------------------------- redirect stderr */
	close(STDERR_FILENO);
	dup2(errfd, STDERR_FILENO);
	
	/* Before we unblock our signals, return our trapped signals back to the defaults */
	/*signal(SIGHUP, SIG_DFL);
	signal(SIGCHLD, SIG_DFL);
	signal(SIGINT, SIG_DFL);
	signal(SIGURG, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	signal(SIGPIPE, SIG_DFL);
	signal(SIGXFSZ, SIG_DFL);*/
	

	/* ----------------------------------- Close everything but stdin/out/error */
	for (x=3;x<1024;x++) {
		if(x != errfd) close(x);
	}
	
	smprintf_level(s, D_ERROR, CGI_ENGINE "Executing > %s\n", script_name);
	
	/* Execute script */
	execv(script_name, NULL);
	
	/* ------------------------------------------------------ failed to execute */
	smprintf_level(s, D_ERROR, CGI_ENGINE "Failed to execure %s : %s\n", script_name, strerror(errno));
	
	/* ************************************************************************ */
	/* we retry here ..                                                         */
	/* ************************************************************************ */
	
	/* -------------------------------------------- try to execute error script */
	close(STDERR_FILENO);
	close(errfd);
	strcpy(script_name, cgi_path); /**<                     prepend script path */
	strcat(script_name, "error");
	
	/* ---------------------------------------------------- open error log file */
	errfd = cgi_get_error_fd(s, script_name);
	if(errfd == -1) {
		goto error;
	}
	dup2(errfd, STDERR_FILENO);
	
	smprintf_level(s, D_ERROR, CGI_ENGINE "Executing > %s\n", script_name);
	
	/* Execute script */
	execv(script_name, NULL);
	
	error:
	/* ------------------------------------------------------ failed to execute */
	smprintf_level(s, D_ERROR, CGI_ENGINE "Failed to execure %s : %s\n", script_name, strerror(errno));
	
	fflush(stdout);
	_exit(1);
}

static int cgi_process_each_helper(GSM_StateMachine *s, int fd, char*data, int size) {
	int ret,offset = 0;
	while(size > offset) {
		if((ret = write(fd, data+offset, size - offset) ) <= 0) {
			smprintf_level(s, D_ERROR, CGI_ENGINE "Could not write data: %s\n", strerror(errno));
			return -1;
		}
		offset += ret;
	}
	return 0;
}

static void cgi_process_each(GSM_StateMachine *s, GSM_SMSMessage*sms) {
	int child_in[2];
	int child_out[2];
	int pid;
	int ret;
	int offset;
	int status;
	
	child_in[0] = child_in[1] = child_out[0] = child_out[1] = -1; /* invalid fd */
	
	DecodeUnicode(sms->Text, buffer);
	smprintf_level(s, D_TEXT, CGI_ENGINE "<< [%s]\n", buffer);
	
	/* ----------------------------------------------------- now open the pipes */
	if (pipe(child_in)) {
		smprintf_level(s, D_ERROR, CGI_ENGINE "Unable to open to pipe: %s\n", strerror(errno));
		goto error;
	}
	if (pipe(child_out)) {
		smprintf_level(s, D_ERROR, CGI_ENGINE "Unable to open from pipe: %s\n", strerror(errno));
		goto error;
	}
	
	/* ------------------------- Block SIGHUP during the fork - prevents a race */
	//sigfillset(&signal_set);
	//pthread_sigmask(SIG_BLOCK, &signal_set, &old_set);
	//signal(SIGCHLD, cgi_child_end);
	
	
	/* ----------------------------------------------------------- fork the cgi */
	pid = fork();
	if(pid < 0) {
		smprintf_level(s, D_ERROR, CGI_ENGINE "Could not fork: %s\n", strerror(errno));
		// pthread_sigmask(SIG_SETMASK, &old_set, NULL);
		goto error;
	}
	if(!pid) {
		/* -------------------------------------------------- child process */
		/* ------------------------------------ move stdout to child_out[1] */
		close(STDOUT_FILENO);
		dup2(child_out[1], STDOUT_FILENO);
		close(child_out[1]);
		close(child_out[0]);                       /* close unused read end */
		/* -------------------------------------- move stdin to child_in[0] */
		close(STDIN_FILENO);
		dup2(child_in[0], STDIN_FILENO);
		close(child_in[0]);
		close(child_in[1]);                       /* close unused write end */
		cgi_child(s);
	}
	/* --------------------------------------------------------- parent process */
	close(child_out[1]);                              /* close unused write end */
	close(child_in[0]);                                /* close unused read end */
	smprintf_level(s, D_TEXT, CGI_ENGINE "Launched CGI script\n");
	
	/* ----------------------------------------------------------- send headers */
	strcpy(buffer2, "SMS_FROM=");
	DecodeUnicode(sms->Number, buffer2 + sizeof("SMS_FROM=") - 1);
	strcat(buffer2, "\r\n");
	cgi_process_each_helper(s, child_in[1], buffer2, strlen(buffer2));

	strcpy(buffer2, "SMS_NAME=");
	DecodeUnicode(sms->Name, buffer2 + sizeof("SMS_NAME=") - 1);
	strcat(buffer2, "\r\n");
	cgi_process_each_helper(s, child_in[1], buffer2, strlen(buffer2));

	strcpy(buffer2, "SMS_TIME=");
	strcat(buffer2, OSDate(sms->DateTime));
	strcat(buffer2, "\r\n");
	cgi_process_each_helper(s, child_in[1], buffer2, strlen(buffer2));

	/* -------------------------------------------- End headers with empty line */
	cgi_process_each_helper(s, child_in[1], "\r\n", sizeof("\r\n"));

	/* ----------------------------------------------- now we write the command */
	cgi_process_each_helper(s, child_in[1], buffer, strlen(buffer));
	
	close(child_in[1]);                                             /* send EOF */
	
	/* ------------------------------------------------------------ now we read */
	smprintf_level(s, D_TEXT, CGI_ENGINE ">>======== CGI Response ==========\n");
	buffer[0] = '\0';
	offset = 0;
	while((ret = read(child_out[0], buffer + offset, 1)) > 0) {
		offset += ret;
		*(buffer+offset) = '\0';
	}
	smprintf_level(s, D_TEXT, CGI_ENGINE "%s", buffer);
	smprintf_level(s, D_TEXT, "\n>>================================\n");

	do {
		/* ------------------------------------------------- wait for child to exit */
		if((ret = waitpid(pid, &status, WNOHANG)) == -1) {
			smprintf_level(s, D_ERROR, CGI_ENGINE " waitpid failed :(\n");
			goto error;
		}
		if(!ret) {
			smprintf_level(s, D_TEXT, CGI_ENGINE " Child is not dead yet ..\n");
		}
		if(!WIFEXITED(status)) {
			if(WIFSIGNALED(status)) {
				smprintf_level(s, D_ERROR, CGI_ENGINE "killed by signal %d\n", WTERMSIG(status));
			} else if (WIFSTOPPED(status)) {
				smprintf_level(s, D_TEXT, CGI_ENGINE "stopped by signal %d\n", WSTOPSIG(status));
			} else if (WIFCONTINUED(status)) {
				smprintf_level(s, D_TEXT, CGI_ENGINE "continued\n");
			}
		}
	} while(!WIFEXITED(status) && !WIFSIGNALED(status));
	smprintf_level(s, D_TEXT, CGI_ENGINE " Child process exited\n");

	if(buffer[0] != '\0') {
		/* ----------------------------------------------- prepare response */
		GSM_SetDefaultSMSData(&smsSendBuffer);              /* reset memory */
		smsSendBuffer.Location = 0;
		smsSendBuffer.PDU = SMS_Submit;
		CopyUnicodeString(smsSendBuffer.Number, sms->Number);
		smsSendBuffer.Coding = sms->Coding;
		EncodeUnicode(smsSendBuffer.Text, buffer, strlen(buffer));

		/* -------------------------------------------------- send response */
		error = GSM_SendSMS(s, &smsSendBuffer);
	}
	
	error:
		close(child_in[0]);
		close(child_in[1]);
		close(child_out[0]);
		close(child_out[1]);
	
	/* delete the sms when we are done */
	GSM_DeleteSMS(s, sms);
	return;
}

void cgi_enqueue(GSM_StateMachine *s, GSM_SMSMessage sms) {
	int i;
	
	if(sms.Location == -1 || sms.Folder == -1) {
		/* discard invalid message */
		return;
	}
	
	for(i = 0; i<(GSM_MAX_MULTI_SMS - 1) /* keep the last one always empty */; i++) {
		if(smsQ.SMS[i].Location == -1) {
			smsQ.SMS[i].Folder = sms.Folder;
			smsQ.SMS[i].Location = sms.Location;
			smprintf_level(s, D_TEXT, CGI_ENGINE "New message at folder: %d, location : %d\n", sms.Folder, sms.Location);
			break;
		}
		if(
			smsQ.SMS[i].Folder == sms.Folder
			&& smsQ.SMS[i].Location == sms.Location
			) {
			/* it is already in the queue */
			break;
		}
	}
}

void cgi_reset() {
	int i = 0;
	for(i = 0; i<GSM_MAX_MULTI_SMS ; i++) {
		smsQ.SMS[i].Location = -1;
	}
}

void cgi_process(GSM_StateMachine *s) {
	int i = 0;
	for(i = 0; i<GSM_MAX_MULTI_SMS && smsQ.SMS[0].Location != -1; i++) {
		
		/* ----------------------------------------------------- read the message */
		error = GSM_GetSMS(s, &smsQ);
		if(error == ERR_EMPTY) {
			/* somehow the message is empty */
			error = ERR_NONE;
			smprintf_level(s, D_TEXT, CGI_ENGINE "Message was empty\n");
		}
		if(error != ERR_NONE) {
			smprintf_level(s, D_ERROR, CGI_ENGINE "Error while reading sms: %s\n", GSM_ErrorString(error));
		}
		if(error == ERR_NONE) {
			cgi_process_each(s, smsQ.SMS);
		}
		
		/* ------------------------------------------------------- scroll to next */
		memmove(smsQ.SMS, &(smsQ.SMS[1]), (GSM_MAX_MULTI_SMS-i-1)*sizeof(GSM_SMSMessage));
	}
}

/* Editor configuration
 * vim: noexpandtab sw=8 ts=8 sts=8 tw=72:
 */
