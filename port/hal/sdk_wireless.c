/* The DS's wireless library replaced: ov105 (WM and its helper wh.c) is not built; the 24 functions
 * the game calls in it are these, under the decompilation's names, on the port's network
 * (runtime/net/net_session.h). What the game reads back keeps its shape; what wh.c did in a WM
 * callback is done where the port takes the game's interrupts (docs/PLATFORM.md, docs/MULTIPLAYER.md). */
#include "hal.h"
#include "../runtime/net/net_session.h"

typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define NULL 0

/* --- what the game reads (the decompilation's shapes) ------------------ */

/* wh.c's system states (Game_PollSceneAlive switches on them). */
enum {
    WH_SYSSTATE_STOP = 0,
    WH_SYSSTATE_IDLE = 1,
    WH_SYSSTATE_SCANNING = 2,
    WH_SYSSTATE_BUSY = 3,
    WH_SYSSTATE_CONNECTED = 4,
    WH_SYSSTATE_MEASURECHANNEL = 7,
    WH_SYSSTATE_CONNECT_FAIL = 8,
    WH_SYSSTATE_ERROR = 9,
    WH_SYSSTATE_FATAL = 10
};

/* WM's error codes as wh.c keeps them (WH_GetLastError). */
enum {
    WM_ERRCODE_SUCCESS = 0,
    WM_ERRCODE_FAILED = 1,
    WM_ERRCODE_OPERATING = 2,
    WM_ERRCODE_ILLEGAL_STATE = 3,
    WM_ERRCODE_WM_DISABLE = 4,
    WM_ERRCODE_INVALID_PARAM = 6,
    WM_ERRCODE_NO_CHILD = 7,
    WM_ERRCODE_NO_ENTRY = 11,
    WM_ERRCODE_OVER_MAX_ENTRY = 12,
    WH_ERRCODE_DISCONNECTED = 20
};

enum {
    WM_APIID_SET_MP_DATA = 15,
    WM_APIID_SET_ENTRY = 33,
    WM_APIID_PORT_RECV = 130,
    WM_STATECODE_CONNECTED = 7,
    WM_STATECODE_DISCONNECTED = 9,
    WM_STATECODE_PORT_SEND = 20,
    WM_STATECODE_PORT_RECV = 21,
    WM_STATECODE_PORT_INIT = 25,
    WM_ATTR_FLAG_ENTRY = 0x01,
    WM_GAMEINFO_MAGIC_NUMBER = 0x0001
};

#define WM_NUM_OF_PORT 16
#define WM_SIZE_CHILD_SSID 24
#define WH_DATA_PORT 14
#define USER_GAMEINFO_MAX 112

/* The most a frame of the engine's messages holds (MsgQueue_Enqueue: 0x1e0
 * bytes unless the session's link mode is 3, a guest's, 0x60). */
#define HOST_FRAME 0x1e0
#define GUEST_FRAME 0x60
/* The engine's port (Session_Init_2): its receivers put a record into the sender's packet slot, two
 * of its largest frames (0xc0 a guest's on the host, 0x3c0 the host's on a guest), read as a run of
 * messages up to its length: records that waited go together. */
#define ENGINE_PORT 12

typedef struct WMGameInfo {
    u16 magicNumber;                    /* 0x00 */
    u8 ver;                             /* 0x02 */
    u8 platform;                        /* 0x03 */
    u32 ggid;                           /* 0x04 */
    u16 tgid;                           /* 0x08 */
    u8 userGameInfoLength;              /* 0x0a */
    u8 attribute;                       /* 0x0b */
    u16 parentMaxSize;                  /* 0x0c */
    u16 childMaxSize;                   /* 0x0e */
    u8 userGameInfo[USER_GAMEINFO_MAX]; /* 0x10 */
} WMGameInfo;

/* A room as the scan callback hands it; the menu keeps it whole as a
 * MissionRecord (0xc0 bytes): the host's id at +4 is the row's key, the game
 * information at +0x40 (its user game information at +0x50 the row's label). */
typedef struct WMBssDesc {
    u16 length;            /* 0x00: in halfwords */
    u16 rssi;              /* 0x02 */
    u8 bssid[6];           /* 0x04 */
    u16 ssidLength;        /* 0x0a */
    u8 ssid[32];           /* 0x0c */
    u16 capaInfo;          /* 0x2c */
    u16 rateBasic;         /* 0x2e */
    u16 rateSupport;       /* 0x30 */
    u16 beaconPeriod;      /* 0x32 */
    u16 dtimPeriod;        /* 0x34 */
    u16 channel;           /* 0x36 */
    u16 cfpPeriod;         /* 0x38 */
    u16 cfpMaxDuration;    /* 0x3a */
    u16 gameInfoLength;    /* 0x3c */
    u16 otherElementCount; /* 0x3e */
    WMGameInfo gameInfo;   /* 0x40 */
} WMBssDesc;

/* The parent's connect record its judge reads (Ov006_MatchMissionStartPacket:
 * +2 error, +8 state). */
typedef struct WMStartParentCallback {
    u16 apiid;        /* 0x00 */
    u16 errcode;      /* 0x02 */
    u16 wlCmdID;      /* 0x04 */
    u16 wlResult;     /* 0x06 */
    u16 state;        /* 0x08 */
    u8 macAddress[6]; /* 0x0a */
    u16 aid;          /* 0x10 */
    u16 reason;       /* 0x12 */
    u8 ssid[24];      /* 0x14 */
    u16 parentSize;   /* 0x2c */
    u16 childSize;    /* 0x2e */
} WMStartParentCallback;

/* A port record (the engine's port-12 handlers read +2 error, +0xc data,
 * +0x10 length, +0x12 aid). */
typedef struct WMPortRecvCallback {
    u16 apiid;              /* 0x00 */
    u16 errcode;            /* 0x02 */
    u16 state;              /* 0x04 */
    u16 port;               /* 0x06 */
    u16 *recvBuf;           /* 0x08 */
    u16 *data;              /* 0x0c */
    u16 length;             /* 0x10 */
    u16 aid;                /* 0x12 */
    u8 macAddress[6];       /* 0x14 */
    u16 seqNo;              /* 0x1a */
    void *arg;              /* 0x1c */
    u16 myAid;              /* 0x20 */
    u16 connectedAidBitmap; /* 0x22 */
    u8 ssid[24];            /* 0x24 */
    u16 reason;             /* 0x3c */
    u16 reserved;           /* 0x3e */
    u16 maxSendDataSize;    /* 0x40 */
    u16 maxRecvDataSize;    /* 0x42 */
} WMPortRecvCallback;

/* A send's completion as the game reads it: +2 error (wh.c's send callback,
 * the decompilation's Ov105_DispatchOrNotifyCancel), the data at +0x14 (the
 * engine's AsyncMessage_OnSendDone -- the decompilation's wm.h has it at
 * +0x10, which the engine's read contradicts), the arg at +0x20. */
typedef struct WMPortSendCallback {
    u16 apiid;                /* 0x00 */
    u16 errcode;              /* 0x02 */
    u16 wlCmdID;              /* 0x04 */
    u16 wlResult;             /* 0x06 */
    u16 state;                /* 0x08 */
    u16 port;                 /* 0x0a */
    u16 destBitmap;           /* 0x0c */
    u16 restBitmap;           /* 0x0e */
    u16 sentBitmap;           /* 0x10 */
    u16 reserved;             /* 0x12 */
    const u16 *data;          /* 0x14 */
    u16 length;               /* 0x18 */
    u16 seqNo;                /* 0x1a */
    void (*callback)(void *); /* 0x1c */
    void *arg;                /* 0x20 */
} WMPortSendCallback;

typedef struct WMCallback {
    u16 apiid;
    u16 errcode;
} WMCallback;

typedef void (*WMCallbackFunc)(void *arg);
typedef void (*WhScanCallbackFunc)(WMBssDesc *bssDesc);
typedef void (*WhReceiverFunc)(u16 aid, u16 *data, u16 size);
typedef BOOL (*WhJudgeAcceptFunc)(WMStartParentCallback *cb);
typedef void (*WhSendCallbackFunc)(BOOL delivered);

/* wh.c's two DTCM words the engine reads (WH_GetCurrentAid / WH_GetBitmap). */
extern u16 data_027e0064; /* sMyAid */
extern u16 data_027e0068; /* sConnectBitmap */

extern void OS_GetMacAddress(u8 *macAddress);
extern u32 OS_DisableInterrupts(void);
extern u32 OS_RestoreInterrupts(u32 state);

/* the port's side */
extern u32 khdays_events_vblank_count(void);

/* --- the layer's state -------------------------------------------------- */

enum { PEND_NONE, PEND_INIT, PEND_MEASURE, PEND_PARENT, PEND_END_SCAN, PEND_RESET, PEND_END, PEND_JOIN_FAILED };

static int sSysState;
static int sErrCode;
static u16 sDisconnectReason;
static int sPending;
static int sConnectMode;
static int sIsParent;
static u32 sGgid;
static u16 sTgid;
static u16 sMaxEntry;
static const u8 *sUserGameInfo;
static u16 sUserGameInfoLength;
static WhJudgeAcceptFunc sJudge;
static WhReceiverFunc sReceiver;
static WhScanCallbackFunc sScanCallback;
static u8 sScanFilter[6];
static u8 sSsid[WM_SIZE_CHILD_SSID];
static u32 sTgidSeed = 0x10000;
static u16 sWmBitmap; /* WM's connected aids (the port records' connectedAidBitmap) */
static u16 sWmAid;    /* WM's own aid */
static WMCallbackFunc sPortCallback[WM_NUM_OF_PORT];
static void *sPortArg[WM_NUM_OF_PORT];
static int sDelivering;

/* The records handed to the game: the layer's own, in the game's memory. */
static WMBssDesc sBssDesc;
static WMStartParentCallback sParentCb;
static WMPortRecvCallback sPortRecv;
static WMPortSendCallback sPortSend;
static WMCallback sPlainCb;
static WMGameInfo sOwnGameInfo;
/* A received record's bytes while its handler runs: two of the host's frames
 * joined (ENGINE_PORT), a frame grown past the DS's (runtime/net/msg_overflow.h)
 * or a menu block -- room for any record, whose size is 16 bits. */
static u16 sRecvData[0x8000];

/* A completion owed to the game at the next delivery (runtime/net/net_session.h
 * keeps them: a queue that grows). Kinds: wh.c's send callback, a WM port
 * send, a plain WM callback. */
enum { OWED_WH_SEND = 1, OWED_PORT_SEND, OWED_PLAIN };

static void owe(u32 kind, void *callback, void *arg, const u16 *data, u16 length, u16 port, u16 dest, u16 apiid)
{
    KhdaysNetOwed o;
    o.kind = kind;
    o.callback = (u32)callback;
    o.arg = (u32)arg;
    o.data = (u32)data;
    o.length = length;
    o.port = port;
    o.dest = dest;
    o.apiid = apiid;
    khdays_net_owe(&o);
}

static void WH_SetError(int code)
{
    /* wh.c keeps the first error: none is recorded over ERROR or FATAL */
    if (sSysState != WH_SYSSTATE_ERROR && sSysState != WH_SYSSTATE_FATAL) {
        sErrCode = code;
    }
}

static void clear(void *p, u32 size)
{
    u8 *b = (u8 *)p;
    u32 i;
    for (i = 0; i < size; ++i) {
        b[i] = 0;
    }
}

static void copy(void *to, const void *from, u32 size)
{
    u8 *t = (u8 *)to;
    const u8 *f = (const u8 *)from;
    u32 i;
    for (i = 0; i < size; ++i) {
        t[i] = f[i];
    }
}

static int connected(void)
{
    return sSysState == WH_SYSSTATE_CONNECTED;
}

/* --- port records ------------------------------------------------------ */

/* A member in or out, told to every port callback as WM's receive path
 * replays it (the decompilation's Ov105_WmReceiveFifo). */
static void tell_ports(u16 state, u16 aid, const u8 *id)
{
    u16 port;
    for (port = 0; port < WM_NUM_OF_PORT; ++port) {
        if (sPortCallback[port] == NULL) {
            continue;
        }
        clear(&sPortRecv, sizeof(sPortRecv));
        sPortRecv.apiid = WM_APIID_PORT_RECV;
        sPortRecv.errcode = WM_ERRCODE_SUCCESS;
        sPortRecv.state = state;
        sPortRecv.port = port;
        sPortRecv.aid = aid;
        sPortRecv.seqNo = 0xffff;
        sPortRecv.arg = sPortArg[port];
        sPortRecv.myAid = sWmAid;
        sPortRecv.connectedAidBitmap = sWmBitmap;
        if (id != NULL) {
            copy(sPortRecv.macAddress, id, 6);
        }
        sPortRecv.maxSendDataSize = sIsParent ? HOST_FRAME : GUEST_FRAME;
        sPortRecv.maxRecvDataSize = sIsParent ? GUEST_FRAME : HOST_FRAME;
        sPortCallback[port](&sPortRecv);
    }
}

static void received(u16 aid, u16 port, u32 size, const u8 *id)
{
    if (port == WH_DATA_PORT && sReceiver != NULL) {
        sReceiver(aid, sRecvData, (u16)size);
    }
    if (port < WM_NUM_OF_PORT && sPortCallback[port] != NULL) {
        clear(&sPortRecv, sizeof(sPortRecv));
        sPortRecv.apiid = WM_APIID_PORT_RECV;
        sPortRecv.errcode = WM_ERRCODE_SUCCESS;
        sPortRecv.state = WM_STATECODE_PORT_RECV;
        sPortRecv.port = port;
        sPortRecv.data = sRecvData;
        sPortRecv.length = (u16)size;
        sPortRecv.aid = aid;
        copy(sPortRecv.macAddress, id, 6);
        sPortRecv.arg = sPortArg[port];
        sPortRecv.myAid = sWmAid;
        sPortRecv.connectedAidBitmap = sWmBitmap;
        sPortCallback[port](&sPortRecv);
    }
}

/* --- the game's calls ---------------------------------------------------- */

/* WH_Initialize: the statics reset as wh.c resets them; IDLE once the
 * network is up (at the delivery). */
BOOL Ov105_WH_Initialize(void)
{
    data_027e0064 = 0;
    data_027e0068 = 1;
    sErrCode = WM_ERRCODE_SUCCESS;
    sDisconnectReason = 0;
    sUserGameInfo = NULL;
    sUserGameInfoLength = 0;
    clear(sSsid, sizeof(sSsid));
    sJudge = NULL;
    sReceiver = NULL;
    sSysState = WH_SYSSTATE_BUSY;
    sPending = PEND_INIT;
    khdays_net_trace("WH_Initialize", 0, 0);
    return TRUE;
}

/* WH_Finalize: whatever is up comes down; IDLE at the delivery. */
void Ov105_WH_Finalize(void)
{
    khdays_net_trace("WH_Finalize", (u32)sSysState, 0);
    if (sSysState == WH_SYSSTATE_IDLE) {
        return;
    }
    if (sSysState == WH_SYSSTATE_SCANNING) {
        khdays_net_scan(sGgid, 0);
        sSysState = WH_SYSSTATE_BUSY;
        sPending = PEND_END_SCAN;
        return;
    }
    khdays_net_leave();
    sSysState = WH_SYSSTATE_BUSY;
    sPending = PEND_RESET;
}

/* WH_Reset. */
void Ov105_WH_Reset(void)
{
    khdays_net_trace("WH_Reset", (u32)sSysState, 0);
    khdays_net_scan(sGgid, 0);
    khdays_net_leave();
    sSysState = WH_SYSSTATE_BUSY;
    sPending = PEND_RESET;
}

/* WH_End: from IDLE only, as WM_End; STOP at
 * the delivery. 1 while it runs. */
int Ov105_WH_End(void)
{
    khdays_net_trace("WH_End", (u32)sSysState, 0);
    if (sSysState != WH_SYSSTATE_IDLE) {
        WH_SetError(WM_ERRCODE_ILLEGAL_STATE);
        sSysState = WH_SYSSTATE_ERROR;
        return 0;
    }
    sSysState = WH_SYSSTATE_BUSY;
    sPending = PEND_END;
    return 1;
}

/* WH_GetSystemState. */
int Ov105_WH_GetSystemState(void)
{
    return sSysState;
}

/* WH_GetLastError. */
int Ov105_WH_GetLastError(void)
{
    return sErrCode;
}

/* The first disconnect reason (Ov105_WH_GetDisconnectReason). */
int Ov105_WH_GetDisconnectReason(void)
{
    return sDisconnectReason;
}

/* WH_SetGgid: the game's 0x800356. */
void Ov105_WH_SetGgid(int ggid)
{
    sGgid = (u32)ggid;
}

/* WH_SetUserGameInfo. */
void Ov105_WH_SetUserGameInfo(const void *data, int size)
{
    sUserGameInfo = (const u8 *)data;
    sUserGameInfoLength = (u16)size;
}

/* WH_SetJudgeAcceptFunc. */
void Ov105_WH_SetJudgeAcceptFunc(void *judge)
{
    sJudge = (WhJudgeAcceptFunc)judge;
}

/* WH_StartMeasureChannel: there are no channels: measured at the delivery. */
BOOL Ov105_WH_StartMeasureChannel(void)
{
    sSysState = WH_SYSSTATE_BUSY;
    sPending = PEND_MEASURE;
    return TRUE;
}

/* WH_GetMeasureChannel: IDLE, and a channel
 * for the game to pass on (it means nothing here). */
u16 Ov105_WH_GetMeasureChannel(void)
{
    sSysState = WH_SYSSTATE_IDLE;
    return 1;
}

/* WM_GetNextTgid: a room id, new each call (seeded once). */
u16 Ov105_WM_GetNextTgid(void)
{
    if (sTgidSeed == 0x10000) {
        u8 id[6];
        khdays_net_id(id);
        sTgidSeed = (u16)(id[4] << 8 | id[5]);
    }
    sTgidSeed = (u16)(sTgidSeed + 1);
    return (u16)sTgidSeed;
}

/* WH_ParentConnect: a room of ours, with the game's information. */
BOOL Ov105_WH_ParentConnect(int mode, u16 tgid, u16 channel, int mpFreq, u16 maxEntry)
{
    (void)channel;
    (void)mpFreq;
    khdays_net_trace("WH_ParentConnect", tgid, maxEntry);
    sConnectMode = mode;
    sTgid = tgid;
    sMaxEntry = maxEntry;
    sSysState = WH_SYSSTATE_BUSY;
    sPending = PEND_PARENT;
    return TRUE;
}

/* WM_SetEntry: the room open or closed to new guests. */
int Ov105_WM_SetEntry(void *callback, BOOL enabled)
{
    if (!connected() || !sIsParent) {
        return WM_ERRCODE_ILLEGAL_STATE;
    }
    khdays_net_entry(enabled);
    owe(OWED_PLAIN, callback, NULL, NULL, 0, 0, 0, WM_APIID_SET_ENTRY);
    return WM_ERRCODE_OPERATING;
}

/* WH_StartScan: every room of the game's group, as soon as it is known. */
BOOL Ov105_WH_StartScan(void *callback, const u16 *macAddr, u16 channel)
{
    (void)channel;
    sScanCallback = (WhScanCallbackFunc)callback;
    copy(sScanFilter, macAddr, 6);
    sSysState = WH_SYSSTATE_SCANNING;
    khdays_net_scan(sGgid, 1);
    khdays_net_trace("WH_StartScan", sGgid, 0);
    return TRUE;
}

/* WH_SetSsid: the bytes a guest joins with (24 at most, zero padded). */
void Ov105_WH_SetSsid(const void *ssid, u32 length)
{
    if (length > WM_SIZE_CHILD_SSID) {
        length = WM_SIZE_CHILD_SSID;
    }
    clear(sSsid, sizeof(sSsid));
    copy(sSsid, ssid, length);
}

/* WH_ChildConnect: a join asked of the room the descriptor names. */
BOOL Ov105_WH_ChildConnect(int mode, WMBssDesc *bssDesc)
{
    sConnectMode = mode;
    sSysState = WH_SYSSTATE_BUSY;
    khdays_net_scan(sGgid, 0);
    khdays_net_trace("WH_ChildConnect", bssDesc->gameInfo.tgid, 0);
    if (!khdays_net_join(bssDesc->bssid, sSsid, WM_SIZE_CHILD_SSID)) {
        sPending = PEND_JOIN_FAILED; /* the room is gone: the DS's connect would fail */
    }
    return TRUE;
}

/* WH_SetReceiver: the data port's receiver. */
void Ov105_WH_SetReceiver(void *proc)
{
    sReceiver = (WhReceiverFunc)proc;
}

/* WH_SendData: to every member on the data port. */
BOOL Ov105_WH_SendData(const void *data, int size, void *callback)
{
    if (!connected() || (sIsParent && (data_027e0068 & ~1u) == 0)) {
        return FALSE;
    }
    khdays_net_send(0xffff, WH_DATA_PORT, data, (u32)size);
    owe(OWED_WH_SEND, callback, NULL, NULL, 0, WH_DATA_PORT, 0xffff, 0);
    return TRUE;
}

/* WM_GetLinkLevel, and wh.c's: the connection's
 * quality, 0 with no one connected. */
int Ov105_WM_GetLinkLevel(void)
{
    if (!connected() || (sIsParent && (data_027e0068 & ~1u) == 0)) {
        return 0;
    }
    return khdays_net_link_level();
}

int Ov105_WH_GetLinkLevel(void)
{
    return Ov105_WM_GetLinkLevel();
}

/* WM_SetMPDataToPortEx: to `destBitmap` on `port`; completed at the next
 * delivery, the transport delivering it. */
int Ov105_WM_SetMPDataToPortEx(void *callback, void *arg, const u16 *data, u16 size, u16 destBitmap, u16 port, u16 prio)
{
    (void)prio;
    if (!connected()) {
        return WM_ERRCODE_ILLEGAL_STATE;
    }
    if (sIsParent && (data_027e0068 & ~1u) == 0) {
        return WM_ERRCODE_NO_CHILD;
    }
    if (data == NULL || size == 0 || port >= WM_NUM_OF_PORT) {
        return WM_ERRCODE_INVALID_PARAM;
    }
    khdays_net_send(destBitmap, port, data, size);
    owe(OWED_PORT_SEND, callback, arg, data, size, port, destBitmap, WM_APIID_SET_MP_DATA);
    return WM_ERRCODE_OPERATING;
}

/* WM_SetPortCallback: kept, and a first record
 * at once, as the decompilation's function sends it (+0x20 own aid, +0x22
 * the connected aids: the decompilation names their getters otherwise). */
int Ov105_WM_SetPortCallback(u16 port, void *callback, void *arg)
{
    u32 enabled;
    if (port >= WM_NUM_OF_PORT) {
        return WM_ERRCODE_INVALID_PARAM;
    }
    enabled = OS_DisableInterrupts();
    if (sSysState == WH_SYSSTATE_STOP) {
        OS_RestoreInterrupts(enabled);
        return WM_ERRCODE_ILLEGAL_STATE;
    }
    sPortCallback[port] = (WMCallbackFunc)callback;
    sPortArg[port] = arg;
    if (port == ENGINE_PORT) {
        khdays_net_join_records(port, callback == NULL ? 0 : sIsParent ? 2 * GUEST_FRAME : 2 * HOST_FRAME);
    }
    if (callback != NULL) {
        clear(&sPortRecv, sizeof(sPortRecv));
        sPortRecv.apiid = WM_APIID_PORT_RECV;
        sPortRecv.state = WM_STATECODE_PORT_INIT;
        sPortRecv.port = port;
        sPortRecv.seqNo = 0xffff;
        sPortRecv.arg = arg;
        OS_GetMacAddress(sPortRecv.macAddress);
        sPortRecv.myAid = sWmAid;
        sPortRecv.connectedAidBitmap = sWmBitmap;
        ((WMCallbackFunc)callback)(&sPortRecv);
    }
    OS_RestoreInterrupts(enabled);
    return WM_ERRCODE_SUCCESS;
}

/* --- the delivery: what wh.c did in WM's callbacks ---------------------- */

static void build_room(const KhdaysNetEvent *e)
{
    const u32 size = e->size < sizeof(WMGameInfo) ? e->size : sizeof(WMGameInfo);
    clear(&sBssDesc, sizeof(sBssDesc));
    sBssDesc.length = sizeof(WMBssDesc) / 2;
    copy(sBssDesc.bssid, e->id, 6);
    sBssDesc.channel = 1;
    copy(&sBssDesc.gameInfo, sRecvData, size);
    if (e->reason) {
        sBssDesc.gameInfo.attribute |= WM_ATTR_FLAG_ENTRY;
    } else {
        sBssDesc.gameInfo.attribute &= (u8)~WM_ATTR_FLAG_ENTRY;
    }
    sBssDesc.gameInfoLength = (u16)(16 + sBssDesc.gameInfo.userGameInfoLength);
}

static int filtered_out(const u8 *id)
{
    int any = 1, i;
    for (i = 0; i < 6; ++i) {
        if (sScanFilter[i] != 0xff) {
            any = 0;
        }
    }
    if (any) {
        return 0;
    }
    for (i = 0; i < 6; ++i) {
        if (sScanFilter[i] != id[i]) {
            return 1;
        }
    }
    return 0;
}

static void finish_pending(void)
{
    const int pending = sPending;
    sPending = PEND_NONE;
    switch (pending) {
    case PEND_INIT:
        if (khdays_net_begin()) {
            sSysState = WH_SYSSTATE_IDLE;
        } else {
            WH_SetError(WM_ERRCODE_WM_DISABLE);
            sSysState = WH_SYSSTATE_FATAL;
        }
        break;
    case PEND_MEASURE: sSysState = WH_SYSSTATE_MEASURECHANNEL; break;
    case PEND_PARENT:
        clear(&sOwnGameInfo, sizeof(sOwnGameInfo));
        sOwnGameInfo.magicNumber = WM_GAMEINFO_MAGIC_NUMBER;
        sOwnGameInfo.ggid = sGgid;
        sOwnGameInfo.tgid = sTgid;
        sOwnGameInfo.userGameInfoLength =
            (u8)(sUserGameInfoLength < USER_GAMEINFO_MAX ? sUserGameInfoLength : USER_GAMEINFO_MAX);
        sOwnGameInfo.attribute = WM_ATTR_FLAG_ENTRY;
        sOwnGameInfo.parentMaxSize = HOST_FRAME;
        sOwnGameInfo.childMaxSize = GUEST_FRAME;
        if (sUserGameInfo != NULL) {
            copy(sOwnGameInfo.userGameInfo, sUserGameInfo, sOwnGameInfo.userGameInfoLength);
        }
        if (khdays_net_host(sGgid, &sOwnGameInfo, 16u + sOwnGameInfo.userGameInfoLength, sMaxEntry)) {
            sIsParent = 1;
            data_027e0064 = 0;
            data_027e0068 = 1;
            sWmAid = 0;
            sWmBitmap = 0;
            sSysState = WH_SYSSTATE_CONNECTED;
        } else {
            WH_SetError(WM_ERRCODE_FAILED);
            sSysState = WH_SYSSTATE_ERROR;
        }
        break;
    case PEND_END_SCAN: sSysState = WH_SYSSTATE_IDLE; break;
    case PEND_RESET:
        sIsParent = 0;
        data_027e0064 = 0;
        data_027e0068 = 1;
        sWmAid = 0;
        sWmBitmap = 0;
        sSysState = WH_SYSSTATE_IDLE;
        break;
    case PEND_END:
        khdays_net_end();
        sSysState = WH_SYSSTATE_STOP;
        break;
    case PEND_JOIN_FAILED:
        WH_SetError(WM_ERRCODE_FAILED);
        sSysState = WH_SYSSTATE_CONNECT_FAIL;
        break;
    default: break;
    }
}

static void on_event(const KhdaysNetEvent *e)
{
    switch (e->kind) {
    case KHDAYS_NET_ROOM:
        if (sSysState == WH_SYSSTATE_SCANNING && sScanCallback != NULL && e->reason && !filtered_out(e->id)) {
            build_room(e);
            if (sBssDesc.gameInfo.magicNumber == WM_GAMEINFO_MAGIC_NUMBER && sBssDesc.gameInfo.ggid == sGgid) {
                sScanCallback(&sBssDesc);
            }
        }
        break;
    case KHDAYS_NET_JOINING: {
        BOOL accept = TRUE;
        clear(&sParentCb, sizeof(sParentCb));
        sParentCb.state = WM_STATECODE_CONNECTED;
        copy(sParentCb.macAddress, e->id, 6);
        sParentCb.aid = (u16)e->aid;
        copy(sParentCb.ssid, sRecvData, e->size < 24 ? e->size : 24);
        sParentCb.parentSize = HOST_FRAME;
        sParentCb.childSize = GUEST_FRAME;
        if (sJudge != NULL) {
            accept = sJudge(&sParentCb);
        }
        khdays_net_trace(accept ? "a guest accepted" : "a guest refused by the game", e->aid, 0);
        khdays_net_accept(e->aid, accept);
        break;
    }
    case KHDAYS_NET_JOINED:
        if (sIsParent) {
            data_027e0068 = (u16)(data_027e0068 | (1u << e->aid));
            sWmBitmap = (u16)(sWmBitmap | (1u << e->aid));
            tell_ports(WM_STATECODE_CONNECTED, (u16)e->aid, e->id);
        } else {
            data_027e0064 = (u16)e->aid;
            sWmAid = (u16)e->aid;
            sWmBitmap = 1; /* the parent's bit, as WM's receive path sets it */
            sSysState = WH_SYSSTATE_CONNECTED;
            tell_ports(WM_STATECODE_CONNECTED, 0, e->id);
        }
        break;
    case KHDAYS_NET_REFUSED:
        /* A full or closed room: the game goes back to its list on a failed connection with these
         * codes (Ov006_UpdateMissionMenuConfirmScreen's state 8, Ov006_MissionSelectStateCallback's
         * 11 and 12); in the error state it shows its connection error and restarts. */
        switch (e->reason) {
        case KHDAYS_NET_REFUSE_FULL:
            WH_SetError(WM_ERRCODE_OVER_MAX_ENTRY);
            sSysState = WH_SYSSTATE_CONNECT_FAIL;
            break;
        case KHDAYS_NET_REFUSE_CLOSED:
        case KHDAYS_NET_REFUSE_PASSWORD: /* the game's own way back to its list (the port's says why) */
        case KHDAYS_NET_REFUSE_MODS:
            WH_SetError(WM_ERRCODE_NO_ENTRY);
            sSysState = WH_SYSSTATE_CONNECT_FAIL;
            break;
        case KHDAYS_NET_REFUSE_JUDGE:
            /* the DS's parent disconnects a guest its judge refuses */
            WH_SetError(WH_ERRCODE_DISCONNECTED);
            sSysState = WH_SYSSTATE_ERROR;
            break;
        default:
            WH_SetError(WM_ERRCODE_FAILED);
            sSysState = WH_SYSSTATE_CONNECT_FAIL;
            break;
        }
        break;
    case KHDAYS_NET_RECORD:
        if (e->size > sizeof(sRecvData)) {
            khdays_net_trace("a record longer than the layer's buffer, dropped", e->size, e->port);
            break;
        }
        received((u16)e->aid, (u16)e->port, e->size, e->id);
        break;
    case KHDAYS_NET_LEFT:
        if (sIsParent) {
            data_027e0068 = (u16)(data_027e0068 & ~(1u << e->aid));
            sWmBitmap = (u16)(sWmBitmap & ~(1u << e->aid));
            tell_ports(WM_STATECODE_DISCONNECTED, (u16)e->aid, e->id);
            if (sReceiver != NULL) {
                sReceiver((u16)e->aid, NULL, 0);
            }
        } else if (connected()) {
            sWmBitmap = 0;
            tell_ports(WM_STATECODE_DISCONNECTED, 0, e->id);
            if (sReceiver != NULL) {
                sReceiver(0, NULL, 0);
            }
            WH_SetError(WH_ERRCODE_DISCONNECTED);
            sSysState = WH_SYSSTATE_ERROR;
        }
        break;
    default: break;
    }
}

static void complete(const KhdaysNetOwed *o)
{
    switch (o->kind) {
    case OWED_WH_SEND:
        if (o->callback != 0) {
            ((WhSendCallbackFunc)o->callback)(TRUE);
        }
        break;
    case OWED_PORT_SEND:
        if (o->callback != 0) {
            clear(&sPortSend, sizeof(sPortSend));
            sPortSend.apiid = (u16)o->apiid;
            sPortSend.errcode = WM_ERRCODE_SUCCESS;
            sPortSend.state = WM_STATECODE_PORT_SEND;
            sPortSend.port = (u16)o->port;
            sPortSend.destBitmap = (u16)o->dest;
            sPortSend.sentBitmap = (u16)o->dest;
            sPortSend.data = (const u16 *)o->data;
            sPortSend.length = (u16)o->length;
            sPortSend.callback = (void (*)(void *))o->callback;
            sPortSend.arg = (void *)o->arg;
            ((WMCallbackFunc)o->callback)(&sPortSend);
        }
        break;
    case OWED_PLAIN:
        if (o->callback != 0) {
            sPlainCb.apiid = (u16)o->apiid;
            sPlainCb.errcode = WM_ERRCODE_SUCCESS;
            ((WMCallbackFunc)o->callback)(&sPlainCb);
        }
        break;
    default: break;
    }
}

/* Whether the layer owes the game something (hal/sdk_irq.c asks before it
 * takes the game's interrupts). */
int khdays_wireless_due(void)
{
    return !sDelivering && (sPending != PEND_NONE || khdays_net_owed_count() > 0 || khdays_net_pending());
}

/* What is owed, in IRQ mode as the DS's wireless interrupt was taken. */
void khdays_wireless_deliver(void)
{
    const u32 frame = khdays_events_vblank_count();
    KhdaysNetEvent e;
    int i;
    if (sDelivering) {
        return;
    }
    sDelivering = 1;
    if (sPending != PEND_NONE) {
        finish_pending();
    }
    /* the completions of the sends made before this delivery (one a callback
     * makes waits for the next) */
    {
        const int count = khdays_net_owed_count();
        KhdaysNetOwed o;
        for (i = 0; i < count && khdays_net_take_owed(&o); ++i) {
            complete(&o);
        }
    }
    while (khdays_net_next(&e, sRecvData, sizeof(sRecvData), frame)) {
        on_event(&e);
    }
    sDelivering = 0;
}
