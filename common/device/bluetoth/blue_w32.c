/* (c) 2003-2004 by Marcin Wiacek and Intra */

/* To define GUID and not only declare */
#define INITGUID
#include "../../gsmstate.h"

#ifdef GSM_ENABLE_BLUETOOTHDEVICE
#ifdef WIN32

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ole2.h>
#include <io.h>

#include "../../misc/coding/coding.h"
#include "../../gsmcomon.h"
#include "../devfunc.h"
#include "bluetoth.h"
#include "blue_w32.h"

GSM_Error bluetooth_connect(GSM_StateMachine *s, int port, char *device)
{
	GSM_Device_BlueToothData 	*d = &s->Device.Data.BlueTooth;
    	WSADATA				wsaData;
	SOCKADDR_BTH 			sab;
	int				i;

	smprintf(s, "Connecting to RF channel %i\n",port);

    	WSAStartup(MAKEWORD(1,1), &wsaData);

    	d->hPhone = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
	if (d->hPhone == INVALID_SOCKET) {
		i = GetLastError();
		GSM_OSErrorInfo(s, "Socket in bluetooth_open");
		if (i == 10041) return ERR_DEVICENODRIVER;/* unknown socket type */
		return ERR_UNKNOWN;
	}

	memset (&sab, 0, sizeof(sab));
	sab.port 		= port;
	sab.addressFamily 	= AF_BTH;
	sab.btAddr 		= 0;
	for (i=0;i<(int)strlen(device);i++) {
		if (device[i] >='0' && device[i] <='9') {
			sab.btAddr = sab.btAddr * 16;
			sab.btAddr = sab.btAddr + (device[i]-'0');
		}
		if (device[i] >='a' && device[i] <='f') {
			sab.btAddr = sab.btAddr * 16;
			sab.btAddr = sab.btAddr + (device[i]-'a'+10);
		}
		if (device[i] >='A' && device[i] <='F') {
			sab.btAddr = sab.btAddr * 16;
			sab.btAddr = sab.btAddr + (device[i]-'A'+10);
		}
	}
	dbgprintf("Remote Bluetooth device is %04llx%08llx\n",
	  		GET_NAP(sab.btAddr), GET_SAP(sab.btAddr));

	if (connect (d->hPhone, (struct sockaddr *)&sab, sizeof(sab)) != 0) {
		i = GetLastError();
		GSM_OSErrorInfo(s, "Connect in bluetooth_open");
		if (i == 10060) return ERR_TIMEOUT;	 /* remote device failed to respond */
		if (i == 10050) return ERR_DEVICENOTWORK; /* socket operation connected with dead network */
		/* noauth */
		close(d->hPhone);
		return ERR_UNKNOWN;
	}

	return ERR_NONE;
}

#ifdef BLUETOOTH_RF_SEARCHING

DEFINE_GUID(L2CAP_PROTOCOL_UUID,  0x00000100, 0x0000, 0x1000, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB);

#ifndef __GNUC__
#pragma comment(lib, "irprops.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

static GSM_Error bluetooth_checkdevice(GSM_StateMachine *s, char *address, WSAPROTOCOL_INFO *protocolInfo)
{
	int				found = -1;
	int				score, bestscore = 0;
	WSAQUERYSET 			querySet;
	DWORD				flags;
	GUID				protocol;
	int				i, result;
	BYTE 				buffer[2000];
	char 				addressAsString[1000];
	DWORD 				bufferLength, addressSize;
	WSAQUERYSET 			*pResults = (WSAQUERYSET*)&buffer;
	HANDLE				handle;

	memset(&querySet, 0, sizeof(querySet));
	querySet.dwSize 	  = sizeof(querySet);
	protocol 		  = L2CAP_PROTOCOL_UUID;
	querySet.lpServiceClassId = &protocol;
	querySet.dwNameSpace 	  = NS_BTH;
	querySet.lpszContext 	  = address;

	flags = LUP_FLUSHCACHE  | LUP_RETURN_NAME |
		LUP_RETURN_TYPE | LUP_RETURN_ADDR |
		LUP_RETURN_BLOB | LUP_RETURN_COMMENT;

        result = WSALookupServiceBegin(&querySet, flags, &handle);
	if (result != 0) return ERR_UNKNOWN;

	bufferLength = sizeof(buffer);
	while (1) {
                result = WSALookupServiceNext(handle, flags, &bufferLength, pResults);
		if (result != 0) break;
 		addressSize 		= sizeof(addressAsString);
		addressAsString[0] 	= 0;
		if (WSAAddressToString(pResults->lpcsaBuffer->RemoteAddr.lpSockaddr,
			pResults->lpcsaBuffer->RemoteAddr.iSockaddrLength, protocolInfo,
			addressAsString,&addressSize)==0) {
                	smprintf(s, "%s - ", addressAsString);
		}
		score = bluetooth_checkservicename(s, pResults->lpszServiceInstanceName);
		smprintf(s, "\"%s\" (score=%d)\n", pResults->lpszServiceInstanceName, score);
		if (addressAsString[0] != 0) {
			for (i=strlen(addressAsString)-1;i>0;i--) {
				if (addressAsString[i] == ':') break;
			}
			if (score > bestscore) {
				found = atoi(addressAsString+i+1);
			}
		}
	}
	result = WSALookupServiceEnd(handle);
	if (found != -1) {
		return bluetooth_connect(s,found,address+1);
	}
	return ERR_NOTSUPPORTED;
}

GSM_Error bluetooth_findchannel(GSM_StateMachine *s)
{
	GSM_Device_BlueToothData 	*d = &s->Device.Data.BlueTooth;
    	WSADATA				wsaData;
	int				i, protocolInfoSize, result;
	WSAPROTOCOL_INFO 		protocolInfo;
	HANDLE 				handle;
	DWORD				flags;
	WSAQUERYSET 			querySet;
	BYTE 				buffer[2000];
	char 				addressAsString[1000];
	DWORD 				bufferLength, addressSize;
	WSAQUERYSET 			*pResults = (WSAQUERYSET*)&buffer;
	GSM_Error			error;

    	if (WSAStartup(MAKEWORD(2,2), &wsaData)!=0x00) return ERR_DEVICENODRIVER;

    	d->hPhone = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
	if (d->hPhone == INVALID_SOCKET) {
		i = GetLastError();
		GSM_OSErrorInfo(s, "Socket in bluetooth_open");
		if (i == 10041) return ERR_DEVICENODRIVER;/* unknown socket type */
		return ERR_UNKNOWN;
	}

	protocolInfoSize = sizeof(protocolInfo);
      	if (getsockopt(d->hPhone, SOL_SOCKET, SO_PROTOCOL_INFO,
		(char*)&protocolInfo, &protocolInfoSize) != 0)
	{
		close(d->hPhone);
		return ERR_UNKNOWN;
	}
	close(d->hPhone);

	if (!strcmp(s->CurrentConfig->Device,"com2:")) {
		bufferLength = sizeof(buffer);

      		flags = LUP_RETURN_NAME | LUP_CONTAINERS |
			LUP_RETURN_ADDR | LUP_FLUSHCACHE |
			LUP_RETURN_TYPE | LUP_RETURN_BLOB | LUP_RES_SERVICE;

      		memset(&querySet, 0, sizeof(querySet));
      		querySet.dwSize 	= sizeof(querySet);
      		querySet.dwNameSpace 	= NS_BTH;

	        result = WSALookupServiceBegin(&querySet, flags, &handle);
		if (result != 0) return ERR_UNKNOWN;

		while (1) {
			result = WSALookupServiceNext(handle, flags, &bufferLength, pResults);
			if (result != 0) break;

 	                smprintf(s, "\"%s\"", pResults->lpszServiceInstanceName);

	 		addressSize 		= sizeof(addressAsString);
			addressAsString[0] 	= 0;
			if (WSAAddressToString(pResults->lpcsaBuffer->RemoteAddr.lpSockaddr,
				pResults->lpcsaBuffer->RemoteAddr.iSockaddrLength, &protocolInfo,
				addressAsString,&addressSize)==0) {
	                	smprintf(s, " - %s\n", addressAsString);
				error = bluetooth_checkdevice(s, addressAsString,&protocolInfo);
				if (error == ERR_NONE) {
					result = WSALookupServiceEnd(handle);
					return error;
				}
			} else smprintf(s, "\n");
		}

		result = WSALookupServiceEnd(handle);
		return ERR_NOTSUPPORTED;
	} else {
		return bluetooth_checkdevice(s, s->CurrentConfig->Device,&protocolInfo);
	}
}

#endif
#endif
#endif

/* How should editor hadle tabs in this file? Add editor commands here.
 * vim: noexpandtab sw=8 ts=8 sts=8:
 */
