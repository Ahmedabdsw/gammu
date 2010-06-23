/* (c) 2002-2003 by Marcin Wiacek */
/* based on some work from Gnokii and MyGnokii */

#ifndef fbus2_h
#define fbus2_h

#include "../protocol.h"

#define FBUS2_FRAME_ID       	0x1e
#define FBUS2_IRDA_FRAME_ID    	0x1c
#define FBUS2_DEVICE_PHONE   	0x00 /* Nokia mobile phone */
#define FBUS2_DEVICE_PC      	0x0c /* Our PC */
#define FBUS2_ACK_BYTE	     	0x7f /* Acknowledge of the received frame */

#define FBUS2_MAX_TRANSMIT_LENGTH 120

typedef struct {
	int			MsgSequenceNumber;
	int			MsgRXState;
	int			FramesToGo;
	GSM_Protocol_Message	MultiMsg;
	GSM_Protocol_Message	Msg;
} GSM_Protocol_FBUS2Data;

#ifndef GSM_USED_SERIALDEVICE
#  define GSM_USED_SERIALDEVICE
#endif
#if defined(GSM_ENABLE_BLUEFBUS2)
#  ifndef GSM_USED_BLUETOOTHDEVICE
#    define GSM_USED_BLUETOOTHDEVICE
#  endif
#endif

#endif

/* How should editor hadle tabs in this file? Add editor commands here.
 * vim: noexpandtab sw=8 ts=8 sts=8:
 */
