/*
 * python-gammu - Phone communication libary
 * Copyright (c) 2003 - 2006 Michal Čihař
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/* Python includes */
#include <Python.h>

/* Gammu includes */
#include <gammu.h>

/* Locales */
#include <locale.h>

/* For locking */
#ifdef WITH_THREAD
#include "pythread.h"
#endif

/* Convertors between Gammu and Python types */
#include "convertors.h"

/* Error objects */
#include "errors.h"

/* Other useful stuff */
#include "misc.h"

/* Length of buffers used in most of code */
#define BUFFER_LENGTH 255

#define CHECK_EXISTING_CONNECTION \
{ \
    if (!self->s.opened) { \
        checkError(&(self->s), ERR_NOTCONNECTED, ""); \
        return NULL; \
    } \
}

#ifdef WITH_THREAD

/* Use python locking */

#define BEGIN_PHONE_COMM \
    Py_BEGIN_ALLOW_THREADS \
    PyThread_acquire_lock(self->mutex, 1);

#define END_PHONE_COMM \
    PyThread_release_lock(self->mutex); \
    Py_END_ALLOW_THREADS \
    CheckIncomingEvents(self);

#else

/* No need for locking when no threads */
#define BEGIN_PHONE_COMM
#define END_PHONE_COMM \
    CheckIncomingEvents(self);

#endif

PyObject    *DebugFile;

#define MAX_EVENTS 10

/* ----------------------------------------------------- */

/* Declarations for objects of type StateMachine */
typedef struct {
    PyObject_HEAD

    GSM_StateMachine    s;
    PyObject            *DebugFile;
    PyObject            *IncomingCallback;
    volatile GSM_Error  SMSStatus;
    volatile int        MessageReference;
    GSM_Call            *IncomingCallQueue[MAX_EVENTS + 1];
    GSM_SMSMessage      *IncomingSMSQueue[MAX_EVENTS + 1];
    GSM_CBMessage       *IncomingCBQueue[MAX_EVENTS + 1];
    char                *IncomingUSSDQueue[MAX_EVENTS + 1];
    GSM_MemoryType      memory_entry_cache_type;
    int                 memory_entry_cache;
    int                 todo_entry_cache;
    int                 calendar_entry_cache;
#ifdef WITH_THREAD
    PyThread_type_lock mutex;
#endif
} StateMachineObject;


/* Forward declaration */
static PyTypeObject StateMachineType;

/* ---------------------------------------------------------------- */

/*
 * StateMachine list handling. This is required to find StateMachine object
 * corresponding to device which is used in incoming events notifications.
 */

/**
 * How many state machines can we handle?
 */
#define MAX_STATEMACHINES 128

/**
 * List of registered state machines.
 */
StateMachineObject *AllStateMachines[MAX_STATEMACHINES + 1];
#ifdef WITH_THREAD
/**
 * Lock for access to AllStateMachines.
 */
PyThread_type_lock AllStateMachinesMutex;

/* Macros for locking array of state machines */
#define LOCK_STATEMACHINES PyThread_acquire_lock(AllStateMachinesMutex, 1); {
#define UNLOCK_STATEMACHINES  } PyThread_release_lock(AllStateMachinesMutex);
#else
#define LOCK_STATEMACHINES
#define UNLOCK_STATEMACHINES
#endif

/**
 * Function for registering state machine into list. This is done on its
 * creation.
 */
static int RegisterStateMachine(StateMachineObject *sm) {
    int i = 0;

    LOCK_STATEMACHINES;

    while (AllStateMachines[i] != NULL) i++;
    if (i == MAX_STATEMACHINES) {
        PyErr_Format(PyExc_OverflowError, "Too much state machines allocated, increase MAX_STATEMACHINES and recompile python-gammu.");
        return 0;
    }

    AllStateMachines[i] = sm;

    UNLOCK_STATEMACHINES;
    return 1;
}

/**
 * Function for unregistering state machine.
 */
static int UnRegisterStateMachine(StateMachineObject *sm) {
    int i = 0;

    LOCK_STATEMACHINES;

    while (i < MAX_STATEMACHINES && AllStateMachines[i] != sm) i++;
    if (i == MAX_STATEMACHINES) {
        PyErr_Format(PyExc_LookupError, "Could not find StateMachine to deallocate, something is definitely wrong!");
        return 0;
    }

    AllStateMachines[i] = NULL;

    UNLOCK_STATEMACHINES;
    return 1;
}

/**
 * State machine lookup by device name (actually by pointer to that name).
 */
static StateMachineObject *FindStateMachine(const char *dev) {
    int i = 0;

    LOCK_STATEMACHINES;

    while (i < MAX_STATEMACHINES && AllStateMachines[i]->s.CurrentConfig->Device != dev) i++;
    if (i == MAX_STATEMACHINES) {
        /* FIXME: This should be exception */
        pyg_error("Failed to lookup state machine proxy for device %s\n", dev);
        return NULL;
    }

    UNLOCK_STATEMACHINES;
    return AllStateMachines[i];
}

/*
 * Callbacks follow. Those are common for all state machines and thus need to
 * lookup correct state machine.
 */

/**
 * SMS sending status callback, we handle this ourself and this is not
 * published.
 */
static void SendSMSStatus (char *Device, int status, int mr) {
    StateMachineObject *sm;
    sm = FindStateMachine(Device);
    if (sm == NULL) return;

    sm->MessageReference = mr;
	if (status==0) {
		sm->SMSStatus = ERR_NONE;
	} else {
		sm->SMSStatus = ERR_UNKNOWN;
	}
}

/**
 * Incoming call callback.
 */
static void IncomingCall (char *Device, GSM_Call call) {
    StateMachineObject  *sm;
    int i = 0;

    sm = FindStateMachine(Device);
    if (sm == NULL) return;

    while (i < MAX_EVENTS && sm->IncomingCallQueue[i] != NULL) i++;

    if (i == MAX_EVENTS) {
        pyg_error("Incoming call queue overflow!\n");
        return;
    }

    sm->IncomingCallQueue[i] = (GSM_Call *)malloc(sizeof(GSM_Call));
    if (sm->IncomingCallQueue[i] == NULL) return;

    *(sm->IncomingCallQueue[i]) = call;
}

/**
 * Incoming SMS callback.
 */
static void IncomingSMS (char *Device, GSM_SMSMessage msg) {
    StateMachineObject  *sm;
    int i = 0;

    sm = FindStateMachine(Device);
    if (sm == NULL) return;

    while (i < MAX_EVENTS && sm->IncomingSMSQueue[i] != NULL) i++;

    if (i == MAX_EVENTS) {
        pyg_error("Incoming SMS queue overflow!\n");
        return;
    }

    sm->IncomingSMSQueue[i] = (GSM_SMSMessage *)malloc(sizeof(GSM_SMSMessage));
    if (sm->IncomingSMSQueue[i] == NULL) return;

    *(sm->IncomingSMSQueue[i]) = msg;
}

/**
 * Incoming CB callback.
 */
static void IncomingCB (char *Device, GSM_CBMessage cb) {
    StateMachineObject  *sm;
    int i = 0;

    sm = FindStateMachine(Device);
    if (sm == NULL) return;

    while (i < MAX_EVENTS && sm->IncomingCBQueue[i] != NULL) i++;

    if (i == MAX_EVENTS) {
        pyg_error("Incoming CB queue overflow!\n");
        return;
    }

    sm->IncomingCBQueue[i] = (GSM_CBMessage *)malloc(sizeof(GSM_CBMessage));
    if (sm->IncomingCBQueue[i] == NULL) return;

    *(sm->IncomingCBQueue[i]) = cb;
}

/**
 * Incoming USSD callback.
 */
static void IncomingUSSD (char *Device, char *Text) {
    StateMachineObject  *sm;
    int i = 0;

    sm = FindStateMachine(Device);
    if (sm == NULL) return;

    while (i < MAX_EVENTS && sm->IncomingUSSDQueue[i] != NULL) i++;

    if (i == MAX_EVENTS) {
        pyg_error("Incoming USSD queue overflow!\n");
        return;
    }

    sm->IncomingUSSDQueue[i] = strdup(Text);
    if (sm->IncomingUSSDQueue[i] == NULL) return;
}

/**
 * Process queue of incoming events from phone.
 */
static void CheckIncomingEvents(StateMachineObject *sm) {
    int                 i;
    PyObject            *arglist;
    PyObject            *event;

    if (sm->IncomingCallQueue[0] != NULL) {
        event = CallToPython(sm->IncomingCallQueue[0]);
        if (event == NULL) {
            PyErr_Clear();
            pyg_error("Discarding incoming event due to conversion error!\n");
            return;
        }

        arglist = Py_BuildValue("(OsO)", sm, "Call", event);
        Py_DECREF(event);

        /* Shift queue */
        i = 0;
        do {
            sm->IncomingCallQueue[i] = sm->IncomingCallQueue[i + 1];
            i++;
        } while (sm->IncomingCallQueue[i] != NULL && i < MAX_EVENTS);

        if (arglist == NULL) {
            PyErr_Clear();
            pyg_error("Discarding incoming event due to conversion error!\n");
            return;
        }

        PyEval_CallObject(sm->IncomingCallback, arglist);

        Py_DECREF(arglist);

        free(sm->IncomingCallQueue[0]);
    }

    if (sm->IncomingSMSQueue[0] != NULL) {
        /* Did we get full message on location only? */
        if (sm->IncomingSMSQueue[0]->State == 0) {
            /* Location only */
            event = Py_BuildValue("{s:i,s:i,s:i}",
                        "Location", sm->IncomingSMSQueue[0]->Location,
                        "Folder", sm->IncomingSMSQueue[0]->Folder,
                        "InboxFolder", (int)sm->IncomingSMSQueue[0]->InboxFolder
                    );
        } else {
            event = SMSToPython(sm->IncomingSMSQueue[0]);
            if (event == NULL) {
                PyErr_Clear();
                pyg_error("Discarding incoming event due to conversion error!\n");
                return;
            }
        }

        /* Shift queue */
        i = 0;
        do {
            sm->IncomingSMSQueue[i] = sm->IncomingSMSQueue[i + 1];
            i++;
        } while (sm->IncomingSMSQueue[i] != NULL && i < MAX_EVENTS);

        arglist = Py_BuildValue("(OsO)", sm, "SMS", event);
        Py_DECREF(event);
        if (arglist == NULL) {
            PyErr_Clear();
            pyg_error("Discarding incoming event due to conversion error!\n");
            return;
        }

        PyEval_CallObject(sm->IncomingCallback, arglist);

        Py_DECREF(arglist);

        free(sm->IncomingSMSQueue[0]);
    }

    if (sm->IncomingCBQueue[0] != NULL) {
        event = CBToPython(sm->IncomingCBQueue[0]);
        if (event == NULL) {
            PyErr_Clear();
            pyg_error("Discarding incoming event due to conversion error!\n");
            return;
        }

        arglist = Py_BuildValue("(OsO)", sm, "CB", event);
        Py_DECREF(event);

        /* Shift queue */
        i = 0;
        do {
            sm->IncomingCBQueue[i] = sm->IncomingCBQueue[i + 1];
            i++;
        } while (sm->IncomingCBQueue[i] != NULL && i < MAX_EVENTS);

        if (arglist == NULL) {
            PyErr_Clear();
            pyg_error("Discarding incoming event due to conversion error!\n");
            return;
        }

        PyEval_CallObject(sm->IncomingCallback, arglist);

        Py_DECREF(arglist);

        free(sm->IncomingCBQueue[0]);
    }

    if (sm->IncomingUSSDQueue[0] != NULL) {
        arglist = Py_BuildValue("(Oss)", sm, "USSD", sm->IncomingUSSDQueue[0]);
        if (arglist == NULL) {
            PyErr_Clear();
            pyg_error("Discarding incoming event due to conversion error!\n");
            return;
        }

        PyEval_CallObject(sm->IncomingCallback, arglist);

        Py_DECREF(arglist);

        free(sm->IncomingUSSDQueue[0]);

        /* Shift queue */
        i = 0;
        do {
            sm->IncomingUSSDQueue[i] = sm->IncomingUSSDQueue[i + 1];
            i++;
        } while (sm->IncomingUSSDQueue[i] != NULL && i < MAX_EVENTS);
    }
}

/* ---------------------------------------------------------------- */

static char StateMachine_GetConfig__doc__[] =
"GetConfig(Section)\n\n"
"Gets specified config section. Configuration consists of all params which can be defined in gammurc config file:\n"
" - Model\n"
" - DebugLevel\n"
" - Device\n"
" - Connection\n"
" - SyncTime\n"
" - LockDevice\n"
" - DebugFile\n"
" - Localize\n"
" - StartInfo\n"
" - UseGlobalDebugFile\n"
"\n"
"@param Section: Index of config section to read. Defaults to 0.\n"
"@type Section: int\n"
"@return: Hash containing configuration\n"
;

static PyObject *
StateMachine_GetConfig(StateMachineObject *self, PyObject *args, PyObject *kwds)
{
    int             section = 0;
    static char     *kwlist[] = {"Section", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|I", kwlist, &section))
        return NULL;

    if (self->s.ConfigNum <= section) {
        PyErr_Format(PyExc_ValueError, "Requested configuration not available (requested: %d, available: %d)", section, self->s.ConfigNum);
        return NULL;
    }

    return Py_BuildValue("{s:s,s:s,s:s,s:s,s:s,s:s,s:s,s:s,s:s,s:i}",
        "Model", self->s.Config[section].Model,
        "DebugLevel", self->s.Config[section].DebugLevel,
        "Device", self->s.Config[section].Device,
        "Connection", self->s.Config[section].Connection,
        "SyncTime", self->s.Config[section].SyncTime,
        "LockDevice", self->s.Config[section].LockDevice,
        "DebugFile", self->s.Config[section].DebugFile,
        "Localize", self->s.Config[section].Localize,
        "StartInfo", self->s.Config[section].StartInfo,
        "UseGlobalDebugFile", self->s.Config[section].UseGlobalDebugFile);
}

static char StateMachine_SetConfig__doc__[] =
"SetConfig(Section, Values)\n\n"
"Sets specified config section.\n\n"
"@param Section: Index of config section to modify\n"
"@type Section: int\n"
"@param Values: Config values, see L{GetConfig} for description of accepted\n"
"@type Values: hash\n"
"@return: None\n"
;

static PyObject *
StateMachine_SetConfig(StateMachineObject *self, PyObject *args, PyObject *kwds)
{
    int             section = 0;
    static char     *kwlist[] = {"Section", "Values", NULL};
    PyObject        *key, *value, *str;
    PyObject        *dict;
    char            *s, *v, *setv;
    int pos = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "IO!", kwlist,
                &section, &PyDict_Type, &dict))
        return NULL;

    if (MAX_CONFIG_NUM <= section) {
        PyErr_Format(PyExc_ValueError, "Requested configuration not available (requested: %d, available: %d)", section, MAX_CONFIG_NUM);
        return NULL;
    }
    self->s.ConfigNum = section + 1;

    while (PyDict_Next(dict, &pos, &key, &value)) {
        s = PyString_AsString(key);
        if (s == NULL) {
            PyErr_Format(PyExc_ValueError, "Non string key in configuration values");
            return NULL;
        }
        if (strcmp(s, "UseGlobalDebugFile") == 0) {
            if (!PyInt_Check(value)) {
                PyErr_Format(PyExc_ValueError, "Non integer value for UseGlobalDebugFile");
                return NULL;
            }
            self->s.Config[section].UseGlobalDebugFile = PyInt_AsLong(value);
        } else {
            if (PyString_Check(value) || PyUnicode_Check(value)) {
                if (PyUnicode_Check(value)) {
                    str = PyUnicode_AsASCIIString(value);
                    if (str == NULL) {
                        PyErr_Format(PyExc_ValueError, "Non string value for %s", s);
                        return NULL;
                    }
                } else {
                    str = value;
                    Py_INCREF(str);
                }
                v = PyString_AsString(value);
                if (v == NULL) {
                    PyErr_Format(PyExc_ValueError, "Non string value for %s", s);
                    return NULL;
                } else {
                    setv = strdup(v);
                }
                Py_DECREF(str);
            } else {
                v = NULL;
                if (value == Py_None) {
                    setv = NULL;
                } else {
                    PyErr_Format(PyExc_ValueError, "Non string value for %s", s);
                    return NULL;
                }
            }
            if (strcmp(s, "Model") == 0) {
                free(setv);
                if (v == NULL) {
                    self->s.Config[section].Model[0] = 0;
                } else {
                    mystrncpy(self->s.Config[section].Model, v, sizeof(self->s.Config[section].Model) - 1);
                }
            } else if (strcmp(s, "DebugLevel") == 0) {
                free(setv);
                if (v == NULL) {
                    self->s.Config[section].DebugLevel[0] = 0;
                } else {
                    mystrncpy(self->s.Config[section].DebugLevel, v, sizeof(self->s.Config[section].DebugLevel) - 1);
                }
            } else if (strcmp(s, "Device") == 0) {
                free(self->s.Config[section].Device);
                self->s.Config[section].Device = setv;
            } else if (strcmp(s, "Connection") == 0) {
                free(self->s.Config[section].Connection);
                self->s.Config[section].Connection = setv;
            } else if (strcmp(s, "SyncTime") == 0) {
                free(self->s.Config[section].SyncTime);
                self->s.Config[section].SyncTime = setv;
            } else if (strcmp(s, "LockDevice") == 0) {
                free(self->s.Config[section].LockDevice);
                self->s.Config[section].LockDevice = setv;
            } else if (strcmp(s, "DebugFile") == 0) {
                free(self->s.Config[section].DebugFile);
                self->s.Config[section].DebugFile = setv;
            } else if (strcmp(s, "Localize") == 0) {
                free(self->s.Config[section].Localize);
                self->s.Config[section].Localize = setv;
            } else if (strcmp(s, "StartInfo") == 0) {
                free(self->s.Config[section].StartInfo);
                self->s.Config[section].StartInfo = setv;
            } else {
                free(setv);
                PyErr_Format(PyExc_ValueError, "Uknown key: %s", s);
                return NULL;
            }
        }
    }
    Py_RETURN_NONE;
}



static char StateMachine_ReadConfig__doc__[] =
"ReadConfig(Section, Configuration)\n\n"
"Reads specified section of gammurc\n\n"
"@param Section: Index of config section to read. Defaults to 0.\n"
"@type Section: int\n"
"@param Configuration: Index where config section will be stored. Defaults to Section.\n"
"@type Configuration: int\n"
"@return: None\n"
;

static PyObject *
StateMachine_ReadConfig(StateMachineObject *self, PyObject *args, PyObject *kwds)
{
    int             section = 0;
    int             dst = -1;
    INI_Section     *cfg;

    static char         *kwlist[] = {"Section", "Configuration", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|II", kwlist, &section, &dst))
        return NULL;

    if (dst == -1) dst = section;
    if (dst >= MAX_CONFIG_NUM) {
        PyErr_Format(PyExc_ValueError, "Maximal configuration storage is %d", MAX_CONFIG_NUM);
        return NULL;
    }

    cfg = GSM_FindGammuRC();
    if (cfg == NULL) {
        PyErr_SetString(PyExc_IOError, "Can not find gammurc");
        return NULL;
    }

    if (!GSM_ReadConfig(cfg, &(self->s.Config[dst]), section)) {
        INI_Free(cfg);
        PyErr_SetString(PyExc_IOError, "Can not read config");
        return NULL;
    }
    self->s.Config[dst].UseGlobalDebugFile = false;

    self->s.ConfigNum = dst + 1;

    INI_Free(cfg);

    Py_RETURN_NONE;
}


static char StateMachine_Init__doc__[] =
"Init(Replies)\n\n"
"Initialises the connection with phone.\n\n"
"@param Replies: Number of replies to wait for on each request. Defaults to 3.\n"
"@type Replies: int\n"
"@return: None\n"
;

static PyObject *
StateMachine_Init(StateMachineObject *self, PyObject *args, PyObject *kwds)
{
    GSM_Error           error;
    int                 replies = 3;
    static char         *kwlist[] = {"Replies", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|I", kwlist, &replies))
        return NULL;

    BEGIN_PHONE_COMM
    error = GSM_InitConnection(&(self->s), replies);
    END_PHONE_COMM
    if (!checkError(&(self->s), error, "Init"))
        return NULL;

    /* Set callbacks */
    self->s.User.IncomingCall = IncomingCall;
    self->s.User.IncomingSMS = IncomingSMS;
    self->s.User.IncomingCB = IncomingCB;
    self->s.User.IncomingUSSD = IncomingUSSD;
    self->s.User.SendSMSStatus = SendSMSStatus;

    /* No cached data */
    self->memory_entry_cache_type = 0;
    self->memory_entry_cache = 1;
    self->todo_entry_cache = 1;
    self->calendar_entry_cache = 1;

    Py_RETURN_NONE;
}


static char StateMachine_Terminate__doc__[] =
"Terminate()\n\n"
"Terminates the connection with phone.\n\n"
"@return: None\n"
;

static PyObject *
StateMachine_Terminate(StateMachineObject *self, PyObject *args, PyObject *kwds)
{
    GSM_Error           error;

    /* Remove callbacks */
    self->s.User.IncomingCall = NULL;
    self->s.User.IncomingSMS = NULL;
    self->s.User.IncomingCB = NULL;
    self->s.User.IncomingUSSD = NULL;
    self->s.User.SendSMSStatus = NULL;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = GSM_TerminateConnection(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "Terminate")) return NULL;

    Py_RETURN_NONE;
}


/*****************/
/* ShowStartInfo */
/*****************/

static char StateMachine_ShowStartInfo__doc__[] =
"ShowStartInfo(Enable)\n\n"
"Enables showing information on phone display.\n\n"
"@param Enable: Whether to enable. Defaults to True.\n"
"@type Enable: bool\n"
"@return: None\n"
;

static PyObject *
StateMachine_ShowStartInfo(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    int                 enable = true;
    static char         *kwlist[] = {"Enable", NULL};

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|I", kwlist, &enable))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->ShowStartInfo(&(self->s), enable);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "ShowStartInfo")) return NULL;

    Py_RETURN_NONE;
}

/*******************/
/* GetManufacturer */
/*******************/

static char StateMachine_GetManufacturer__doc__[] =
"GetManufacturer()\n\n"
"Reads manufacturer from phone.\n\n"
"@return: String with manufacturer name\n"
;

static PyObject *
StateMachine_GetManufacturer(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetManufacturer(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetManufacturer")) return NULL;

    return Py_BuildValue("s", self->s.Phone.Data.Manufacturer);
}

/************/
/* GetModel */
/************/

static char StateMachine_GetModel__doc__[] =
"GetModel()\n\n"
"Reads model from phone.\n\n"
"@return: Tuple containing gammu identification and real model returned by phone.\n"
;

static PyObject *
StateMachine_GetModel(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetModel(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetModel")) return NULL;

    return Py_BuildValue("ss", self->s.Phone.Data.ModelInfo->model, self->s.Phone.Data.Model);
}

/***************/
/* GetFirmware */
/***************/

static char StateMachine_GetFirmware__doc__[] =
"GetFirmware()\n\n"
"Reads firmware information from phone.\n\n"
"@return: Tuple from version, date and numeric version.\n"
;

static PyObject *
StateMachine_GetFirmware(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetFirmware(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetFirmware")) return NULL;

   return Py_BuildValue("(s,s,d)",
            self->s.Phone.Data.Version,
            self->s.Phone.Data.VerDate,
            self->s.Phone.Data.VerNum
            );
}

/***********/
/* GetIMEI */
/***********/

static char StateMachine_GetIMEI__doc__[] =
"GetIMEI()\n\n"
"Reads IMEI/serial number from phone.\n\n"
"@return: IMEI of phone as string.\n"
;

static PyObject *
StateMachine_GetIMEI(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetIMEI(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetIMEI")) return NULL;

    return Py_BuildValue("s", self->s.Phone.Data.IMEI);
}

/*******************/
/* GetOriginalIMEI */
/*******************/

static char StateMachine_GetOriginalIMEI__doc__[] =
"GetOriginalIMEI()\n\n"
"Gets original IMEI from phone.\n\n"
"@return: Original IMEI of phone as string.\n"
;

static PyObject *
StateMachine_GetOriginalIMEI(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    char                buffer[BUFFER_LENGTH];

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetOriginalIMEI(&(self->s), buffer);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetOriginalIMEI")) return NULL;

    return Py_BuildValue("s", buffer);
}

/***********************/
/* GetManufactureMonth */
/***********************/

static char StateMachine_GetManufactureMonth__doc__[] =
"GetManufactureMonth()\n\n"
"Gets month when device was manufactured.\n\n"
"@return: Month of manufacture as string.\n"
;

static PyObject *
StateMachine_GetManufactureMonth(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    char                buffer[BUFFER_LENGTH];

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetManufactureMonth(&(self->s), buffer);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetManufactureMonth")) return NULL;

    return Py_BuildValue("s", buffer);
}

/******************/
/* GetProductCode */
/******************/

static char StateMachine_GetProductCode__doc__[] =
"GetProductCode()\n\n"
"Gets product code of device.\n"
"@return: Product code as string.\n"
;

static PyObject *
StateMachine_GetProductCode(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    char                buffer[BUFFER_LENGTH];

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetProductCode(&(self->s), buffer);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetProductCode")) return NULL;

    return Py_BuildValue("s", buffer);
}

/***************/
/* GetHardware */
/***************/

static char StateMachine_GetHardware__doc__[] =
"GetHardware()\n\n"
"Gets hardware information about device.\n\n"
"@return: Hardware information as string.\n"
;

static PyObject *
StateMachine_GetHardware(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    char                buffer[BUFFER_LENGTH];

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetHardware(&(self->s), buffer);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetHardware")) return NULL;

    return Py_BuildValue("s", buffer);
}

/**********/
/* GetPPM */
/**********/

static char StateMachine_GetPPM__doc__[] =
"GetPPM()\n\n"
"Gets PPM (Post Programmable Memory) from phone.\n\n"
"@return: PPM as string\n"
;

static PyObject *
StateMachine_GetPPM(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    char                buffer[BUFFER_LENGTH];

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetPPM(&(self->s), buffer);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetPPM")) return NULL;

    return Py_BuildValue("s", buffer);
}

/**************/
/* GetSIMIMSI */
/**************/

static char StateMachine_GetSIMIMSI__doc__[] =
"GetSIMIMSI()\n\n"
"Gets SIM IMSI from phone.\n\n"
"@return: SIM IMSI as string\n"
;

static PyObject *
StateMachine_GetSIMIMSI(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    char                buffer[BUFFER_LENGTH];

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetSIMIMSI(&(self->s), buffer);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetSIMIMSI")) return NULL;

    return Py_BuildValue("s", buffer);
}

/***************/
/* GetDateTime */
/***************/

static char StateMachine_GetDateTime__doc__[] =
"GetDateTime()\n\n"
"Reads date and time from phone.\n\n"
"@return: Date and time from phone as datetime.datetime object.\n"
;

static PyObject *
StateMachine_GetDateTime(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_DateTime        dt;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetDateTime(&(self->s), &dt);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetDateTime")) return NULL;

    return BuildPythonDateTime(&dt);
}

/***************/
/* SetDateTime */
/***************/

static char StateMachine_SetDateTime__doc__[] =
"SetDateTime(Date)\n\n"
"Sets date and time in phone.\n\n"
"@param Date: Date to set\n"
"@type Date: datetime.datetime\n"
"@return: None\n"
;

static PyObject *
StateMachine_SetDateTime(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    PyObject            *pydt;
    GSM_DateTime        dt;
    static char         *kwlist[] = {"Date", NULL};

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O", kwlist,&(pydt)))
        return NULL;

    if (!BuildGSMDateTime(pydt, &dt)) return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetDateTime(&(self->s), &dt);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetDateTime")) return NULL;

    Py_RETURN_NONE;
}

/************/
/* GetAlarm */
/************/

static char StateMachine_GetAlarm__doc__[] =
"GetAlarm(Location)\n\n"
"Reads alarm set in phone.\n\n"
"@param Location: Which alarm to read. Many phone support only one alarm. Default is 1.\n"
"@type Location: int\n"
"@return: Alarm hash\n"
;

static PyObject *
StateMachine_GetAlarm(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_Alarm           alarm;
    PyObject            *dt;
    Py_UNICODE          *s;
    PyObject            *result;
    static char         *kwlist[] = {"Location", NULL};

    alarm.Location = 1;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|I", kwlist, &(alarm.Location)))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetAlarm(&(self->s), &alarm);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetAlarm")) return NULL;

    s = strGammuToPython(alarm.Text);
    if (s == NULL) return NULL;

    if (alarm.Repeating) {
        dt = BuildPythonTime(&(alarm.DateTime));
        if (dt == NULL) return NULL;

        result = Py_BuildValue("{s:i,s:O,s:i,s:u}",
                "Location", alarm.Location,
                "DateTime", dt,
                "Repeating", alarm.Repeating,
                "Text", s);
        Py_DECREF(dt);
    } else {
        dt = BuildPythonTime(&(alarm.DateTime));
        if (dt == NULL) return NULL;

        result = Py_BuildValue("{s:i,s:O,s:i,s:u}",
                "Location", alarm.Location,
                "Time", dt,
                "Repeating", alarm.Repeating,
                "Text", s);
        Py_DECREF(dt);
    }
    free(s);
    return result;
}

/************/
/* SetAlarm */
/************/

static char StateMachine_SetAlarm__doc__[] =
"SetAlarm(DateTime, Location, Repeating, Text)\n\n"
"Sets alarm in phone.\n"
"@param DateTime: When should alarm happen.\n"
"@type DateTime: datetime.datetime\n"
"@param Location: Location of alarm to set. Defaults to 1.\n"
"@type Location: int\n"
"@param Repeating: Whether alarm should be repeating. Defaults to True.\n"
"@type Repeating: bool\n"
"@param Text: Text to be displayed on alarm. Defaults to empty.\n"
"@type Text: string\n"
"@return: None\n"
;

static PyObject *
StateMachine_SetAlarm(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_Alarm           alarm;
    PyObject            *pydt;
    PyObject            *s = NULL;
    unsigned char       *gs;
    static char         *kwlist[] = {"DateTime", "Location", "Repeating", "Text", NULL};

    alarm.Location = 1;
    alarm.Text[0] = 0;
    alarm.Repeating = true;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|IIO", kwlist,
                &pydt, &(alarm.Location), &(alarm.Repeating), &(s)))
        return NULL;

    if (s != NULL) {
        if (!PyString_Check(s) && !PyUnicode_Check(s)) {
            PyErr_Format(PyExc_ValueError, "Text not string nor unicode!");
            return NULL;
        }
        gs = StringPythonToGammu(s);
        if (gs == NULL) return NULL;

        if (UnicodeLength(gs) > MAX_CALENDAR_TEXT_LENGTH) {
            pyg_warning("Alarm text too long, truncating to %d (from %zd)\n", MAX_CALENDAR_TEXT_LENGTH, UnicodeLength(gs));
        }
        CopyUnicodeString(alarm.Text, gs);
        free(gs);
    }

    if (alarm.Repeating) {
        if (!BuildGSMDateTime(pydt, &(alarm.DateTime))) return NULL;
    } else {
        if (!BuildGSMTime(pydt, &(alarm.DateTime))) return NULL;
    }

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetAlarm(&(self->s), &alarm);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetAlarm")) return NULL;

    Py_RETURN_NONE;
}

/*************/
/* GetLocale */
/*************/

static char StateMachine_GetLocale__doc__[] =
"GetLocale()\n\n"
"Gets locale information from phone.\n\n"
"@return: Hash of locale settings. L{SetLocale} lists them all.\n"
;

static PyObject *
StateMachine_GetLocale(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_Locale          locale;
    char                s[2];

    static char *dtfmts[] = {"", "DDMMYYYY", "MMDDYYYY", "YYYYMMDD"};

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetLocale(&(self->s), &locale);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetLocale")) return NULL;

    s[0] = locale.DateSeparator;
    s[1] = 0;

    return Py_BuildValue("{s:s,s:s,s:i}",
            "DateSeparator", s,
            "DateFormat", dtfmts[locale.DateFormat],
            "AMPMTime", locale.AMPMTime);
}

/*************/
/* SetLocale */
/*************/

static char StateMachine_SetLocale__doc__[] =
"SetLocale(DateSeparator, DateFormat, AMPMTime)\n\n"
"Sets locale of phone.\n\n"
"@param DateSeparator: Date separator.\n"
"@type DateSeparator: string\n"
"@param DateFormat: Date format, one of 'DDMMYYYY', 'MMDDYYYY', 'YYYYMMDD'\n"
"@type DateFormat: string\n"
"@param AMPMTime: Whether to use AM/PM time.\n"
"@type AMPMTime: bool\n"
"@return: None\n"
;

static PyObject *
StateMachine_SetLocale(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_Locale          locale;
    char                *ds;
    char                *df;
    int                 ampm;
    static char         *kwlist[] = {"DateSeparator", "DateFormat", "AMPMTime", NULL};

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "ssi", kwlist,
                &ds, &df, &ampm))
        return NULL;

    if (strcmp(df, "DDMMYYYY") == 0) locale.DateFormat = GSM_Date_DDMMYYYY;
    else if (strcmp(df, "MMDDYYYY") == 0) locale.DateFormat = GSM_Date_MMDDYYYY;
    else if (strcmp(df, "YYYYMMDD") == 0) locale.DateFormat = GSM_Date_YYYYMMDD;
    else {
        PyErr_Format(PyExc_ValueError, "Bad value for DateFormat: '%s'", df);
        return NULL;
    }
    if (strlen(ds) != 1) {
        PyErr_Format(PyExc_ValueError, "Bad value for DateSeparator: '%s'", ds);
        return NULL;
    }
    locale.DateSeparator = ds[0];
    locale.AMPMTime = ampm;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetLocale(&(self->s), &locale);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetLocale")) return NULL;

    Py_RETURN_NONE;
}

/************/
/* PressKey */
/************/

static char StateMachine_PressKey__doc__[] =
"PressKey(Key, Press)\n\n"
"Emulates key press.\n\n"
"@param Key: What key to press\n"
"@type Key: string\n"
"@param Press: Whether to emulate press or release.\n"
"@type Press: bool\n"
"@return: None\n"
;

static PyObject *
StateMachine_PressKey(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    int                 press = 1;
    char                *key;
    static char         *kwlist[] = {"Key", "Press", NULL};
    GSM_KeyCode         KeyCode[1];
    int                 Length;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|i", kwlist,
                &key, &press))
        return NULL;

    if (strlen(key) != 1) {
        PyErr_Format(PyExc_ValueError, "Bad value for Key: '%s'", key);
        return NULL;
    }

    error = MakeKeySequence(key,  KeyCode, &Length);
    if (!checkError(&(self->s), error, "MakeKeySequence")) return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->PressKey(&(self->s), KeyCode[0], press);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "PressKey")) return NULL;

    Py_RETURN_NONE;
}

/*********/
/* Reset */
/*********/

static char StateMachine_Reset__doc__[] =
"Reset(Hard)\n\n"
"Performs phone reset.\n\n"
"@param Hard: Whether to make hard reset\n"
"@type Hard: bool\n"
"@return: None\n"
;

static PyObject *
StateMachine_Reset(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    int                 hard = 0;
    static char         *kwlist[] = {"Hard", NULL};

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|i", kwlist,
                &hard))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->Reset(&(self->s), hard);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "Reset")) return NULL;

    Py_RETURN_NONE;
}

/**********************/
/* ResetPhoneSettings */
/**********************/

static char StateMachine_ResetPhoneSettings__doc__[] =
"ResetPhoneSettings(Type)\n\n"
"Resets phone settings.\n\n"
"@param Type: What to reset, one of 'PHONE', 'UIF', 'ALL', 'DEV', 'FACTORY'\n"
"@type Type: string\n"
"@return: None\n"
;

static PyObject *
StateMachine_ResetPhoneSettings(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    char                *s;
    static char         *kwlist[] = {"Type", NULL};
    GSM_ResetSettingsType Type;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist,
                &s))
        return NULL;

    if (strcmp(s, "PHONE") == 0)        Type = GSM_RESET_PHONESETTINGS;
    else if (strcmp(s, "UIF") == 0)     Type = GSM_RESET_USERINTERFACE;
    else if (strcmp(s, "ALL") == 0)     Type = GSM_RESET_USERINTERFACE_PHONESETTINGS;
    else if (strcmp(s, "DEV") == 0)     Type = GSM_RESET_DEVICE;
    else if (strcmp(s, "FACTORY") == 0) Type = GSM_RESET_FULLFACTORY;
    else {
        PyErr_Format(PyExc_ValueError, "Bad value for Type: '%s'", s);
        return NULL;
    }

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->ResetPhoneSettings(&(self->s), Type);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "ResetPhoneSettings")) return NULL;

    Py_RETURN_NONE;
}

/*********************/
/* EnterSecurityCode */
/*********************/

static char StateMachine_EnterSecurityCode__doc__[] =
"EnterSecurityCode(Type, Code)\n\n"
"Entres security code.\n"
"@param Type: What code to enter, one of 'PIN', 'PUK', 'PIN2', 'PUK2'.\n"
"@type Type: string\n"
"@param Code: Code value\n"
"@type Code: string\n"
"@return: None\n"
;

static PyObject *
StateMachine_EnterSecurityCode(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_SecurityCode    Code;
    char                *s;
    char                *code;
    static char         *kwlist[] = {"Type", "Code", NULL};

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "ss", kwlist,
                &s, &code))
        return NULL;

    if (strcmp(s, "PIN") == 0)          Code.Type = SEC_Pin;
    else if (strcmp(s, "PUK") == 0)     Code.Type = SEC_Puk;
    else if (strcmp(s, "PIN2") == 0)    Code.Type = SEC_Pin2;
    else if (strcmp(s, "PUK2") == 0)    Code.Type = SEC_Puk2;
    else {
        PyErr_Format(PyExc_ValueError, "Bad value for Type: '%s'", s);
        return NULL;
    }

    mystrncpy(Code.Code, code, GSM_SECURITY_CODE_LEN);

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->EnterSecurityCode(&(self->s), Code);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "EnterSecurityCode")) return NULL;

    Py_RETURN_NONE;
}

/*********************/
/* GetSecurityStatus */
/*********************/

static char StateMachine_GetSecurityStatus__doc__[] =
"GetSecurityStatus()\n\n"
"Queries whether some security code needs to be entered.\n\n"
"@return: String indicating which code needs to be entered or None if none is needed\n"
;

static PyObject *
StateMachine_GetSecurityStatus(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_SecurityCodeType    Status;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetSecurityStatus(&(self->s), &Status);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetSecurityStatus")) return NULL;

    switch (Status) {
        case SEC_SecurityCode: return Py_BuildValue("s", "SecurityCode");
        case SEC_Pin: return Py_BuildValue("s", "Pin");
        case SEC_Pin2: return Py_BuildValue("s", "Pin2");
        case SEC_Puk: return Py_BuildValue("s", "Puk");
        case SEC_Puk2: return Py_BuildValue("s", "Puk2");
        case SEC_None: Py_RETURN_NONE;
    }
    Py_RETURN_NONE;
}

/********************/
/* GetDisplayStatus */
/********************/

static char StateMachine_GetDisplayStatus__doc__[] =
"GetDisplayStatus()\n\n"
"Acquired display status.\n"
"@return: List of indicators displayed on display\n"
;

static PyObject *
StateMachine_GetDisplayStatus(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_DisplayFeatures features;
    PyObject *          list;
    PyObject *          val;
    int                 i;
    char                *buffer;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetDisplayStatus(&(self->s), &features);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetDisplayStatus")) return NULL;

    list = PyList_New(0);

    for (i = 0; i < features.Number; i++) {
        buffer = "Unknown";
        switch (features.Feature[i]) {
            case GSM_CallActive: buffer = "CallActive"; break;
            case GSM_UnreadSMS: buffer = "UnreadSMS"; break;
            case GSM_VoiceCall: buffer = "VoiceCall"; break;
            case GSM_FaxCall: buffer = "FaxCall"; break;
            case GSM_DataCall: buffer = "DataCall"; break;
            case GSM_KeypadLocked: buffer = "KeypadLocked"; break;
            case GSM_SMSMemoryFull: buffer = "SMSMemoryFull"; break;
        }
        val = Py_BuildValue("s", buffer);
        if (PyList_Append(list, val) != 0) {
            Py_DECREF(list);
            Py_DECREF(val);
            return NULL;
        }
        Py_DECREF(val);
    }

    return list;
}

/***********************/
/* SetAutoNetworkLogin */
/***********************/

static char StateMachine_SetAutoNetworkLogin__doc__[] =
"SetAutoNetworkLogin()\n\n"
"Enables network auto login.\n\n"
"@return: None\n"
;

static PyObject *
StateMachine_SetAutoNetworkLogin(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetAutoNetworkLogin(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetAutoNetworkLogin")) return NULL;

    Py_RETURN_NONE;
}

/********************/
/* GetBatteryCharge */
/********************/

static char StateMachine_GetBatteryCharge__doc__[] =
"GetBatteryCharge()\n\n"
"Gets information about batery charge and phone charging state.\n\n"
"@return: Hash containing information about battery state (BatteryPercent and ChargeState)\n"
;

static PyObject *
StateMachine_GetBatteryCharge(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_BatteryCharge   bat;
    char                *buffer;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetBatteryCharge(&(self->s), &bat);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetBatteryCharge")) return NULL;

    buffer = "Unknown";

    switch (bat.ChargeState) {
        case GSM_BatteryPowered: buffer = "BatteryPowered"; break;
        case GSM_BatteryConnected: buffer = "BatteryConnected"; break;
        case GSM_BatteryNotConnected: buffer = "BatteryNotConnected"; break;
        case GSM_PowerFault: buffer = "PowerFault"; break;
    }

    return Py_BuildValue("{s:i,s:s}",
            "BatteryPercent", bat.BatteryPercent,
            "ChargeState", buffer);
}

/********************/
/* GetSignalQuality */
/********************/

static char StateMachine_GetSignalQuality__doc__[] =
"GetSignalQuality()\n\n"
"Reads signal quality (strength and error rate).\n\n"
"@return: Hash containing information about signal state (SignalStrength, SignalPercent and BitErrorRate)\n"
;

static PyObject *
StateMachine_GetSignalQuality(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_SignalQuality   sig;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetSignalQuality(&(self->s), &sig);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetSignalQuality")) return NULL;

    return Py_BuildValue("{s:i,s:i,s:i}",
            "SignalStrength", sig.SignalStrength,
            "SignalPercent", sig.SignalPercent,
            "BitErrorRate", sig.BitErrorRate);
}

/******************/
/* GetNetworkInfo */
/******************/

static char StateMachine_GetNetworkInfo__doc__[] =
"GetNetworkInfo()\n\n"
"Gets network information.\n\n"
"@return: Hash with information about network (NetworkName, State, NetworkCode, CID and LAC)\n"
;

static PyObject *
StateMachine_GetNetworkInfo(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_NetworkInfo     netinfo;
    char                *buffer;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetNetworkInfo(&(self->s), &netinfo);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetNetworkInfo")) return NULL;

    buffer = "Unknown";

    switch(netinfo.State) {
        case GSM_HomeNetwork: buffer = "HomeNetwork"; break;
        case GSM_RoamingNetwork: buffer = "RoamingNetwork"; break;
        case GSM_RequestingNetwork: buffer = "RequestingNetwork"; break;
        case GSM_NoNetwork: buffer = "NoNetwork"; break;
        case GSM_RegistrationDenied: buffer = "RegistrationDenied"; break;
        case GSM_NetworkStatusUnknown: buffer = "NetworkStatusUnknown"; break;
    }

    return Py_BuildValue("{s:s,s:s,s:s,s:s,s:s}",
            "NetworkName", netinfo.NetworkName,
            "State", buffer,
            "NetworkCode", netinfo.NetworkCode,
            "CID", netinfo.CID,
            "LAC", netinfo.LAC);
}

/***************/
/* GetCategory */
/***************/

static char StateMachine_GetCategory__doc__[] =
"GetCategory(Type, Location)\n\n"
"Reads category from phone.\n\n"
"@param Type: Type of category to read, one of 'ToDo', 'Phonebook'\n"
"@type Type: string\n"
"@param Location: Location of category to read\n"
"@type Location: int\n"
"@return: Category name as string\n"
;

static PyObject *
StateMachine_GetCategory(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_Category        Category;
    static char         *kwlist[] = {"Type", "Location", NULL};
    char                *s;
    Py_UNICODE          *u;
    PyObject            *o;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "si", kwlist,
                &s, &(Category.Location)))
        return NULL;

    Category.Type = StringToCategoryType(s);
    if (Category.Type == 0) return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetCategory(&(self->s), &Category);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetCategory")) return NULL;

    u = strGammuToPython(Category.Name);
    if (u == NULL) return NULL;
    o = Py_BuildValue("u", u);
    free(u);

    return o;
}

/***************/
/* AddCategory */
/***************/

static char StateMachine_AddCategory__doc__[] =
"AddCategory(Type, Name)\n\n"
"Adds category to phone.\n\n"
"@param Type: Type of category to read, one of 'ToDo', 'Phonebook'\n"
"@type Type: string\n"
"@param Name: Category name\n"
"@type Name: string\n"
"@return: Location of created category\n"
;

static PyObject *
StateMachine_AddCategory(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_Category        Category;
    static char         *kwlist[] = {"Type", "Name", NULL};
    char                *s;
    PyObject            *u;
    unsigned char       *name;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "sO", kwlist,
                &s, &u))
        return NULL;

    Category.Type = StringToCategoryType(s);
    if (Category.Type == 0) return NULL;

    if (!PyString_Check(u) && !PyUnicode_Check(u)) {
        PyErr_Format(PyExc_ValueError, "Name not string nor unicode!");
        return NULL;
    }
    name = StringPythonToGammu(u);
    if (name == NULL) return NULL;

    if (UnicodeLength(name) > GSM_MAX_CATEGORY_NAME_LENGTH) {
        pyg_warning("Category name too long, truncating to %d (from %zd)\n", GSM_MAX_CATEGORY_NAME_LENGTH, UnicodeLength(name));
        name[2*GSM_MAX_CATEGORY_NAME_LENGTH] = 0;
        name[2*GSM_MAX_CATEGORY_NAME_LENGTH + 1] = 0;
    }

    CopyUnicodeString(Category.Name, name);
    free(name);

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->AddCategory(&(self->s), &Category);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "AddCategory")) return NULL;

    return Py_BuildValue("i", Category.Location);
}

/*********************/
/* GetCategoryStatus */
/*********************/

static char StateMachine_GetCategoryStatus__doc__[] =
"GetCategoryStatus(Type)\n\n"
"Reads category status (number of used entries) from phone.\n"
"@param Type: Type of category to read, one of 'ToDo', 'Phonebook'\n"
"@type Type: string\n"
"@return: Hash containing information about category status (Used)\n"
;

static PyObject *
StateMachine_GetCategoryStatus(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_CategoryStatus  Status;
    static char         *kwlist[] = {"Type", NULL};
    char                *s;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist,
                &s))
        return NULL;

    Status.Type = StringToCategoryType(s);
    if (Status.Type == 0) return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetCategoryStatus(&(self->s), &Status);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetCategoryStatus")) return NULL;

    return Py_BuildValue("{s:i}", "Used", Status.Used);
}

/*******************/
/* GetMemoryStatus */
/*******************/

static char StateMachine_GetMemoryStatus__doc__[] =
"GetMemoryStatus(Type)\n\n"
"Gets memory (phonebooks or calls) status (eg. number of used andfree entries).\n\n"
"@param Type: Memory type, one of 'ME', 'SM', 'ON', 'DC', 'RC', 'MC', 'MT', 'FD', 'VM'\n"
"@type Type: string\n"
"@return: Hash with information about memory (Used and Free)\n"
;

static PyObject *
StateMachine_GetMemoryStatus(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_MemoryStatus    Status;
    static char         *kwlist[] = {"Type", NULL};
    char                *s;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist,
                &s))
        return NULL;

    Status.MemoryType = StringToMemoryType(s);
    if (Status.MemoryType == 0) return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetMemoryStatus(&(self->s), &Status);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetMemoryStatus")) return NULL;

    return Py_BuildValue("{s:i,s:i}", "Used", Status.MemoryUsed, "Free", Status.MemoryFree);
}

/*************/
/* GetMemory */
/*************/

static char StateMachine_GetMemory__doc__[] =
"GetMemory(Type, Location)\n\n"
"Reads entry from memory (phonebooks or calls). Which entry shouldbe read is defined in entry.\n"
"@param Type: Memory type, one of 'ME', 'SM', 'ON', 'DC', 'RC', 'MC', 'MT', 'FD', 'VM'\n"
"@type Type: string\n"
;

static PyObject *
StateMachine_GetMemory(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_MemoryEntry     entry;
    static char         *kwlist[] = {"Type", "Location", NULL};
    char                *s;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "si", kwlist,
                &s, &(entry.Location)))
        return NULL;

    entry.MemoryType = StringToMemoryType(s);
    if (entry.MemoryType == 0) return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetMemory(&(self->s), &entry);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetMemory")) return NULL;

    return MemoryEntryToPython(&entry);
}

/*****************/
/* GetNextMemory */
/*****************/

static char StateMachine_GetNextMemory__doc__[] =
"GetNextMemory(Type, Start, Location)\n\n"
"Reads entry from memory (phonebooks or calls). Which entry shouldbe read is defined in entry. This can be easily used for reading all entries.\n\n"
"@param Type: Memory type, one of 'ME', 'SM', 'ON', 'DC', 'RC', 'MC', 'MT', 'FD', 'VM'\n"
"@type Type: string\n"
"@param Start: Whether to start. This can not be used together with Location\n"
"@type Start: bool\n"
"@param Location: Last read location. This can not be used together with Start\n"
"@type Location: int\n"
"@return: Memory entry as hash\n"
;

static PyObject *
StateMachine_GetNextMemory(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_MemoryEntry     entry;
    static char         *kwlist[] = {"Type", "Start", "Location", NULL};
    char                *s = NULL;
    int                 start = false;

    entry.Location = -1;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|ii", kwlist,
                &s, &start, &(entry.Location)))
        return NULL;

    if (!start && entry.Location == -1) {
        PyErr_SetString(PyExc_TypeError, "you have to specify Type and (Start or Location)");
        return NULL;
    }

    entry.MemoryType = StringToMemoryType(s);
    if (entry.MemoryType == 0) return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetNextMemory(&(self->s), &entry, start);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetNextMemory")) return NULL;

    return MemoryEntryToPython(&entry);
}

/*************/
/* SetMemory */
/*************/

static char StateMachine_SetMemory__doc__[] =
"SetMemory(Value)\n\n"
"Sets memory (phonebooks or calls) entry.\n\n"
"@param Value: Memory entry\n"
"@type Value: hash\n"
"@return: Location of created entry\n"
;

static PyObject *
StateMachine_SetMemory(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    PyObject            *value;
    GSM_MemoryEntry     entry;
    static char         *kwlist[] = {"Value", NULL};

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!", kwlist,
                &PyDict_Type, &(value)))
        return NULL;

    if (!MemoryEntryFromPython(value, &entry, 1)) return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetMemory(&(self->s), &entry);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetMemory")) return NULL;

    return PyInt_FromLong(entry.Location);
}

/*************/
/* AddMemory */
/*************/

static char StateMachine_AddMemory__doc__[] =
"AddMemory(Value)\n\n"
"Adds memory (phonebooks or calls) entry.\n\n"
"@param Value: Memory entry\n"
"@type Value: hash\n"
"@return: Location of created entry\n"
;

static PyObject *
StateMachine_AddMemory(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_Error           error2;
    PyObject            *value;
    GSM_MemoryEntry     entry;
    GSM_MemoryEntry     tmp;
    static char         *kwlist[] = {"Value", NULL};
    int                 loc;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!", kwlist,
                &PyDict_Type, &(value)))
        return NULL;

    if (!MemoryEntryFromPython(value, &entry, 0)) return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->AddMemory(&(self->s), &entry);

    /* Phone does not support adding, let's implement it here */
    if (error == ERR_NOTSUPPORTED || error == ERR_NOTIMPLEMENTED) {
        /* Speed up lookup by starting from last location */
        if (self->memory_entry_cache_type == entry.MemoryType) {
            loc = self->memory_entry_cache;
        } else {
            loc = 1;
        }
        error2 = ERR_NONE;
        while (error2 == ERR_NONE) {
            tmp.MemoryType = entry.MemoryType;
            tmp.Location = loc;
            loc++;
            error2 = self->s.Phone.Functions->GetMemory(&(self->s), &tmp);
        }
        if (error2 == ERR_EMPTY) {
            /* Found what wanted, use regullar add */
            self->memory_entry_cache = loc;
            entry.Location = tmp.Location;
            error = self->s.Phone.Functions->SetMemory(&(self->s), &entry);
        } else if (error2 == ERR_INVALIDLOCATION) {
            /* Indicate full memory */
            error = ERR_FULL;
        } else {
            /* Propagate error */
            error = error2;
        }
    }
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "AddMemory")) return NULL;

    return PyInt_FromLong(entry.Location);
}

/****************/
/* DeleteMemory */
/****************/

static char StateMachine_DeleteMemory__doc__[] =
"DeleteMemory(Type, Location)\n\n"
"Deletes memory (phonebooks or calls) entry.\n\n"
"@param Type: Memory type, one of 'ME', 'SM', 'ON', 'DC', 'RC', 'MC', 'MT', 'FD', 'VM'\n"
"@type Type: string\n"
"@param Location: Location of entry to delete\n"
"@type Location: int\n"
"@return: None\n"
;

static PyObject *
StateMachine_DeleteMemory(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_MemoryEntry     entry;
    static char         *kwlist[] = {"Type", "Location", NULL};
    char                *s;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "si", kwlist,
                &s, &(entry.Location)))
        return NULL;

    entry.MemoryType = StringToMemoryType(s);
    if (entry.MemoryType == 0) return NULL;

    /* Move hint for adding entries */
    if (entry.MemoryType == self->memory_entry_cache_type && entry.Location < self->memory_entry_cache) {
        self->memory_entry_cache = entry.Location;
    }

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->DeleteMemory(&(self->s), &entry);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "DeleteMemory")) return NULL;

    Py_RETURN_NONE;
}

/*******************/
/* DeleteAllMemory */
/*******************/

static char StateMachine_DeleteAllMemory__doc__[] =
"DeleteAllMemory(Type)\n\n"
"Deletes all memory (phonebooks or calls) entries of specified type.\n\n"
"@param Type: Memory type, one of 'ME', 'SM', 'ON', 'DC', 'RC', 'MC', 'MT', 'FD', 'VM'\n"
"@type Type: string\n"
"@return: None\n"
;

static PyObject *
StateMachine_DeleteAllMemory(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_MemoryType      MemoryType;
    static char         *kwlist[] = {"Type", NULL};
    char                *s;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist,
                &s))
        return NULL;

    MemoryType = StringToMemoryType(s);
    if (MemoryType == 0) return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->DeleteAllMemory(&(self->s), MemoryType);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "DeleteAllMemory")) return NULL;

    Py_RETURN_NONE;
}

/****************/
/* GetSpeedDial */
/****************/

static char StateMachine_GetSpeedDial__doc__[] =
"GetSpeedDial(Location)\n\n"
"Gets speed dial.\n\n"
"@param Location: Location of entry to read\n"
"@type Location: int\n"
"@return: Hash with speed dial (Location, MemoryLocation, MemoryNumberID, MemoryType)\n"
;

static PyObject *
StateMachine_GetSpeedDial(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_SpeedDial       Speed;
    static char         *kwlist[] = {"Location", NULL};
    char                *s;
    PyObject            *ret;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "i", kwlist,
                &(Speed.Location)))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetSpeedDial(&(self->s), &Speed);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetSpeedDial")) return NULL;

    s = MemoryTypeToString(Speed.MemoryType);

    ret = Py_BuildValue("{s:i,s:i,s:i,s:s}",
            "Location",         Speed.Location,
            "MemoryLocation",   Speed.MemoryLocation,
            "MemoryNumberID",   Speed.MemoryNumberID,
            "MemoryType",       s);
    free(s);

    return ret;
}

/****************/
/* SetSpeedDial */
/****************/

static char StateMachine_SetSpeedDial__doc__[] =
"SetSpeedDial(Value)\n\n"
"Sets speed dial.\n\n"
"@param Value: Speed dial data, see L{GetSpeedDial} for listing.\n"
"@type Value: hash\n"
"@return: None\n"
;

static PyObject *
StateMachine_SetSpeedDial(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_SpeedDial       Speed;
    static char         *kwlist[] = {"Value", NULL};
    PyObject            *value;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!", kwlist,
                &PyDict_Type, &(value)))
        return NULL;

    Speed.Location = GetIntFromDict(value, "Location");
    if (Speed.Location == INT_INVALID) return NULL;

    Speed.MemoryNumberID = GetIntFromDict(value, "MemoryNumberID");
    if (Speed.MemoryNumberID == INT_INVALID) return NULL;

    Speed.MemoryLocation = GetIntFromDict(value, "MemoryLocation");
    if (Speed.MemoryLocation == INT_INVALID) return NULL;

    Speed.MemoryType = GetMemoryTypeFromDict(value, "MemoryType");
    if (Speed.MemoryType == -1) return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetSpeedDial(&(self->s), &Speed);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetSpeedDial")) return NULL;

    Py_RETURN_NONE;
}

/***********/
/* GetSMSC */
/***********/

static char StateMachine_GetSMSC__doc__[] =
"GetSMSC(Location)\n\n"
"Gets SMS Service Center number and SMS settings.\n\n"
"@param Location: Location of entry to read. Defaults to 1\n"
"@type Location: int\n"
"@return: Hash with SMSC information\n"
;

static PyObject *
StateMachine_GetSMSC(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_SMSC            smsc;
    static char         *kwlist[] = {"Location", NULL};

    CHECK_EXISTING_CONNECTION

    smsc.Location = 1;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|i", kwlist,
                &(smsc.Location)))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetSMSC(&(self->s), &smsc);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetSMSC")) return NULL;

    return SMSCToPython(&smsc);
}

/***********/
/* SetSMSC */
/***********/

static char StateMachine_SetSMSC__doc__[] =
"SetSMSC(Value)\n\n"
"Sets SMS Service Center number and SMS settings.\n\n"
"@param Value: SMSC information\n"
"@type Value: hash\n"
"@return: None\n"
;

static PyObject *
StateMachine_SetSMSC(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    PyObject            *value;
    GSM_SMSC            smsc;
    static char         *kwlist[] = {"Value", NULL};

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!", kwlist,
                &PyDict_Type, &(value)))
        return NULL;

    if (!SMSCFromPython(value, &smsc, true)) return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetSMSC(&(self->s), &smsc);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetSMSC")) return NULL;

    Py_RETURN_NONE;
}

/****************/
/* GetSMSStatus */
/****************/

static char StateMachine_GetSMSStatus__doc__[] =
"GetSMSStatus()\n\n"
"Gets information about SMS memory (read/unread/size of memory for both SIM and phone).\n\n"
"@return: Hash with information about phone memory (SIMUnRead, SIMUsed, SIMSize, PhoneUnRead, PhoneUsed, PhoneSize and TemplatesUsed)\n"
;

static PyObject *
StateMachine_GetSMSStatus(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_SMSMemoryStatus status;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetSMSStatus(&(self->s), &status);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetSMSStatus")) return NULL;

    return Py_BuildValue("{s:i,s:i,s:i,s:i,s:i,s:i,s:i}",
            "SIMUnRead", status.SIMUnRead,
            "SIMUsed", status.SIMUsed,
            "SIMSize", status.SIMSize,
            "PhoneUnRead", status.PhoneUnRead,
            "PhoneUsed", status.PhoneUsed,
            "PhoneSize", status.PhoneSize,
            "TemplatesUsed", status.TemplatesUsed);
}

/**********/
/* GetSMS */
/**********/

static char StateMachine_GetSMS__doc__[] =
"GetSMS(Folder, Location)\n\n"
"Reads SMS message.\n\n"
"@param Folder: Folder where to read entry (0 is emulated flat memory)\n"
"@type Folder: int\n"
"@param Location: Location of entry to read\n"
"@type Location: int\n"
"@return: Hash with SMS data\n"
;

static PyObject *
StateMachine_GetSMS(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_MultiSMSMessage sms;
    static char         *kwlist[] = {"Folder", "Location", NULL};

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "II", kwlist, &(sms.SMS[0].Folder), &(sms.SMS[0].Location)))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetSMS(&(self->s), &sms);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetSMS")) return NULL;

    return MultiSMSToPython(&sms);
}


/**************/
/* GetNextSMS */
/**************/

static char StateMachine_GetNextSMS__doc__[] =
"GetNextSMS(Folder, Start, Location)\n\n"
"Reads next (or first if start set) SMS message. This might befaster for some phones than using L{GetSMS} for each message.\n\n"
"@param Folder: Folder where to read entry (0 is emulated flat memory)\n"
"@type Folder: int\n"
"@param Start: Whether to start. This can not be used together with Location\n"
"@type Start: bool\n"
"@param Location: Location last read entry. This can not be used together with Start\n"
"@type Location: int\n"
"@return: Hash with SMS data\n"
;

static PyObject *
StateMachine_GetNextSMS(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_MultiSMSMessage sms;
    static char         *kwlist[] = {"Folder", "Start", "Location", NULL};
    int                 start = false;

    sms.SMS[0].Location = -1;
    sms.SMS[0].Folder = -1;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "i|ii", kwlist,
                &(sms.SMS[0].Folder), &start, &(sms.SMS[0].Location)))
        return NULL;

    if (!start && sms.SMS[0].Location == -1) {
        PyErr_SetString(PyExc_TypeError, "you have to specify Folder and (Start or Location)");
        return NULL;
    }

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetNextSMS(&(self->s), &sms, start);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetNextSMS")) return NULL;

    return MultiSMSToPython(&sms);
}

/**********/
/* SetSMS */
/**********/

static char StateMachine_SetSMS__doc__[] =
"SetSMS(Value)\n\n"
"Sets SMS.\n\n"
"@param Value: SMS data\n"
"@type Value: hash\n"
"@return: Tuple for location and folder.\n"
;

static PyObject *
StateMachine_SetSMS(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_SMSMessage      sms;
    PyObject            *value;
    static char         *kwlist[] = {"Value", NULL};

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!", kwlist,
                &PyDict_Type, &(value)))
        return NULL;

    if (!SMSFromPython(value, &sms, 1, 1, 0)) return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetSMS(&(self->s), &sms);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetSMS")) return NULL;

    return Py_BuildValue("i,i", sms.Location, sms.Folder);
}

/**********/
/* AddSMS */
/**********/

static char StateMachine_AddSMS__doc__[] =
"AddSMS(Value)\n\n"
"Adds SMS to specified folder.\n"
"@param Value: SMS data\n"
"@type Value: hash\n"
"@return: Tuple for location and folder.\n"
;

static PyObject *
StateMachine_AddSMS(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_SMSMessage      sms;
    PyObject            *value;
    static char         *kwlist[] = {"Value", NULL};

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!", kwlist,
                &PyDict_Type, &(value)))
        return NULL;

    if (!SMSFromPython(value, &sms, 0, 1, 0)) return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->AddSMS(&(self->s), &sms);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "AddSMS")) return NULL;

    return Py_BuildValue("i,i", sms.Location, sms.Folder);
}

/*************/
/* DeleteSMS */
/*************/

static char StateMachine_DeleteSMS__doc__[] =
"DeleteSMS(Folder, Location)\n\n"
"Deletes SMS.\n\n"
"@param Folder: Folder where to read entry (0 is emulated flat memory)\n"
"@type Folder: int\n"
"@param Location: Location of entry to delete\n"
"@type Location: int\n"
"@return: None\n"
;

static PyObject *
StateMachine_DeleteSMS(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_SMSMessage      sms;
    static char         *kwlist[] = {"Folder", "Location", NULL};

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "ii", kwlist,
                &(sms.Folder), &(sms.Location)))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->DeleteSMS(&(self->s), &sms);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "DeleteSMS")) return NULL;

    Py_RETURN_NONE;
}

/***********/
/* SendSMS */
/***********/

static char StateMachine_SendSMS__doc__[] =
"SendSMS(Value)\n\n"
"Sends SMS.\n\n"
"@param Value: SMS data\n"
"@type Value: hash\n"
"@return: Message reference as integer\n"
;

static PyObject *
StateMachine_SendSMS(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_SMSMessage      sms;
    PyObject            *value;
    static char         *kwlist[] = {"Value", NULL};
    int                 i = 0;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!", kwlist,
                &PyDict_Type, &(value)))
        return NULL;

    if (!SMSFromPython(value, &sms, 0, 0, 1)) return NULL;

    self->SMSStatus = ERR_TIMEOUT;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SendSMS(&(self->s), &sms);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SendSMS")) return NULL;

    while (self->SMSStatus != ERR_NONE) {
        i++;
        BEGIN_PHONE_COMM
        GSM_ReadDevice(&(self->s), true);
        END_PHONE_COMM
        if (self->SMSStatus == ERR_UNKNOWN || i == 100) {
            if (!checkError(&(self->s), self->SMSStatus, "SendSMS")) {
                return NULL;
            }
        }
    }

    return PyInt_FromLong(self->MessageReference);
}


static char StateMachine_SendSavedSMS__doc__[] =
"SendSavedSMS(Folder, Location)\n\n"
"Sends SMS saved in phone.\n\n"
"@param Folder: Folder where to read entry (0 is emulated flat memory)\n"
"@type Folder: int\n"
"@param Location: Location of entry to send\n"
"@type Location: int\n"
"@return: Message reference as integer\n"
;

static PyObject *
StateMachine_SendSavedSMS(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    int                 Folder;
    int                 Location;
    static char         *kwlist[] = {"Folder", "Location", NULL};
    int                 i = 0;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "ii", kwlist,
                &Folder, &Location))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SendSavedSMS(&(self->s), Folder, Location);
    END_PHONE_COMM

    self->SMSStatus = ERR_TIMEOUT;

    if (!checkError(&(self->s), error, "SendSavedSMS")) return NULL;

    while (self->SMSStatus != ERR_NONE) {
        i++;
        BEGIN_PHONE_COMM
        GSM_ReadDevice(&(self->s),true);
        END_PHONE_COMM
        if (self->SMSStatus == ERR_UNKNOWN || i == 100) {
            if (!checkError(&(self->s), self->SMSStatus, "SendSavedSMS")) {
                return NULL;
            }
        }
    }

    return PyInt_FromLong(self->MessageReference);

}

/******************/
/* SetIncomingSMS */
/******************/

static char StateMachine_SetIncomingSMS__doc__[] =
"SetIncomingSMS(Enable)\n\n"
"Enable/disable notification on incoming SMS.\n\n"
"@type Enable: boolean\n"
"@param Enable: Whether to enable notifications, default is True\n"
"@return: None\n"
;

static PyObject *
StateMachine_SetIncomingSMS(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    int                 enable = true;
    static char         *kwlist[] = {"Enable", NULL};

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|I", kwlist, &enable))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetIncomingSMS(&(self->s), enable);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetIncomingSMS")) return NULL;

    Py_RETURN_NONE;
}

/*****************/
/* SetIncomingCB */
/*****************/

static char StateMachine_SetIncomingCB__doc__[] =
"SetIncomingCB(Enable)\n\n"
"Gets network information from phone.\n"
"@type Enable: boolean\n"
"@param Enable: Whether to enable notifications, default is True\n"
"@return: None\n"
;

static PyObject *
StateMachine_SetIncomingCB(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    int                 enable = true;
    static char         *kwlist[] = {"Enable", NULL};

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|I", kwlist, &enable))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetIncomingCB(&(self->s), enable);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetIncomingCB")) return NULL;

    Py_RETURN_NONE;
}

/*******************/
/* SetIncomingCall */
/*******************/

static char StateMachine_SetIncomingCall__doc__[] =
"SetIncomingCall(Enable)\n\n"
"Activates/deactivates noticing about incoming calls.\n"
"@type Enable: boolean\n"
"@param Enable: Whether to enable notifications, default is True\n"
"@return: None\n"
;

static PyObject *
StateMachine_SetIncomingCall(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    int                 enable = true;
    static char         *kwlist[] = {"Enable", NULL};

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|I", kwlist, &enable))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetIncomingCall(&(self->s), enable);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetIncomingCall")) return NULL;

    Py_RETURN_NONE;
}

/*******************/
/* SetIncomingUSSD */
/*******************/

static char StateMachine_SetIncomingUSSD__doc__[] =
"SetIncomingUSSD(Enable)\n\n"
"Activates/deactivates noticing about incoming USSDs (UnStructured Supplementary Services).\n"
"@type Enable: boolean\n"
"@param Enable: Whether to enable notifications, default is True\n"
"@return: None\n"
;

static PyObject *
StateMachine_SetIncomingUSSD(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    int                 enable = true;
    static char         *kwlist[] = {"Enable", NULL};

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|I", kwlist, &enable))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetIncomingUSSD(&(self->s), enable);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetIncomingUSSD")) return NULL;

    Py_RETURN_NONE;
}

/*****************/
/* GetSMSFolders */
/*****************/

static char StateMachine_GetSMSFolders__doc__[] =
"GetSMSFolders()\n\n"
"Returns SMS folders information.\n\n"
"@return: List of SMS folders.\n"
;

static PyObject *
StateMachine_GetSMSFolders(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_SMSFolders      folders;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetSMSFolders(&(self->s), &folders);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetSMSFolders")) return NULL;

    return SMSFoldersToPython(&folders);
}

/****************/
/* AddSMSFolder */
/****************/

static char StateMachine_AddSMSFolder__doc__[] =
"AddSMSFolder(Name)\n\n"
"Creates SMS folder.\n\n"
"@param Name: Name of new folder\n"
"@type Name: string\n"
"@return: None\n"
;

static PyObject *
StateMachine_AddSMSFolder(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    PyObject            *val;
    unsigned char       *str;
    static char         *kwlist[] = {"Name", NULL};

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O", kwlist,
                &val))
        return NULL;

    if (!PyString_Check(val) && !PyUnicode_Check(val)) {
        PyErr_Format(PyExc_ValueError, "Name not string nor unicode!");
        return NULL;
    }

    str = StringPythonToGammu(val);
    if (str == NULL) return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->AddSMSFolder(&(self->s), str);
    END_PHONE_COMM

    free(str);

    if (!checkError(&(self->s), error, "AddSMSFolder")) return NULL;

    Py_RETURN_NONE;
}

/*******************/
/* DeleteSMSFolder */
/*******************/

static char StateMachine_DeleteSMSFolder__doc__[] =
"DeleteSMSFolder(ID)\n\n"
"Deletes SMS folder.\n\n"
"@param ID: Index of folder to delete\n"
"@type ID: int\n"
"@return: None\n"
;

static PyObject *
StateMachine_DeleteSMSFolder(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    static char         *kwlist[] = {"ID", NULL};
    int                 val;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "i", kwlist,
                &val))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->DeleteSMSFolder(&(self->s), val);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "DeleteSMSFolder")) return NULL;

    Py_RETURN_NONE;
}

/*************/
/* DialVoice */
/*************/

static char StateMachine_DialVoice__doc__[] =
"DialVoice(Number, ShowNumber)\n\n"
"Dials number and starts voice call.\n"
"@param Number: Number to dial\n"
"@type Number: string\n"
"@param ShowNumber: Identifies whether to enable CLIR (None = keep default phone settings). Default is None\n"
"@type ShowNumber: bool or None\n"
"@return: None\n"
;

static PyObject *
StateMachine_DialVoice(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    static char         *kwlist[] = {"Number", "ShowNumber", NULL};
    char                *s;
    PyObject            *o = Py_None;
    GSM_CallShowNumber  ShowNumber;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|O", kwlist,
                &s, &o))
        return NULL;

    if (o == Py_None) {
        ShowNumber = GSM_CALL_DefaultNumberPresence;
    } else if (o == Py_False) {
        ShowNumber = GSM_CALL_HideNumber;
    } else if (o == Py_True) {
        ShowNumber = GSM_CALL_ShowNumber;
    } else {
        PyErr_SetString(PyExc_TypeError, "use None or bool as ShowNumber!");
        return NULL;
    }

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->DialVoice(&(self->s), s, ShowNumber);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "DialVoice")) return NULL;

    Py_RETURN_NONE;
}

#if 0
/**************/
/* AnswerCall */
/**************/

static char StateMachine_AnswerCall__doc__[] =
"Accept current incoming call."
;

static PyObject *
StateMachine_AnswerCall(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->AnswerCall(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "AnswerCall")) return NULL;

    Py_RETURN_NONE;
}

/**************/
/* CancelCall */
/**************/

static char StateMachine_CancelCall__doc__[] =
"Deny current incoming call."
;

static PyObject *
StateMachine_CancelCall(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->CancelCall(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "CancelCall")) return NULL;

    Py_RETURN_NONE;
}

/************/
/* HoldCall */
/************/

static char StateMachine_HoldCall__doc__[] =
"Holds call."
;

static PyObject *
StateMachine_HoldCall(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->HoldCall(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "HoldCall")) return NULL;

    Py_RETURN_NONE;
}

/**************/
/* UnholdCall */
/**************/

static char StateMachine_UnholdCall__doc__[] =
"Unholds call."
;

static PyObject *
StateMachine_UnholdCall(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->UnholdCall(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "UnholdCall")) return NULL;

    Py_RETURN_NONE;
}

/******************/
/* ConferenceCall */
/******************/

static char StateMachine_ConferenceCall__doc__[] =
"Initiates conference call."
;

static PyObject *
StateMachine_ConferenceCall(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->ConferenceCall(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "ConferenceCall")) return NULL;

    Py_RETURN_NONE;
}

/*************/
/* SplitCall */
/*************/

static char StateMachine_SplitCall__doc__[] =
"Splits call."
;

static PyObject *
StateMachine_SplitCall(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SplitCall(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SplitCall")) return NULL;

    Py_RETURN_NONE;
}

/****************/
/* TransferCall */
/****************/

static char StateMachine_TransferCall__doc__[] =
"Transfers call."
;

static PyObject *
StateMachine_TransferCall(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->TransferCall(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "TransferCall")) return NULL;

    Py_RETURN_NONE;
}

/**************/
/* SwitchCall */
/**************/

static char StateMachine_SwitchCall__doc__[] =
"Switches call."
;

static PyObject *
StateMachine_SwitchCall(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SwitchCall(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SwitchCall")) return NULL;

    Py_RETURN_NONE;
}

/*****************/
/* GetCallDivert */
/*****************/

static char StateMachine_GetCallDivert__doc__[] =
"Gets call diverts."
;

static PyObject *
StateMachine_GetCallDivert(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetCallDivert(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetCallDivert")) return NULL;

    Py_RETURN_NONE;
}

/*****************/
/* SetCallDivert */
/*****************/

static char StateMachine_SetCallDivert__doc__[] =
"Sets call diverts."
;

static PyObject *
StateMachine_SetCallDivert(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetCallDivert(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetCallDivert")) return NULL;

    Py_RETURN_NONE;
}

/********************/
/* CancelAllDiverts */
/********************/

static char StateMachine_CancelAllDiverts__doc__[] =
"Cancels all diverts."
;

static PyObject *
StateMachine_CancelAllDiverts(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->CancelAllDiverts(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "CancelAllDiverts")) return NULL;

    Py_RETURN_NONE;
}

/************/
/* SendDTMF */
/************/

static char StateMachine_SendDTMF__doc__[] =
"Sends DTMF (Dual Tone Multi Frequency) tone."
;

static PyObject *
StateMachine_SendDTMF(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SendDTMF(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SendDTMF")) return NULL;

    Py_RETURN_NONE;
}

/***************/
/* GetRingtone */
/***************/

static char StateMachine_GetRingtone__doc__[] =
"Gets ringtone from phone."
;

static PyObject *
StateMachine_GetRingtone(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetRingtone(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetRingtone")) return NULL;

    Py_RETURN_NONE;
}

/***************/
/* SetRingtone */
/***************/

static char StateMachine_SetRingtone__doc__[] =
"Sets ringtone in phone."
;

static PyObject *
StateMachine_SetRingtone(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetRingtone(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetRingtone")) return NULL;

    Py_RETURN_NONE;
}

/********************/
/* GetRingtonesInfo */
/********************/

static char StateMachine_GetRingtonesInfo__doc__[] =
"Acquires ringtone informaiton."
;

static PyObject *
StateMachine_GetRingtonesInfo(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetRingtonesInfo(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetRingtonesInfo")) return NULL;

    Py_RETURN_NONE;
}

/***********************/
/* DeleteUserRingtones */
/***********************/

static char StateMachine_DeleteUserRingtones__doc__[] =
"Deletes user defined ringtones from phone."
;

static PyObject *
StateMachine_DeleteUserRingtones(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->DeleteUserRingtones(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "DeleteUserRingtones")) return NULL;

    Py_RETURN_NONE;
}

/************/
/* PlayTone */
/************/

static char StateMachine_PlayTone__doc__[] =
"Plays tone."
;

static PyObject *
StateMachine_PlayTone(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->PlayTone(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "PlayTone")) return NULL;

    Py_RETURN_NONE;
}

/******************/
/* GetWAPBookmark */
/******************/

static char StateMachine_GetWAPBookmark__doc__[] =
"Reads WAP bookmark."
;

static PyObject *
StateMachine_GetWAPBookmark(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetWAPBookmark(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetWAPBookmark")) return NULL;

    Py_RETURN_NONE;
}

/******************/
/* SetWAPBookmark */
/******************/

static char StateMachine_SetWAPBookmark__doc__[] =
"Sets WAP bookmark."
;

static PyObject *
StateMachine_SetWAPBookmark(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetWAPBookmark(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetWAPBookmark")) return NULL;

    Py_RETURN_NONE;
}

/*********************/
/* DeleteWAPBookmark */
/*********************/

static char StateMachine_DeleteWAPBookmark__doc__[] =
"Deletes WAP bookmark."
;

static PyObject *
StateMachine_DeleteWAPBookmark(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->DeleteWAPBookmark(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "DeleteWAPBookmark")) return NULL;

    Py_RETURN_NONE;
}

/******************/
/* GetWAPSettings */
/******************/

static char StateMachine_GetWAPSettings__doc__[] =
"Acquires WAP settings."
;

static PyObject *
StateMachine_GetWAPSettings(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetWAPSettings(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetWAPSettings")) return NULL;

    Py_RETURN_NONE;
}

/******************/
/* SetWAPSettings */
/******************/

static char StateMachine_SetWAPSettings__doc__[] =
"Changes WAP settings."
;

static PyObject *
StateMachine_SetWAPSettings(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetWAPSettings(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetWAPSettings")) return NULL;

    Py_RETURN_NONE;
}

/******************/
/* GetMMSSettings */
/******************/

static char StateMachine_GetMMSSettings__doc__[] =
"Acquires MMS settings."
;

static PyObject *
StateMachine_GetMMSSettings(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetMMSSettings(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetMMSSettings")) return NULL;

    Py_RETURN_NONE;
}

/******************/
/* SetMMSSettings */
/******************/

static char StateMachine_SetMMSSettings__doc__[] =
"Changes MMS settings."
;

static PyObject *
StateMachine_SetMMSSettings(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetMMSSettings(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetMMSSettings")) return NULL;

    Py_RETURN_NONE;
}

/*************/
/* GetBitmap */
/*************/

static char StateMachine_GetBitmap__doc__[] =
"Gets bitmap."
;

static PyObject *
StateMachine_GetBitmap(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetBitmap(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetBitmap")) return NULL;

    Py_RETURN_NONE;
}

/*************/
/* SetBitmap */
/*************/

static char StateMachine_SetBitmap__doc__[] =
"Sets bitmap."
;

static PyObject *
StateMachine_SetBitmap(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetBitmap(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetBitmap")) return NULL;

    Py_RETURN_NONE;
}
#endif

/*****************/
/* GetToDoStatus */
/*****************/

static char StateMachine_GetToDoStatus__doc__[] =
"GetToDoStatus()\n\n"
"Gets status of ToDos (count of used entries).\n\n"
"@return: Hash of status (Used)\n"
;

static PyObject *
StateMachine_GetToDoStatus(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_ToDoStatus      status;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetToDoStatus(&(self->s), &status);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetToDoStatus")) return NULL;

    return Py_BuildValue("{s:i}", "Used", status.Used);
}

/***********/
/* GetToDo */
/***********/

static char StateMachine_GetToDo__doc__[] =
"GetToDo(Location)\n\n"
"Reads ToDo from phone.\n"
"@param Location: Location of entry to read\n"
"@type Location: int\n"
"@return: Hash with ToDo values\n"
;

static PyObject *
StateMachine_GetToDo(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_ToDoEntry       todo;
    static char         *kwlist[] = {"Location", NULL};

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "I", kwlist, &(todo.Location)))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetToDo(&(self->s), &todo);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetToDo")) return NULL;

    return TodoToPython(&todo);
}

/***************/
/* GetNextToDo */
/***************/

static char StateMachine_GetNextToDo__doc__[] =
"GetNextToDo(Start, Location)\n\n"
"Reads ToDo from phone.\n\n"
"@param Start: Whether to start. This can not be used together with Location\n"
"@type Start: bool\n"
"@param Location: Last read location. This can not be used together with Start\n"
"@type Location: int\n"
"@return: Hash with ToDo values\n"
;

static PyObject *
StateMachine_GetNextToDo(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    static char         *kwlist[] = {"Start", "Location", NULL};
    GSM_ToDoEntry       todo;
    int                 start = false;

    todo.Location = -1;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ii", kwlist,
                &start, &(todo.Location)))
        return NULL;

    if (!start && todo.Location == -1) {
        PyErr_SetString(PyExc_TypeError, "you have to specify Start or Location");
        return NULL;
    }

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetNextToDo(&(self->s), &todo, start);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetNextToDo")) return NULL;

    return TodoToPython(&todo);
}

/***********/
/* SetToDo */
/***********/

static char StateMachine_SetToDo__doc__[] =
"SetToDo(Value)\n\n"
"Sets ToDo in phone.\n\n"
"@param Value: ToDo data\n"
"@type Value: hash\n"
"@return: Location of created entry\n"
;

static PyObject *
StateMachine_SetToDo(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_ToDoEntry       entry;
    PyObject            *value;
    static char         *kwlist[] = {"Value", NULL};

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!", kwlist,
                &PyDict_Type, &(value)))
        return NULL;

    if (!TodoFromPython(value, &entry, 1)) return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetToDo(&(self->s), &entry);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetToDo")) return NULL;

    return PyInt_FromLong(entry.Location);
}

/***********/
/* AddToDo */
/***********/

static char StateMachine_AddToDo__doc__[] =
"AddToDo(Value)\n\n"
"Adds ToDo in phone.\n\n"
"@param Value: ToDo data\n"
"@type Value: hash\n"
"@return: Location of created entry\n"
;

static PyObject *
StateMachine_AddToDo(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_Error           error2;
    GSM_ToDoEntry       entry;
    GSM_ToDoEntry       tmp;
    PyObject            *value;
    static char         *kwlist[] = {"Value", NULL};
    int                 loc;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!", kwlist,
                &PyDict_Type, &(value)))
        return NULL;

    if (!TodoFromPython(value, &entry, 0)) return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->AddToDo(&(self->s), &entry);

    /* Phone does not support adding, let's implement it here */
    if (error == ERR_NOTSUPPORTED || error == ERR_NOTIMPLEMENTED) {
        /* Speed up lookup by starting from last location */
        loc = self->todo_entry_cache;
        error2 = ERR_NONE;
        while (error2 == ERR_NONE) {
            tmp.Location = loc;
            loc++;
            error2 = self->s.Phone.Functions->GetToDo(&(self->s), &tmp);
        }
        if (error2 == ERR_EMPTY) {
            /* Found what wanted, use regullar add */
            self->todo_entry_cache = loc;
            entry.Location = tmp.Location;
            error = self->s.Phone.Functions->SetToDo(&(self->s), &entry);
        } else if (error2 == ERR_INVALIDLOCATION) {
            /* Indicate full memory */
            error = ERR_FULL;
        } else {
            /* Propagate error */
            error = error2;
        }
    }
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "AddToDo")) return NULL;

    return PyInt_FromLong(entry.Location);
}

/**************/
/* DeleteToDo */
/**************/

static char StateMachine_DeleteToDo__doc__[] =
"DeleteToDo(Location)\n\n"
"Deletes ToDo entry in phone.\n"
"@param Location: Location of entry to delete\n"
"@type Location: int\n"
"@return: None\n"
;

static PyObject *
StateMachine_DeleteToDo(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_ToDoEntry       entry;
    static char         *kwlist[] = {"Location", NULL};

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "i", kwlist,
                &(entry.Location)))
        return NULL;

    /* Move hint for adding entries */
    self->todo_entry_cache = entry.Location;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->DeleteToDo(&(self->s), &entry);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "DeleteToDo")) return NULL;

    Py_RETURN_NONE;
}

/*****************/
/* DeleteAllToDo */
/*****************/

static char StateMachine_DeleteAllToDo__doc__[] =
"DeleteAllToDo()\n\n"
"Deletes all todo entries in phone.\n\n"
"@return: None\n"
;

static PyObject *
StateMachine_DeleteAllToDo(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->DeleteAllToDo(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "DeleteAllToDo")) return NULL;

    Py_RETURN_NONE;
}

/*********************/
/* GetCalendarStatus */
/*********************/

static char StateMachine_GetCalendarStatus__doc__[] =
"GetCalendarStatus()\n\n"
"Retrieves calendar status (number of used entries).\n\n"
"@return: Hash with calendar status (Used)\n"
;

static PyObject *
StateMachine_GetCalendarStatus(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_CalendarStatus  status;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetCalendarStatus(&(self->s), &status);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetCalendarStatus")) return NULL;

    return Py_BuildValue("{s:i}", "Used", status.Used);
}

/***************/
/* GetCalendar */
/***************/

static char StateMachine_GetCalendar__doc__[] =
"GetCalendar(Location)\n\n"
"Retrieves calendar entry.\n\n"
"@param Location: Calendar entry to read\n"
"@type Location: int\n"
"@return: Hash with calendar values\n"
;

static PyObject *
StateMachine_GetCalendar(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_CalendarEntry   entry;
    static char         *kwlist[] = {"Location", NULL};

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "I", kwlist, &(entry.Location)))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetCalendar(&(self->s), &entry);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetCalendar")) return NULL;

    return CalendarToPython(&entry);
}

/*******************/
/* GetNextCalendar */
/*******************/

static char StateMachine_GetNextCalendar__doc__[] =
"GetNextCalendar(Start, Location)\n\n"
"Retrieves calendar entry. This is useful for continuous reading of all calendar entries.\n\n"
"@param Start: Whether to start. This can not be used together with Location\n"
"@type Start: bool\n"
"@param Location: Last read location. This can not be used together with Start\n"
"@type Location: int\n"
"@return: Hash with calendar values\n"
;

static PyObject *
StateMachine_GetNextCalendar(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_CalendarEntry   entry;
    int                 start = false;
    static char         *kwlist[] = {"Start", "Location", NULL};

    entry.Location = -1;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ii", kwlist, &start, &(entry.Location)))
        return NULL;

    if (!start && entry.Location == -1) {
        PyErr_SetString(PyExc_TypeError, "you have to specify Start or Location");
        return NULL;
    }

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetNextCalendar(&(self->s), &entry, start);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetNextCalendar")) return NULL;

    return CalendarToPython(&entry);
}

/***************/
/* SetCalendar */
/***************/

static char StateMachine_SetCalendar__doc__[] =
"SetCalendar(Value)\n\n"
"Sets calendar entry\n\n"
"@param Value: Calendar entry data\n"
"@type Value: hash\n"
"@return: Location of set entry\n"
;

static PyObject *
StateMachine_SetCalendar(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_CalendarEntry   entry;
    PyObject            *value;
    static char         *kwlist[] = {"Value", NULL};

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!", kwlist,
                &PyDict_Type, &(value)))
        return NULL;

    if (!CalendarFromPython(value, &entry, 1)) return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetCalendar(&(self->s), &entry);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetCalendar")) return NULL;

    return PyInt_FromLong(entry.Location);
}

/***************/
/* AddCalendar */
/***************/

static char StateMachine_AddCalendar__doc__[] =
"AddCalendar(Value)\n\n"
"Adds calendar entry.\n\n"
"@param Value: Calendar entry data\n"
"@type Value: hash\n"
"@return: Location of newly created entry\n"
;

static PyObject *
StateMachine_AddCalendar(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_Error           error2;
    GSM_CalendarEntry   entry;
    GSM_CalendarEntry   tmp;
    PyObject            *value;
    static char         *kwlist[] = {"Value", NULL};
    int                 loc;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!", kwlist,
                &PyDict_Type, &(value)))
        return NULL;

    if (!CalendarFromPython(value, &entry, 0)) return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->AddCalendar(&(self->s), &entry);

    /* Phone does not support adding, let's implement it here */
    if (error == ERR_NOTSUPPORTED || error == ERR_NOTIMPLEMENTED) {
        /* Speed up lookup by starting from last location */
        loc = self->calendar_entry_cache;
        error2 = ERR_NONE;
        while (error2 == ERR_NONE) {
            tmp.Location = loc;
            loc++;
            error2 = self->s.Phone.Functions->GetCalendar(&(self->s), &tmp);
        }
        if (error2 == ERR_EMPTY) {
            /* Found what wanted, use regullar add */
            self->calendar_entry_cache = loc;
            entry.Location = tmp.Location;
            error = self->s.Phone.Functions->SetCalendar(&(self->s), &entry);
        } else if (error2 == ERR_INVALIDLOCATION) {
            /* Indicate full memory */
            error = ERR_FULL;
        } else {
            /* Propagate error */
            error = error2;
        }
    }
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "AddCalendar")) return NULL;

    return PyInt_FromLong(entry.Location);
}

/******************/
/* DeleteCalendar */
/******************/

static char StateMachine_DeleteCalendar__doc__[] =
"DeleteCalendar(Location)\n\n"
"Deletes calendar entry.\n\n"
"@param Location: Calendar entry to delete\n"
"@type Location: int\n"
"@return: None\n"
;

static PyObject *
StateMachine_DeleteCalendar(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_CalendarEntry   entry;
    static char         *kwlist[] = {"Location", NULL};

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "i", kwlist,
                &(entry.Location)))
        return NULL;

    /* Move hint for adding entries */
    self->calendar_entry_cache = entry.Location;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->DeleteCalendar(&(self->s), &entry);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "DeleteCalendar")) return NULL;

    Py_RETURN_NONE;
}

/*********************/
/* DeleteAllCalendar */
/*********************/

static char StateMachine_DeleteAllCalendar__doc__[] =
"DeleteAllCalendar()\n\n"
"Deletes all calendar entries.\n\n"
"@return: None\n"
;

static PyObject *
StateMachine_DeleteAllCalendar(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->DeleteAllCalendar(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "DeleteAllCalendar")) return NULL;

    Py_RETURN_NONE;
}

#if 0
/***********************/
/* GetCalendarSettings */
/***********************/

static char StateMachine_GetCalendarSettings__doc__[] =
"Reads calendar settings."
;

static PyObject *
StateMachine_GetCalendarSettings(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    CHECK_EXISTING_CONNECTION

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetCalendarSettings(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetCalendarSettings")) return NULL;

    Py_RETURN_NONE;
}

/***********************/
/* SetCalendarSettings */
/***********************/

static char StateMachine_SetCalendarSettings__doc__[] =
"Sets calendar settings."
;

static PyObject *
StateMachine_SetCalendarSettings(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    CHECK_EXISTING_CONNECTION

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetCalendarSettings(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetCalendarSettings")) return NULL;

    Py_RETURN_NONE;
}

/***********/
/* GetNote */
/***********/

static char StateMachine_GetNote__doc__[] =
"Gets note."
;

static PyObject *
StateMachine_GetNote(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    CHECK_EXISTING_CONNECTION

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetNote(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetNote")) return NULL;

    Py_RETURN_NONE;
}

/**************/
/* GetProfile */
/**************/

static char StateMachine_GetProfile__doc__[] =
"Reads profile."
;

static PyObject *
StateMachine_GetProfile(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    CHECK_EXISTING_CONNECTION

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetProfile(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetProfile")) return NULL;

    Py_RETURN_NONE;
}

/**************/
/* SetProfile */
/**************/

static char StateMachine_SetProfile__doc__[] =
"Updates profile."
;

static PyObject *
StateMachine_SetProfile(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    CHECK_EXISTING_CONNECTION

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetProfile(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetProfile")) return NULL;

    Py_RETURN_NONE;
}

/****************/
/* GetFMStation */
/****************/

static char StateMachine_GetFMStation__doc__[] =
"Reads FM station."
;

static PyObject *
StateMachine_GetFMStation(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    CHECK_EXISTING_CONNECTION

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetFMStation(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetFMStation")) return NULL;

    Py_RETURN_NONE;
}

/****************/
/* SetFMStation */
/****************/

static char StateMachine_SetFMStation__doc__[] =
"Sets FM station."
;

static PyObject *
StateMachine_SetFMStation(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    CHECK_EXISTING_CONNECTION

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetFMStation(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetFMStation")) return NULL;

    Py_RETURN_NONE;
}

/*******************/
/* ClearFMStations */
/*******************/

static char StateMachine_ClearFMStations__doc__[] =
"Clears defined FM stations."
;

static PyObject *
StateMachine_ClearFMStations(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    CHECK_EXISTING_CONNECTION

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->ClearFMStations(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "ClearFMStations")) return NULL;

    Py_RETURN_NONE;
}
#endif

/*********************/
/* GetNextFileFolder */
/*********************/

static char StateMachine_GetNextFileFolder__doc__[] =
"GetNextFileFolder(Start)\n\n"
"Gets next filename from filesystem.\n\n"
"@param Start: Whether we're starting listing. Defaults to False.\n"
"@type Start: bool\n"
"@return: File data as hash\n"
;

static PyObject *
StateMachine_GetNextFileFolder(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    static char         *kwlist[] = {"Start", NULL};
    int                 start = false;
    GSM_File            File;

    CHECK_EXISTING_CONNECTION

    memset(&File, 0, sizeof(File));

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|i", kwlist,
                &start))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetNextFileFolder(&(self->s), &File, start);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetNextFileFolder")) return NULL;

    return FileToPython(&File);
}

/*********************/
/* GetFolderListing */
/*********************/

static char StateMachine_GetFolderListing__doc__[] =
"GetFolderListing(Folder, Start)\n\n"
"Gets next filename from filesystem folder.\n\n"
"@param Folder: Folder to list\n"
"@type Folder: string\n"
"@param Start: Whether we're starting listing. Defaults to False.\n"
"@type Start: bool\n"
"@return: File data as hash\n"
;

static PyObject *
StateMachine_GetFolderListing(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    static char         *kwlist[] = {"Folder", "Start", NULL};
    PyObject            *folder_p;
    unsigned char       *folder_g;
    int                 start = false;
    GSM_File            File;

    CHECK_EXISTING_CONNECTION

    memset(&File, 0, sizeof(File));

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "U|i", kwlist,
                &folder_p, &start))
        return NULL;

    folder_g = StringPythonToGammu(folder_p);
    CopyUnicodeString(File.ID_FullName, folder_g);
    free(folder_g);

    File.Folder = true;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetFolderListing(&(self->s), &File, start);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetFolderListing")) return NULL;

    return FileToPython(&File);
}

/*********************/
/* GetNextRootFolder */
/*********************/

static char StateMachine_GetNextRootFolder__doc__[] =
"GetNextRootFolder(Folder)\n\n"
"Gets next root folder from filesystem. Start with empty folder name.\n\n"
"@param Folder: Previous read fodlder. Start with empty folder name.\n"
"@type Folder: string\n"
"@return: Structure with folder information\n"

;

static PyObject *
StateMachine_GetNextRootFolder(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    static char         *kwlist[] = {"Folder", NULL};
    PyObject            *folder_p;
    unsigned char       *folder_g;
    GSM_File            File;

    CHECK_EXISTING_CONNECTION

    memset(&File, 0, sizeof(File));

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "U", kwlist,
                &folder_p))
        return NULL;

    folder_g = StringPythonToGammu(folder_p);
    CopyUnicodeString(File.ID_FullName, folder_g);
    free(folder_g);

    File.Folder = true;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetNextRootFolder(&(self->s), &File);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetNextRootFolder")) return NULL;

    return FileToPython(&File);
}

/*********************/
/* SetFileAttributes */
/*********************/

static char StateMachine_SetFileAttributes__doc__[] =
"SetFileAttributes(Filename, ReadOnly, Protected, System, Hidden)\n\n"
"Sets file attributes.\n\n"
"@param Filename: File to modify\n"
"@type Filename: string\n"
"@param ReadOnly: Whether file is read only. Default to False.\n"
"@type ReadOnly: bool\n"
"@param Protected: Whether file is protected. Default to False.\n"
"@type Protected: bool\n"
"@param System: Whether file is system. Default to False.\n"
"@type System: bool\n"
"@param Hidden: Whether file is hidden. Default to False.\n"
"@type Hidden: bool\n"
;

static PyObject *
StateMachine_SetFileAttributes(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    static char         *kwlist[] = {"Folder", "ReadOnly", "Protected", "System", "Hidden", NULL};
    PyObject            *folder_p;
    unsigned char       *folder_g;
    int                 readonly = 0, protected = 0, system = 0, hidden = 0;
    GSM_File            File;

    CHECK_EXISTING_CONNECTION

    memset(&File, 0, sizeof(File));

	File.ReadOnly  = false;
	File.Protected = false;
	File.System    = false;
	File.Hidden    = false;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "U|iiii", kwlist,
                &folder_p, &readonly, &protected, &system, &hidden))
        return NULL;

    if (readonly  > 0) File.ReadOnly  = true;
    if (protected > 0) File.Protected = true;
    if (system    > 0) File.System    = true;
    if (hidden    > 0) File.Hidden    = true;

    folder_g = StringPythonToGammu(folder_p);
    CopyUnicodeString(File.ID_FullName, folder_g);
    free(folder_g);

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetFileAttributes(&(self->s), &File);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetFileAttributes")) return NULL;

    Py_RETURN_NONE;
}

/***************/
/* GetFilePart */
/***************/

static char StateMachine_GetFilePart__doc__[] =
"GetFilePart(File)\n\n"
"Gets file part from filesystem.\n\n"
"@param File: File data\n"
"@type File: hash\n"
"@return: File data for subsequent calls (Finished indicates transfer has been completed)\n"
;

static PyObject *
StateMachine_GetFilePart(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    static char         *kwlist[] = {"File", NULL};
    PyObject            *value, *result;
    GSM_File            File;
    int                 handle, size;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!", kwlist,
                &PyDict_Type, &(value)))
        return NULL;

    if (!FileFromPython(value, &File, false)) return NULL;

    handle = GetIntFromDict(value, "Handle");
    if (handle == INT_INVALID) {
        handle = 0;
        PyErr_Clear();
    }
    size = GetIntFromDict(value, "Size");
    if (size == INT_INVALID) {
        size = 0;
        PyErr_Clear();
    }

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetFilePart(&(self->s), &File, &handle, &size);
    END_PHONE_COMM

    // We can not throw exception here, we need to return rest of data
    if (error != ERR_EMPTY) {
        if (!checkError(&(self->s), error, "GetFilePart")) return NULL;
    }

    result = FileToPython(&File);

    free(File.Buffer);
    File.Buffer = NULL;

    if (result == NULL) return NULL;

    value = PyInt_FromLong(size);
    if (value == NULL) {
        Py_DECREF(result);
        return NULL;
    }
    PyDict_SetItemString(result, "Size", value);
    Py_DECREF(value);

    value = PyInt_FromLong(handle);
    if (value == NULL) {
        Py_DECREF(result);
        return NULL;
    }
    PyDict_SetItemString(result, "Handle", value);
    Py_DECREF(value);

    PyDict_SetItemString(result, "Finished", (error == ERR_EMPTY) ? Py_True : Py_False);

    return result;
}

/***************/
/* AddFilePart */
/***************/

static char StateMachine_AddFilePart__doc__[] =
"AddFilePart(File)\n\n"
"Adds file part to filesystem.\n\n"
"@param File: File data\n"
"@type File: hash\n"
"@return: File data for subsequent calls (Finished indicates transfer has been completed)\n"
;

static PyObject *
StateMachine_AddFilePart(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    static char         *kwlist[] = {"File", NULL};
    PyObject            *value, *result;
    GSM_File            File;
    int                 handle, pos;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!", kwlist,
                &PyDict_Type, &(value)))
        return NULL;

    if (!FileFromPython(value, &File, true)) return NULL;

    handle = GetIntFromDict(value, "Handle");
    if (handle == INT_INVALID) {
        handle = 0;
        PyErr_Clear();
    }
    pos = GetIntFromDict(value, "Pos");
    if (pos == INT_INVALID) {
        pos = 0;
        PyErr_Clear();
    }

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->AddFilePart(&(self->s), &File, &pos, &handle);
    END_PHONE_COMM

    // Do not throw exception here to be compatible with GetFilePart
    if (error != ERR_EMPTY) {
        if (!checkError(&(self->s), error, "AddFilePart")) return NULL;
    }

    result = FileToPython(&File);

    free(File.Buffer);
    File.Buffer = NULL;

    if (result == NULL) return NULL;

    value = PyInt_FromLong(pos);
    if (value == NULL) {
        Py_DECREF(result);
        return NULL;
    }
    PyDict_SetItemString(result, "Pos", value);
    Py_DECREF(value);

    value = PyInt_FromLong(handle);
    if (value == NULL) {
        Py_DECREF(result);
        return NULL;
    }
    PyDict_SetItemString(result, "Handle", value);
    Py_DECREF(value);

    PyDict_SetItemString(result, "Finished", (error == ERR_EMPTY) ? Py_True : Py_False);

    return result;
}

/***********************/
/* GetFileSystemStatus */
/***********************/

static char StateMachine_GetFileSystemStatus__doc__[] =
"GetFileSystemStatus()\n\n"
"Acquires filesystem status.\n\n"
"@return: Hash containing filesystem status (Used and Free)\n"
;

static PyObject *
StateMachine_GetFileSystemStatus(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error               error;
    GSM_FileSystemStatus    Status;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetFileSystemStatus(&(self->s), &Status);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetFileSystemStatus")) return NULL;

    return Py_BuildValue("{s:i,s:i}", "Used", Status.Used, "Free", Status.Free);
}

/**************/
/* DeleteFile */
/**************/

static char StateMachine_DeleteFile__doc__[] =
"DeleteFile(FileID)\n\n"
"Deletes file from filesystem.\n\n"
"@param FileID: File to delete\n"
"@type FileID: string\n"
"@return: None\n"
;

static PyObject *
StateMachine_DeleteFile(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    static char         *kwlist[] = {"File", NULL};
    PyObject            *folder_p;
    unsigned char       *folder_g;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "U", kwlist,
                &folder_p))
        return NULL;

    folder_g = StringPythonToGammu(folder_p);

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->DeleteFile(&(self->s), folder_g);
    END_PHONE_COMM

    free(folder_g);

    if (!checkError(&(self->s), error, "DeleteFile")) return NULL;

    Py_RETURN_NONE;
}

/*************/
/* AddFolder */
/*************/

static char StateMachine_AddFolder__doc__[] =
"AddFolder(ParentFolderID, Name)\n\n"
"Adds folder to filesystem.\n\n"
"@param ParentFolderID: Folder where to create subfolder\n"
"@type ParentFolderID: string\n"
"@param Name: New folder name\n"
"@type Name: string\n"
"@return: New folder ID.\n"
;

static PyObject *
StateMachine_AddFolder(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    static char         *kwlist[] = {"FolderID", "Name", NULL};
    PyObject            *folder_p, *name_p;
    unsigned char       *folder_g, *name_g;
    GSM_File            File;

    CHECK_EXISTING_CONNECTION

    memset(&File, 0, sizeof(File));

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "UU", kwlist,
                &folder_p, &name_p))
        return NULL;

	File.ReadOnly  = false;
	File.Protected = false;
	File.System    = false;
	File.Hidden    = false;

    folder_g = StringPythonToGammu(folder_p);
    name_g = StringPythonToGammu(name_p);
    CopyUnicodeString(File.ID_FullName, folder_g);
    CopyUnicodeString(File.Name, name_g);
    free(folder_g);
    free(name_g);

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->AddFolder(&(self->s), &File);
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "AddFolder")) return NULL;

    return UnicodeStringToPython(File.ID_FullName);
}

/****************/
/* DeleteFolder */
/****************/

static char StateMachine_DeleteFolder__doc__[] =
"DeleteFolder(FolderID)\n\n"
"Deletes folder on filesystem.\n\n"
"@param FolderID: Folder to delete\n"
"@type FolderID: string\n"
"@return: None\n"
;

static PyObject *
StateMachine_DeleteFolder(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    static char         *kwlist[] = {"FolderID", NULL};
    PyObject            *folder_p;
    unsigned char       *folder_g;

    CHECK_EXISTING_CONNECTION

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "U", kwlist,
                &folder_p))
        return NULL;

    folder_g = StringPythonToGammu(folder_p);

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->DeleteFolder(&(self->s), folder_g);
    END_PHONE_COMM
    free(folder_g);

    if (!checkError(&(self->s), error, "DeleteFolder")) return NULL;

    Py_RETURN_NONE;
}

#if 0

/**********************/
/* GetGPRSAccessPoint */
/**********************/

static char StateMachine_GetGPRSAccessPoint__doc__[] =
"Gets GPRS access point."
;

static PyObject *
StateMachine_GetGPRSAccessPoint(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    CHECK_EXISTING_CONNECTION

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->GetGPRSAccessPoint(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "GetGPRSAccessPoint")) return NULL;

    Py_RETURN_NONE;
}

/**********************/
/* SetGPRSAccessPoint */
/**********************/

static char StateMachine_SetGPRSAccessPoint__doc__[] =
"Sets GPRS access point."
;

static PyObject *
StateMachine_SetGPRSAccessPoint(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;

    CHECK_EXISTING_CONNECTION

    BEGIN_PHONE_COMM
    error = self->s.Phone.Functions->SetGPRSAccessPoint(&(self->s));
    END_PHONE_COMM

    if (!checkError(&(self->s), error, "SetGPRSAccessPoint")) return NULL;

    Py_RETURN_NONE;
}
#endif

static char StateMachine_SetDebugFile__doc__[] =
"SetDebugFile(File, Global)\n\n"
"Sets state machine debug file.\n\n"
"@param File: File where to write debug stuff (as configured by L{SetDebugLevel}). Can be either None for no file, Python file object or filename.\n"
"@type File: mixed\n"
"@param Global: Whether to use global debug structure (overrides File)\n"
"@type Global: bool\n"
"@return: None\n"
;

static PyObject *
StateMachine_SetDebugFile(StateMachineObject *self, PyObject *args, PyObject *kwds)
{
    PyObject            *value;
    char                *s;
    int                 global = 0;
    FILE                *f;
    static char         *kwlist[] = {"File", "Global", NULL};
    GSM_Error           error;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|i", kwlist, &value, &global))
        return NULL;

    if (!self->s.di.use_global && global) {
        if (self->DebugFile != NULL) {
            Py_DECREF(self->DebugFile);
            DebugFile = NULL;
        } else if (self->s.di.df != NULL && fileno(self->s.di.df) != 1 && fileno(self->s.di.df) != 2) {
            fclose(self->s.di.df);
        }
    }

    self->s.di.use_global = global;


    if (value == Py_None) {
        if (self->DebugFile != NULL) {
            Py_DECREF(self->DebugFile);
            self->DebugFile = NULL;
        } else if (self->s.di.df != NULL && fileno(self->s.di.df) != 1 && fileno(self->s.di.df) != 2) {
            fclose(self->s.di.df);
        }
        self->s.di.df = NULL;
    } else if (PyFile_Check(value)) {
        if (self->DebugFile != NULL) {
            Py_DECREF(self->DebugFile);
            self->DebugFile = NULL;
        } else if (self->s.di.df != NULL && fileno(self->s.di.df) != 1 && fileno(self->s.di.df) != 2) {
            fclose(self->s.di.df);
        }
        f = PyFile_AsFile(value);
        if (f == NULL) return NULL;
        self->DebugFile = value;
        Py_INCREF(self->DebugFile);
        self->s.di.df = f;
    } else if (PyString_Check(value)) {
        if (self->DebugFile != NULL) {
            Py_DECREF(self->DebugFile);
            self->DebugFile = NULL;
        } else if (self->s.di.df != NULL && fileno(self->s.di.df) != 1 && fileno(self->s.di.df) != 2) {
            fclose(self->s.di.df);
        }
        s = PyString_AsString(value);
        if (s == NULL) return NULL;
        error = GSM_SetDebugFile(s, &(self->s.di));
        if (!checkError(NULL, error, "SetDebugFile")) return NULL;
    } else {
        PyErr_SetString(PyExc_TypeError, "Valid are only None, string or file parameters!");
        return NULL;
    }

    Py_RETURN_NONE;
}

static char StateMachine_SetDebugLevel__doc__[] =
"SetDebugLevel(Level)\n\n"
"Sets state machine debug level accorting to passed string. You need to configure output file using L{SetDebugFile} to activate it.\n\n"
"@type Level: string\n"
"@param Level: name of debug level to use, currently one of:\n"
"   - nothing\n"
"   - text\n"
"   - textall\n"
"   - binary\n"
"   - errors\n"
"   - textdate\n"
"   - textalldate\n"
"   - errorsdate\n"
"@return: None\n"
;

static PyObject *
StateMachine_SetDebugLevel(StateMachineObject *self, PyObject *args, PyObject *kwds)
{
    char                *level;
    static char         *kwlist[] = {"Level", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist, &level))
        return NULL;

    if (!GSM_SetDebugLevel(level, &(self->s.di))) {
        PyErr_SetString(PyExc_ValueError, level);
        return NULL;
    }

    Py_RETURN_NONE;
}

static char StateMachine_SetIncomingCallback__doc__[] =
"SetIncomingCallback(Callback)\n\n"
"Sets callback function which is called whenever any (enabled) incoming event appears. Please note that you have to enable each event type by calling SetIncoming* functions.\n\n"
"@param Callback: callback function or None for disabling\n"
"@type Callback: function, it will get three params: StateMachine object, event type and it's data in dictionary\n"
"@return: None\n"
;

static PyObject *
StateMachine_SetIncomingCallback(StateMachineObject *self, PyObject *args, PyObject *kwds)
{
    static char         *kwlist[] = {"Callback", NULL};
    PyObject            *temp;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O", kwlist, &temp))
        return NULL;

    if (temp == Py_None) {
        temp = NULL;
    } else if (!PyCallable_Check(temp)) {
        PyErr_SetString(PyExc_TypeError, "Parameter must be callable");
        return NULL;
    }

    /* Use X variants here as we might have NULL there and those handle it gracefully. */
    Py_XDECREF(self->IncomingCallback);
    self->IncomingCallback = temp;
    Py_XINCREF(self->IncomingCallback);

    Py_RETURN_NONE;
}

static struct PyMethodDef StateMachine_methods[] = {
    {"ReadConfig",	(PyCFunction)StateMachine_ReadConfig,	METH_VARARGS|METH_KEYWORDS,	StateMachine_ReadConfig__doc__},
    {"SetConfig",	(PyCFunction)StateMachine_SetConfig,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetConfig__doc__},
    {"GetConfig",	(PyCFunction)StateMachine_GetConfig,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetConfig__doc__},
    {"Init",	(PyCFunction)StateMachine_Init,	METH_VARARGS|METH_KEYWORDS,	StateMachine_Init__doc__},
    {"Terminate",	(PyCFunction)StateMachine_Terminate,	METH_VARARGS|METH_KEYWORDS,	StateMachine_Terminate__doc__},
    {"ShowStartInfo",	(PyCFunction)StateMachine_ShowStartInfo,	METH_VARARGS|METH_KEYWORDS,	StateMachine_ShowStartInfo__doc__},
    {"GetManufacturer",	(PyCFunction)StateMachine_GetManufacturer,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetManufacturer__doc__},
    {"GetModel",	(PyCFunction)StateMachine_GetModel,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetModel__doc__},
    {"GetFirmware",	(PyCFunction)StateMachine_GetFirmware,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetFirmware__doc__},
    {"GetIMEI",	(PyCFunction)StateMachine_GetIMEI,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetIMEI__doc__},
    {"GetOriginalIMEI",	(PyCFunction)StateMachine_GetOriginalIMEI,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetOriginalIMEI__doc__},
    {"GetManufactureMonth",	(PyCFunction)StateMachine_GetManufactureMonth,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetManufactureMonth__doc__},
    {"GetProductCode",	(PyCFunction)StateMachine_GetProductCode,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetProductCode__doc__},
    {"GetHardware",	(PyCFunction)StateMachine_GetHardware,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetHardware__doc__},
    {"GetPPM",	(PyCFunction)StateMachine_GetPPM,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetPPM__doc__},
    {"GetSIMIMSI",	(PyCFunction)StateMachine_GetSIMIMSI,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetSIMIMSI__doc__},
    {"GetDateTime",	(PyCFunction)StateMachine_GetDateTime,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetDateTime__doc__},
    {"SetDateTime",	(PyCFunction)StateMachine_SetDateTime,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetDateTime__doc__},
    {"GetAlarm",	(PyCFunction)StateMachine_GetAlarm,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetAlarm__doc__},
    {"SetAlarm",	(PyCFunction)StateMachine_SetAlarm,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetAlarm__doc__},
    {"GetLocale",	(PyCFunction)StateMachine_GetLocale,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetLocale__doc__},
    {"SetLocale",	(PyCFunction)StateMachine_SetLocale,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetLocale__doc__},
    {"PressKey",	(PyCFunction)StateMachine_PressKey,	METH_VARARGS|METH_KEYWORDS,	StateMachine_PressKey__doc__},
    {"Reset",	(PyCFunction)StateMachine_Reset,	METH_VARARGS|METH_KEYWORDS,	StateMachine_Reset__doc__},
    {"ResetPhoneSettings",	(PyCFunction)StateMachine_ResetPhoneSettings,	METH_VARARGS|METH_KEYWORDS,	StateMachine_ResetPhoneSettings__doc__},
    {"EnterSecurityCode",	(PyCFunction)StateMachine_EnterSecurityCode,	METH_VARARGS|METH_KEYWORDS,	StateMachine_EnterSecurityCode__doc__},
    {"GetSecurityStatus",	(PyCFunction)StateMachine_GetSecurityStatus,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetSecurityStatus__doc__},
    {"GetDisplayStatus",	(PyCFunction)StateMachine_GetDisplayStatus,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetDisplayStatus__doc__},
    {"SetAutoNetworkLogin",	(PyCFunction)StateMachine_SetAutoNetworkLogin,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetAutoNetworkLogin__doc__},
    {"GetBatteryCharge",	(PyCFunction)StateMachine_GetBatteryCharge,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetBatteryCharge__doc__},
    {"GetSignalQuality",	(PyCFunction)StateMachine_GetSignalQuality,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetSignalQuality__doc__},
    {"GetNetworkInfo",	(PyCFunction)StateMachine_GetNetworkInfo,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetNetworkInfo__doc__},
    {"GetCategory",	(PyCFunction)StateMachine_GetCategory,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetCategory__doc__},
    {"AddCategory",	(PyCFunction)StateMachine_AddCategory,	METH_VARARGS|METH_KEYWORDS,	StateMachine_AddCategory__doc__},
    {"GetCategoryStatus",	(PyCFunction)StateMachine_GetCategoryStatus,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetCategoryStatus__doc__},
    {"GetMemoryStatus",	(PyCFunction)StateMachine_GetMemoryStatus,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetMemoryStatus__doc__},
    {"GetMemory",	(PyCFunction)StateMachine_GetMemory,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetMemory__doc__},
    {"GetNextMemory",	(PyCFunction)StateMachine_GetNextMemory,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetNextMemory__doc__},
    {"SetMemory",	(PyCFunction)StateMachine_SetMemory,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetMemory__doc__},
    {"AddMemory",	(PyCFunction)StateMachine_AddMemory,	METH_VARARGS|METH_KEYWORDS,	StateMachine_AddMemory__doc__},
    {"DeleteMemory",	(PyCFunction)StateMachine_DeleteMemory,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DeleteMemory__doc__},
    {"DeleteAllMemory",	(PyCFunction)StateMachine_DeleteAllMemory,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DeleteAllMemory__doc__},
    {"GetSpeedDial",	(PyCFunction)StateMachine_GetSpeedDial,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetSpeedDial__doc__},
    {"SetSpeedDial",	(PyCFunction)StateMachine_SetSpeedDial,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetSpeedDial__doc__},
    {"GetSMSC",	(PyCFunction)StateMachine_GetSMSC,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetSMSC__doc__},
    {"SetSMSC",	(PyCFunction)StateMachine_SetSMSC,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetSMSC__doc__},
    {"GetSMSStatus",	(PyCFunction)StateMachine_GetSMSStatus,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetSMSStatus__doc__},
    {"GetSMS",	(PyCFunction)StateMachine_GetSMS,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetSMS__doc__},
    {"GetNextSMS",	(PyCFunction)StateMachine_GetNextSMS,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetNextSMS__doc__},
    {"SetSMS",	(PyCFunction)StateMachine_SetSMS,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetSMS__doc__},
    {"AddSMS",	(PyCFunction)StateMachine_AddSMS,	METH_VARARGS|METH_KEYWORDS,	StateMachine_AddSMS__doc__},
    {"DeleteSMS",	(PyCFunction)StateMachine_DeleteSMS,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DeleteSMS__doc__},
    {"SendSMS",	(PyCFunction)StateMachine_SendSMS,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SendSMS__doc__},
    {"SendSavedSMS",	(PyCFunction)StateMachine_SendSavedSMS,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SendSavedSMS__doc__},
    {"SetIncomingSMS",	(PyCFunction)StateMachine_SetIncomingSMS,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetIncomingSMS__doc__},
    {"SetIncomingCB",	(PyCFunction)StateMachine_SetIncomingCB,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetIncomingCB__doc__},
    {"SetIncomingCall",	(PyCFunction)StateMachine_SetIncomingCall,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetIncomingCall__doc__},
    {"SetIncomingUSSD",	(PyCFunction)StateMachine_SetIncomingUSSD,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetIncomingUSSD__doc__},
    {"GetSMSFolders",	(PyCFunction)StateMachine_GetSMSFolders,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetSMSFolders__doc__},
    {"AddSMSFolder",	(PyCFunction)StateMachine_AddSMSFolder,	METH_VARARGS|METH_KEYWORDS,	StateMachine_AddSMSFolder__doc__},
    {"DeleteSMSFolder",	(PyCFunction)StateMachine_DeleteSMSFolder,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DeleteSMSFolder__doc__},
    {"DialVoice",	(PyCFunction)StateMachine_DialVoice,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DialVoice__doc__},
#if 0
    {"AnswerCall",	(PyCFunction)StateMachine_AnswerCall,	METH_VARARGS|METH_KEYWORDS,	StateMachine_AnswerCall__doc__},
    {"CancelCall",	(PyCFunction)StateMachine_CancelCall,	METH_VARARGS|METH_KEYWORDS,	StateMachine_CancelCall__doc__},
    {"HoldCall",	(PyCFunction)StateMachine_HoldCall,	METH_VARARGS|METH_KEYWORDS,	StateMachine_HoldCall__doc__},
    {"UnholdCall",	(PyCFunction)StateMachine_UnholdCall,	METH_VARARGS|METH_KEYWORDS,	StateMachine_UnholdCall__doc__},
    {"ConferenceCall",	(PyCFunction)StateMachine_ConferenceCall,	METH_VARARGS|METH_KEYWORDS,	StateMachine_ConferenceCall__doc__},
    {"SplitCall",	(PyCFunction)StateMachine_SplitCall,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SplitCall__doc__},
    {"TransferCall",	(PyCFunction)StateMachine_TransferCall,	METH_VARARGS|METH_KEYWORDS,	StateMachine_TransferCall__doc__},
    {"SwitchCall",	(PyCFunction)StateMachine_SwitchCall,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SwitchCall__doc__},
    {"GetCallDivert",	(PyCFunction)StateMachine_GetCallDivert,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetCallDivert__doc__},
    {"SetCallDivert",	(PyCFunction)StateMachine_SetCallDivert,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetCallDivert__doc__},
    {"CancelAllDiverts",	(PyCFunction)StateMachine_CancelAllDiverts,	METH_VARARGS|METH_KEYWORDS,	StateMachine_CancelAllDiverts__doc__},
    {"SendDTMF",	(PyCFunction)StateMachine_SendDTMF,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SendDTMF__doc__},
    {"GetRingtone",	(PyCFunction)StateMachine_GetRingtone,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetRingtone__doc__},
    {"SetRingtone",	(PyCFunction)StateMachine_SetRingtone,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetRingtone__doc__},
    {"GetRingtonesInfo",	(PyCFunction)StateMachine_GetRingtonesInfo,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetRingtonesInfo__doc__},
    {"DeleteUserRingtones",	(PyCFunction)StateMachine_DeleteUserRingtones,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DeleteUserRingtones__doc__},
    {"PlayTone",	(PyCFunction)StateMachine_PlayTone,	METH_VARARGS|METH_KEYWORDS,	StateMachine_PlayTone__doc__},
    {"GetWAPBookmark",	(PyCFunction)StateMachine_GetWAPBookmark,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetWAPBookmark__doc__},
    {"SetWAPBookmark",	(PyCFunction)StateMachine_SetWAPBookmark,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetWAPBookmark__doc__},
    {"DeleteWAPBookmark",	(PyCFunction)StateMachine_DeleteWAPBookmark,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DeleteWAPBookmark__doc__},
    {"GetWAPSettings",	(PyCFunction)StateMachine_GetWAPSettings,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetWAPSettings__doc__},
    {"SetWAPSettings",	(PyCFunction)StateMachine_SetWAPSettings,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetWAPSettings__doc__},
    {"GetMMSSettings",	(PyCFunction)StateMachine_GetMMSSettings,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetMMSSettings__doc__},
    {"SetMMSSettings",	(PyCFunction)StateMachine_SetMMSSettings,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetMMSSettings__doc__},
    {"GetBitmap",	(PyCFunction)StateMachine_GetBitmap,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetBitmap__doc__},
    {"SetBitmap",	(PyCFunction)StateMachine_SetBitmap,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetBitmap__doc__},
#endif
    {"GetToDoStatus",	(PyCFunction)StateMachine_GetToDoStatus,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetToDoStatus__doc__},
    {"GetToDo",	(PyCFunction)StateMachine_GetToDo,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetToDo__doc__},
    {"GetNextToDo",	(PyCFunction)StateMachine_GetNextToDo,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetNextToDo__doc__},
    {"SetToDo",	(PyCFunction)StateMachine_SetToDo,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetToDo__doc__},
    {"AddToDo",	(PyCFunction)StateMachine_AddToDo,	METH_VARARGS|METH_KEYWORDS,	StateMachine_AddToDo__doc__},
    {"DeleteToDo",	(PyCFunction)StateMachine_DeleteToDo,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DeleteToDo__doc__},
    {"DeleteAllToDo",	(PyCFunction)StateMachine_DeleteAllToDo,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DeleteAllToDo__doc__},
    {"GetCalendarStatus",	(PyCFunction)StateMachine_GetCalendarStatus,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetCalendarStatus__doc__},
    {"GetCalendar",	(PyCFunction)StateMachine_GetCalendar,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetCalendar__doc__},
    {"GetNextCalendar",	(PyCFunction)StateMachine_GetNextCalendar,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetNextCalendar__doc__},
    {"SetCalendar",	(PyCFunction)StateMachine_SetCalendar,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetCalendar__doc__},
    {"AddCalendar",	(PyCFunction)StateMachine_AddCalendar,	METH_VARARGS|METH_KEYWORDS,	StateMachine_AddCalendar__doc__},
    {"DeleteCalendar",	(PyCFunction)StateMachine_DeleteCalendar,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DeleteCalendar__doc__},
    {"DeleteAllCalendar",	(PyCFunction)StateMachine_DeleteAllCalendar,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DeleteAllCalendar__doc__},
#if 0
    {"GetCalendarSettings",	(PyCFunction)StateMachine_GetCalendarSettings,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetCalendarSettings__doc__},
    {"SetCalendarSettings",	(PyCFunction)StateMachine_SetCalendarSettings,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetCalendarSettings__doc__},
    {"GetNote",	(PyCFunction)StateMachine_GetNote,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetNote__doc__},
    {"GetProfile",	(PyCFunction)StateMachine_GetProfile,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetProfile__doc__},
    {"SetProfile",	(PyCFunction)StateMachine_SetProfile,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetProfile__doc__},
    {"GetFMStation",	(PyCFunction)StateMachine_GetFMStation,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetFMStation__doc__},
    {"SetFMStation",	(PyCFunction)StateMachine_SetFMStation,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetFMStation__doc__},
    {"ClearFMStations",	(PyCFunction)StateMachine_ClearFMStations,	METH_VARARGS|METH_KEYWORDS,	StateMachine_ClearFMStations__doc__},
#endif
    {"GetNextFileFolder",	(PyCFunction)StateMachine_GetNextFileFolder,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetNextFileFolder__doc__},
    {"GetFolderListing",	(PyCFunction)StateMachine_GetFolderListing,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetFolderListing__doc__},
    {"GetNextRootFolder",	(PyCFunction)StateMachine_GetNextRootFolder,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetNextRootFolder__doc__},
    {"SetFileAttributes",	(PyCFunction)StateMachine_SetFileAttributes,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetFileAttributes__doc__},
    {"GetFilePart",	(PyCFunction)StateMachine_GetFilePart,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetFilePart__doc__},
    {"AddFilePart",	(PyCFunction)StateMachine_AddFilePart,	METH_VARARGS|METH_KEYWORDS,	StateMachine_AddFilePart__doc__},
    {"GetFileSystemStatus",	(PyCFunction)StateMachine_GetFileSystemStatus,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetFileSystemStatus__doc__},
    {"DeleteFile",	(PyCFunction)StateMachine_DeleteFile,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DeleteFile__doc__},
    {"AddFolder",	(PyCFunction)StateMachine_AddFolder,	METH_VARARGS|METH_KEYWORDS,	StateMachine_AddFolder__doc__},
    {"DeleteFolder",	(PyCFunction)StateMachine_DeleteFolder,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DeleteFolder__doc__},
#if 0
    {"GetGPRSAccessPoint",	(PyCFunction)StateMachine_GetGPRSAccessPoint,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetGPRSAccessPoint__doc__},
    {"SetGPRSAccessPoint",	(PyCFunction)StateMachine_SetGPRSAccessPoint,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetGPRSAccessPoint__doc__},
#endif
    {"SetDebugFile",    (PyCFunction)StateMachine_SetDebugFile,    METH_VARARGS|METH_KEYWORDS,   StateMachine_SetDebugFile__doc__},
    {"SetDebugLevel",   (PyCFunction)StateMachine_SetDebugLevel,   METH_VARARGS|METH_KEYWORDS,   StateMachine_SetDebugLevel__doc__},

    {"SetIncomingCallback",   (PyCFunction)StateMachine_SetIncomingCallback,   METH_VARARGS|METH_KEYWORDS,   StateMachine_SetIncomingCallback__doc__},

    {NULL,		NULL}		/* sentinel */
};

/* ---------- */

#if 0
static PyObject *
StateMachine_getattr(StateMachineObject *self, char *name)
{
    /* XXXX Add your own getattr code here */
    return  -1;
//        Py_FindMethod(StateMachine_methods, (PyObject *)self, name);
}

static int
StateMachine_setattr(StateMachineObject *self, char *name, PyObject *v)
{
    /* Set attribute 'name' to value 'v'. v==NULL means delete */

    /* XXXX Add your own setattr code here */
    return -1;
}
#endif

#if 0
static int
StateMachine_compare(StateMachineObject *v, StateMachineObject *w)
{
    /* XXXX Compare objects and return -1, 0 or 1 */
}

static PyObject *
StateMachine_repr(StateMachineObject *self)
{
    PyObject *s;

    /* XXXX Add code here to put self into s */
    return s;
}

static PyObject *
StateMachine_str(StateMachineObject *self)
{
    PyObject *s;

    /* XXXX Add code here to put self into s */
    return s;
}
#endif

static void
StateMachine_dealloc(StateMachineObject *self)
{
    int                 i;

    BEGIN_PHONE_COMM
    if (self->s.opened) {
        GSM_TerminateConnection(&(self->s));
    }
    END_PHONE_COMM

    if (self->DebugFile != NULL) {
        Py_DECREF(self->DebugFile);
        DebugFile = NULL;
    }

    /* Free allocated memory */
    for (i = 0; i < MAX_CONFIG_NUM; i++) {
        free(self->s.Config[i].Device);
        free(self->s.Config[i].Connection);
        free(self->s.Config[i].SyncTime);
        free(self->s.Config[i].DebugFile);
        free(self->s.Config[i].LockDevice);
        free(self->s.Config[i].StartInfo);
    }

#ifdef WITH_THREAD
    PyThread_free_lock(self->mutex);
#endif
    free(self->s.msg);
    UnRegisterStateMachine(self);
    self->ob_type->tp_free((PyObject*)self);
}

static PyObject *
StateMachine_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    StateMachineObject *self;

    self = (StateMachineObject *)type->tp_alloc(type, 0);
    memset(&(self->s), 0, sizeof(self->s));

    return (PyObject *)self;
}

static int
StateMachine_init(StateMachineObject *self, PyObject *args, PyObject *kwds)
{
    char                *s = NULL;
    char                localefile[201];
    char                loc[3];
    static char         *kwlist[] = {"Locale", NULL};
    const char          *path;
    char                *locale;
    char                *bk_locale;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|s", kwlist, &s))
        return 0;

    if (s != NULL && strcmp(s, "auto") == 0) {
        s = NULL;
    }

    /* Reset our structures */
    memset(&(self->s), 0, sizeof(self->s));
    self->s.opened          = false;
    self->s.msg             = NULL;
    self->s.ConfigNum       = 0;
    self->DebugFile         = NULL;
    self->IncomingCallback  = NULL;

    self->IncomingCallQueue[0] = NULL;
    self->IncomingSMSQueue[0] = NULL;
    self->IncomingCBQueue[0] = NULL;
    self->IncomingUSSDQueue[0] = NULL;

    if (!RegisterStateMachine(self)) return 0;

    /* Create phone communication lock */
#ifdef WITH_THREAD
    self->mutex = PyThread_allocate_lock();
#endif

    /* FIXME: not sure how this works on Win32 */
    path = GetGammuLocalePath();
    if (path != NULL) {
#ifndef WIN32
        if (s == NULL) {
            locale = setlocale(LC_MESSAGES, "");
            bk_locale = strdup(locale);
            locale = setlocale(LC_MESSAGES, NULL);
            if (bk_locale != NULL) {
                setlocale(LC_MESSAGES, bk_locale);
                free(bk_locale);
            }
            if (locale != NULL) {
                mystrncpy(loc, locale, 2);
                s = loc;
            }
        }
#endif
        if (s != NULL) {
            snprintf(localefile, 200, "%s/gammu_%s.txt", path, s);
            self->s.msg = INI_ReadFile(localefile, true);
        }
    }

    return 1;
}

static char StateMachineType__doc__[] =
"StateNachine(Locale)\n\n"
"StateMachine object, that is used for communication with phone.\n\n"
// FIXME: following doc should go elsewhere
"param Locale: What locales to use for gammu error messages, default is 'auto' which does autodetection according to user locales\n"
"type Locale: string\n"
;

static PyTypeObject StateMachineType = {
    PyObject_HEAD_INIT(NULL)
    0,				/*ob_size*/
    "gammu.StateMachine",			/*tp_name*/
    sizeof(StateMachineObject),		/*tp_basicsize*/
    0,				/*tp_itemsize*/
    /* methods */
    (destructor)StateMachine_dealloc,	/*tp_dealloc*/
    (printfunc)0,		/*tp_print*/
#if 0
    (getattrfunc)StateMachine_getattr,	/*tp_getattr*/
    (setattrfunc)StateMachine_setattr,	/*tp_setattr*/
#endif
    0,	/*tp_getattr*/
    0,	/*tp_setattr*/
    0,
#if 0
	(cmpfunc)StateMachine_compare,		/*tp_compare*/
#endif
    0,
#if 0
	(reprfunc)StateMachine_repr,		/*tp_repr*/
#endif
    0,			/*tp_as_number*/
    0,		/*tp_as_sequence*/
    0,		/*tp_as_mapping*/
    (hashfunc)0,		/*tp_hash*/
    (ternaryfunc)0,		/*tp_call*/
    0,
#if 0
	(reprfunc)StateMachine_str,		/*tp_str*/
#endif
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT, /*tp_flags*/
    StateMachineType__doc__, /* Documentation string */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		               /* tp_iternext */
    StateMachine_methods,             /* tp_methods */
    0,             /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)StateMachine_init,      /* tp_init */
    0,                         /* tp_alloc */
    StateMachine_new                 /* tp_new */
};

/* End of code for StateMachine objects */
/* -------------------------------------------------------- */


static char gammu_Version__doc__[] =
"Version()\n\n"
"Get version information.\n"
"@return: Tuple of python-gammu and Gammu versions.\n"
;

static PyObject *
gammu_Version(PyObject *self)
{
    return Py_BuildValue("s,s", VERSION, PYTHON_GAMMU_VERSION);
}

static char gammu_SetDebugFile__doc__[] =
"SetDebugFile(File)\n\n"
"Sets global debug file.\n\n"
"@param File: File where to write debug stuff (as configured by L{SetDebugLevel}). Can be either None for no file, Python file object or filename.\n"
"@type File: mixed\n"
"@return: None\n"
;

static PyObject *
gammu_SetDebugFile(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyObject            *value;
    char                *s;
    FILE                *f;
    static char         *kwlist[] = {"File", NULL};
    GSM_Error           error;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O", kwlist, &value))
        return NULL;

    if (value == Py_None) {
        if (DebugFile != NULL) {
            Py_DECREF(DebugFile);
            DebugFile = NULL;
        } else if (di.df != NULL && fileno(di.df) != 1 && fileno(di.df) != 2) {
            fclose(di.df);
        }
        di.df = NULL;
    } else if (PyFile_Check(value)) {
        if (DebugFile != NULL) {
            Py_DECREF(DebugFile);
            DebugFile = NULL;
        } else if (di.df != NULL && fileno(di.df) != 1 && fileno(di.df) != 2) {
            fclose(di.df);
        }
        f = PyFile_AsFile(value);
        if (f == NULL) return NULL;
        DebugFile = value;
        Py_INCREF(DebugFile);
        di.df = f;
    } else if (PyString_Check(value)) {
        if (DebugFile != NULL) {
            Py_DECREF(DebugFile);
            DebugFile = NULL;
        } else if (di.df != NULL && fileno(di.df) != 1 && fileno(di.df) != 2) {
            fclose(di.df);
        }
        s = PyString_AsString(value);
        if (s == NULL) return NULL;
        error = GSM_SetDebugFile(s, &di);
        if (!checkError(NULL, error, "SetDebugFile")) return NULL;
    } else {
        PyErr_SetString(PyExc_TypeError, "Valid are only None, string or file parameters!");
        return NULL;
    }

    Py_RETURN_NONE;
}

static char gammu_SetDebugLevel__doc__[] =
"SetDebugLevel(Level)\n\n"
"Sets global debug level accorting to passed string. You need to configure output file using L{SetDebugFile} to activate it.\n\n"
"@type Level: string\n"
"@param Level: name of debug level to use, currently one of:\n"
"   - nothing\n"
"   - text\n"
"   - textall\n"
"   - binary\n"
"   - errors\n"
"   - textdate\n"
"   - textalldate\n"
"   - errorsdate\n"
"@return: None\n"
;

static PyObject *
gammu_SetDebugLevel(PyObject *self, PyObject *args, PyObject *kwds)
{
    char                *level;
    static char         *kwlist[] = {"Level", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist, &level))
        return NULL;

    if (!GSM_SetDebugLevel(level, &di)) {
        PyErr_SetString(PyExc_ValueError, level);
        return NULL;
    }

    Py_RETURN_NONE;
}

static char gammu_LinkSMS__doc__[] =
"LinkSMS(Messages, EMS)\n\n"
"Links multi part SMS messages.\n\n"
"@type Messages: list\n"
"@type EMS: bool\n"
"@param Messages: List of messages to link\n"
"@param EMS: Whether to detect ems, defauls to True\n"
"@return: List of linked messages\n"
;

static PyObject *
gammu_LinkSMS(PyObject *self, PyObject *args, PyObject *kwds)
{
    int                 ems = 1;
    GSM_MultiSMSMessage **smsin;
    GSM_MultiSMSMessage **smsout;
    static char         *kwlist[] = {"Messages", "EMS", NULL};
    GSM_Error           error;
    PyObject            *value;
    PyObject            *ret;
    int                 len;
    int                 i;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!|I", kwlist,
                &PyList_Type, &(value), &ems))
        return NULL;

    len = PyList_Size(value);

    if (!MultiSMSListFromPython(value, &smsin)) return NULL;

    smsout = (GSM_MultiSMSMessage **)malloc((len + 1) * sizeof(GSM_MultiSMSMessage *));

    error = GSM_LinkSMS(smsin, smsout, ems);
    if (!checkError(NULL, error, "LinkSMS")) return NULL;

    ret = MultiSMSListToPython(smsout);

    i = 0;
    while(smsout[i] != NULL) {
        free(smsout[i]);
        i++;
    }
    free(smsout);

    i = 0;
    while(smsin[i] != NULL) {
        free(smsin[i]);
        i++;
    }
    free(smsin);

    return ret;
}

static char gammu_DecodeSMS__doc__[] =
"DecodeSMS(Messages, EMS)\n\n"
"Decodes multi part SMS message.\n\n"
"@param Messages: Nessages to decode\n"
"@type Messages: list\n"
"@param EMS: Whether to use EMS, defalt to True\n"
"@type EMS: bool\n"
"@return: Multi part message information\n"
;

static PyObject *
gammu_DecodeSMS(PyObject *self, PyObject *args, PyObject *kwds)
{
    int                         ems = 1;
    GSM_MultiSMSMessage         smsin;
    GSM_MultiPartSMSInfo        smsinfo;
    static char                 *kwlist[] = {"Messages", "EMS", NULL};
    PyObject                    *value;
    PyObject                    *res;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!|I", kwlist,
                &PyList_Type, &(value), &ems))
        return NULL;

    if (!MultiSMSFromPython(value, &smsin)) return NULL;

    if (!GSM_DecodeMultiPartSMS(&smsinfo, &smsin, ems)) {
        GSM_FreeMultiPartSMSInfo(&smsinfo);
        Py_RETURN_NONE;
    }
    res = SMSInfoToPython(&smsinfo);

    GSM_FreeMultiPartSMSInfo(&smsinfo);

    return res;
}

static char gammu_EncodeSMS__doc__[] =
"EncodeSMS(MessageInfo)\n\n"
"Encodes multi part SMS message.\n\n"
"@param MessageInfo: Description of message\n"
"@type MessageInfo: dict\n"
"@return: Dictionary with raw message\n"
;

static PyObject *
gammu_EncodeSMS(PyObject *self, PyObject *args, PyObject *kwds)
{
    GSM_MultiSMSMessage         smsout;
    GSM_MultiPartSMSInfo        smsinfo;
    static char                 *kwlist[] = {"MessagesInfo", NULL};
    PyObject                    *value;
    PyObject                    *res;

    memset(&smsout, 0, sizeof(GSM_MultiSMSMessage));

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!", kwlist,
                &PyDict_Type, &(value)))
        return NULL;

    if (!SMSInfoFromPython(value, &smsinfo)) return NULL;

    if (!GSM_EncodeMultiPartSMS(&smsinfo, &smsout)) {
        GSM_FreeMultiPartSMSInfo(&smsinfo);
        Py_RETURN_NONE;
    }
    GSM_FreeMultiPartSMSInfo(&smsinfo);

    res = MultiSMSToPython(&smsout);

    return res;
}

static char gammu_SaveRingtone__doc__[] =
"SaveRingtone(Filename, Ringtone, Format)\n\n"
"Saves ringtone into file.\n\n"
"@param Filename: Name of file where ringote will be saved\n"
"@type Filename: string\n"
"@param Ringtone: Ringtone to save\n"
"@type Ringtone: dict\n"
"@param Format: One of 'ott', 'mid', 'rng', 'imy', 'wav', 'rttl'\n"
"@type Format: string\n"
"@return: None\n"
;

static PyObject *
gammu_SaveRingtone(PyObject *self, PyObject *args, PyObject *kwds)
{
    static char                 *kwlist[] = {"Filename", "Ringtone", "Format", NULL};
    PyObject                    *value;
    PyObject                    *file;
    char                        *s;
    char                        *name;
    FILE                        *f;
    GSM_Ringtone                ringtone;
    bool                        closefile = false;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "OO!s", kwlist,
                &file, &PyDict_Type, &(value), &s))
        return NULL;

    if (PyFile_Check(file)) {
        f = PyFile_AsFile(file);
        if (f == NULL) return NULL;
    } else if (PyString_Check(file)) {
        name = PyString_AsString(file);
        if (name == NULL) return NULL;
        f = fopen(name, "wb");
        if (f == NULL) {
            PyErr_SetString(PyExc_IOError, "Can not open file for writing!");
            return NULL;
        }
        closefile = true;
    } else {
        PyErr_SetString(PyExc_TypeError, "Valid are only string or file parameters!");
        return NULL;
    }

    if (!RingtoneFromPython(value, &ringtone)) {
        return NULL;
    }

    if (strcmp(s, "mid") == 0) {
        savemid(f, &ringtone);
    } else if (strcmp(s, "rng") == 0) {
        saveott(f, &ringtone);
    } else if (strcmp(s, "imy") == 0) {
        saveimelody(f, &ringtone);
    } else if (strcmp(s, "wav") == 0) {
        savewav(f, &ringtone);
    } else if (strcmp(s, "rttl") == 0) {
        saverttl(f,&ringtone);
    } else {
        PyErr_Format(PyExc_ValueError, "Bad value for format: '%s'", s);
        return NULL;
    }

    if (closefile) {
        fclose(f);
    }

    Py_RETURN_NONE;
}

static char gammu_SaveBackup__doc__[] =
"SaveBackup(Filename, Backup, UseUnicode)\n\n"
"Saves backup into file.\n\n"
"@param Filename: Name of file to read backup from\n"
"@type Filename: string\n"
"@param Backup: Backup data, see L{ReadBackup} for description\n"
"@type Backup: dict\n"
"@param UseUnicode: Whether to use unicode (UCS2-BE) for saving backup, default True\n"
"@type UseUnicode: bool\n"
"@return: None\n"
;

static PyObject *
gammu_SaveBackup(PyObject *self, PyObject *args, PyObject *kwds)
{
    static char                 *kwlist[] = {"Filename", "Backup", "UseUnicode", NULL};
    PyObject                    *value;
    char                        *filename;
    GSM_Backup                  backup;
    GSM_Error                   error;
    int                         i = 1;
    bool                        unicode;

    GSM_ClearBackup(&backup);

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "sO!|i", kwlist,
                &filename, &PyDict_Type, &(value), &i))
        return NULL;

    unicode = i;

    if (!BackupFromPython(value, &backup))
        return NULL;

    GSM_GetCurrentDateTime(&backup.DateTime);
    backup.DateTimeAvailable = true;

    error = GSM_SaveBackupFile(filename, &backup, unicode);
    if (!checkError(NULL, error, "SaveBackup")) return NULL;

    Py_RETURN_NONE;
}

static char gammu_ReadBackup__doc__[] =
"ReadBackup(Filename)\n\n"
"Reads backup into file.\n\n"
"@param Filename: Name of file where backup is stored\n"
"@type Filename: string\n"
"@return: Dictionary of read entries, it contains following keys, each might be empty:\n"
" - IMEI\n"
" - Model\n"
" - Creator\n"
" - PhonePhonebook\n"
" - SIMPhonebook\n"
" - Calendar\n"
" - ToDo\n"
" - DateTime\n"
;

static PyObject *
gammu_ReadBackup(PyObject *self, PyObject *args, PyObject *kwds)
{
    static char                 *kwlist[] = {"Filename", NULL};
    char                        *filename;
    GSM_Backup                  backup;
    GSM_Error                   error;

    GSM_ClearBackup(&backup);

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist,
                &filename))
        return NULL;

    error = GSM_ReadBackupFile(filename, &backup);
    if (!checkError(NULL, error, "ReadBackup")) return NULL;

    return BackupToPython(&backup);
}


static char gammu_SaveSMSBackup__doc__[] =
"SaveSMSBackup(Filename, Backup)\n\n"
"Saves SMS backup into file.\n\n"
"@param Filename: Name of file where to save SMS backup\n"
"@type Filename: string\n"
"@param Backup: List of messages to store\n"
"@type Backup: list\n"
"@return: None\n"
;

static PyObject *
gammu_SaveSMSBackup(PyObject *self, PyObject *args, PyObject *kwds)
{
    static char                 *kwlist[] = {"Filename", "Backup", NULL};
    PyObject                    *value;
    char                        *filename;
    GSM_SMS_Backup              backup;
    GSM_Error                   error;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "sO!", kwlist,
                &filename, &PyList_Type, &(value)))
        return NULL;

    if (!SMSBackupFromPython(value, &backup))
        return NULL;

    error = GSM_AddSMSBackupFile(filename, &backup);
    if (!checkError(NULL, error, "SaveSMSBackup")) return NULL;

    FreeSMSBackup(&backup);
    Py_RETURN_NONE;
}

static char gammu_ReadSMSBackup__doc__[] =
"ReadSMSBackup(Filename)\n\n"
"Reads SMS backup into file.\n\n"
"@param Filename: Name of file where SMS backup is stored\n"
"@type Filename: string\n"
"@return: List of messages read from file\n"
;

static PyObject *
gammu_ReadSMSBackup(PyObject *self, PyObject *args, PyObject *kwds)
{
    static char                 *kwlist[] = {"Filename", NULL};
    char                        *filename;
    GSM_SMS_Backup              backup;
    GSM_Error                   error;
    PyObject                    *result;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist,
                &filename))
        return NULL;

    error = GSM_ReadSMSBackupFile(filename, &backup);
    if (!checkError(NULL, error, "ReadSMSBackup")) return NULL;

    result = SMSBackupToPython(&backup);
    FreeSMSBackup(&backup);
    return result;
}


/* List of methods defined in the module */

static struct PyMethodDef gammu_methods[] = {
    {"Version",         (PyCFunction)gammu_Version,         METH_NOARGS,   gammu_Version__doc__},
    {"SetDebugFile",    (PyCFunction)gammu_SetDebugFile,    METH_VARARGS|METH_KEYWORDS,   gammu_SetDebugFile__doc__},
    {"SetDebugLevel",   (PyCFunction)gammu_SetDebugLevel,   METH_VARARGS|METH_KEYWORDS,   gammu_SetDebugLevel__doc__},

    {"LinkSMS",         (PyCFunction)gammu_LinkSMS,         METH_VARARGS|METH_KEYWORDS,   gammu_LinkSMS__doc__},
    {"DecodeSMS",       (PyCFunction)gammu_DecodeSMS,       METH_VARARGS|METH_KEYWORDS,   gammu_DecodeSMS__doc__},
    {"EncodeSMS",       (PyCFunction)gammu_EncodeSMS,       METH_VARARGS|METH_KEYWORDS,   gammu_EncodeSMS__doc__},

    {"SaveRingtone",    (PyCFunction)gammu_SaveRingtone,    METH_VARARGS|METH_KEYWORDS,   gammu_SaveRingtone__doc__},

    {"SaveBackup",      (PyCFunction)gammu_SaveBackup,      METH_VARARGS|METH_KEYWORDS,   gammu_SaveBackup__doc__},
    {"ReadBackup",      (PyCFunction)gammu_ReadBackup,      METH_VARARGS|METH_KEYWORDS,   gammu_ReadBackup__doc__},

    {"SaveSMSBackup",   (PyCFunction)gammu_SaveSMSBackup,   METH_VARARGS|METH_KEYWORDS,   gammu_SaveSMSBackup__doc__},
    {"ReadSMSBackup",   (PyCFunction)gammu_ReadSMSBackup,   METH_VARARGS|METH_KEYWORDS,   gammu_ReadSMSBackup__doc__},

    {NULL,	 (PyCFunction)NULL, 0, NULL}		/* sentinel */
};


/* Initialization function for the module (*must* be called initgammu) */

static char gammu_module_documentation[] =
"Module wrapping Gammu functions. Gammu is software for communication with GSM phones "
"allowing work with most of data stored in them. Most of functionality is hidden in L{StateMachine}"
"class which does all phone communication.\n\n"
"This documentation describes python-gammu " PYTHON_GAMMU_VERSION ".\n\n"
"@var Errors: Mapping of exceptions to gammu error codes.\n"
;

#ifndef PyMODINIT_FUNC  /* doesn't exists in older python releases */
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC initgammu(void) {
    PyObject *m, *d;
    int i;

    /* Check runtime Gammu version, in normal case soname would take care of this, but Gammu API is so unstable... */
    if (strcmp(GetGammuVersion(), VERSION) != 0) {
        PyErr_Format(PyExc_SystemError, "Runtime libGammu version does not match compile time version (runtime: %s, compiletime: %s)", GetGammuVersion(), VERSION);
        return;
    }

    /* Cleanup global array */
    for (i = 0; i < MAX_STATEMACHINES + 1; i++) {
        AllStateMachines[i] = NULL;
    }

#ifdef WITH_THREAD
    /* Create list lock */
    AllStateMachinesMutex = PyThread_allocate_lock();
#endif

    /* Create the module and add the functions */
    m = Py_InitModule3("gammu", gammu_methods, gammu_module_documentation);

    if (m == NULL)
        return;

    DebugFile = NULL;

    d = PyModule_GetDict(m);

    if (PyType_Ready(&StateMachineType) < 0)
        return;
    Py_INCREF(&StateMachineType);

    if (PyModule_AddObject(m, "StateMachine", (PyObject *)&StateMachineType) < 0)
        return;

    /* Add some symbolic constants to the module */

    /* Define errors */
    if (!gammu_create_errors(d)) return;

    /* Check for errors */
    if (PyErr_Occurred())
        Py_FatalError("can't initialize module gammu");

    /* Reset debugging setup */
    di.dl		= DL_NONE;
    di.df	 	= NULL;
    di.was_lf	= true;
}
