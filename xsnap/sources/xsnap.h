#ifndef __XSNAP__
#define __XSNAP__

#include "xs.h"

typedef struct xsSnapshotRecord xsSnapshot;

struct xsSnapshotRecord {
	char* signature;
	int signatureLength;
	xsCallback* callbacks;
	int callbacksLength;
	int (*read)(void* stream, void* address, size_t size);
	int (*write)(void* stream, void* address, size_t size);
	void* stream;
	int error;
	void* firstChunk;
	void* firstProjection;
	void* firstSlot;
	int slotSize;
	void* slots;
	int (*patch)(xsMachine*, int version);
};

#define xsInitializeSharedCluster() \
	fxInitializeSharedCluster()
#define xsTerminateSharedCluster() \
	fxTerminateSharedCluster()

#ifdef mxMetering

#define xsBeginMetering(_THE, _CALLBACK, _STEP) \
	do { \
		xsJump __HOST_JUMP__; \
		__HOST_JUMP__.nextJump = (_THE)->firstJump; \
		__HOST_JUMP__.stack = (_THE)->stack; \
		__HOST_JUMP__.scope = (_THE)->scope; \
		__HOST_JUMP__.frame = (_THE)->frame; \
		__HOST_JUMP__.environment = NULL; \
		__HOST_JUMP__.code = (_THE)->code; \
		__HOST_JUMP__.flag = 0; \
		(_THE)->firstJump = &__HOST_JUMP__; \
		if (setjmp(__HOST_JUMP__.buffer) == 0) { \
			fxBeginMetering(_THE, _CALLBACK, _STEP)

#define xsEndMetering(_THE) \
			fxEndMetering(_THE); \
		} \
		(_THE)->stack = __HOST_JUMP__.stack, \
		(_THE)->scope = __HOST_JUMP__.scope, \
		(_THE)->frame = __HOST_JUMP__.frame, \
		(_THE)->code = __HOST_JUMP__.code, \
		(_THE)->firstJump = __HOST_JUMP__.nextJump; \
		break; \
	} while(1)
			
#define xsGetCurrentMeter(_THE) \
	fxGetCurrentMeter(_THE)
#define xsSetCurrentMeter(_THE, _VALUE) \
	fxSetCurrentMeter(_THE, _VALUE)

#else
	#define xsBeginMetering(_THE, _CALLBACK, _STEP)
	#define xsEndMetering(_THE)
	#define xsPatchHostFunction(_FUNCTION,_PATCH)
	#define xsMeterHostFunction(_COUNT) (void)(_COUNT)
	#define xsGetCurrentMeter(_THE) 0
	#define xsSetCurrentMeter(_THE, _VALUE)
#endif

#define xsReadSnapshot(_SNAPSHOT, _NAME, _CONTEXT) \
	fxReadSnapshot(_SNAPSHOT, _NAME, _CONTEXT)
#define xsWriteSnapshot(_THE, _SNAPSHOT) \
	fxWriteSnapshot(_THE, _SNAPSHOT)
	
#define xsRunDebugger(_THE) \
	fxRunDebugger(_THE)
#define xsRunModuleFile(_PATH) \
	fxRunModuleFile(the, _PATH)
#define xsRunProgramFile(_PATH) \
	fxRunProgramFile(the, _PATH)
#define xsRunLoop(_THE) \
	fxRunLoop(_THE)
#define xsRunProgramBuffer(_BUFFER,_SIZE) \
	(fxRunProgramBuffer(the, _BUFFER, _SIZE), \
	fxPop())

#define xsClearTimer() \
	fxClearTimer(the)
#define xsSetTimer(_INTERVAL, _REPEAT) \
	fxSetTimer(the, _INTERVAL, _REPEAT)
	
#define xsVersion(_BUFFER, _SIZE) \
	fxVersion(_BUFFER, _SIZE)

#ifdef mxInstrument	
#define xsDescribeInstrumentation(_THE,_COUNT,_NAMES,_UNITS) \
	fxDescribeInstrumentation(_THE,_COUNT,_NAMES,_UNITS)
#define xsSampleInstrumentation(_THE,_COUNT,_VALUES) \
	fxSampleInstrumentation(_THE,_COUNT,_VALUES)
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern void fxInitializeSharedCluster();
extern void fxTerminateSharedCluster();

#ifdef mxMetering
mxImport void fxBeginMetering(xsMachine* the, xsBooleanValue (*callback)(xsMachine*, uint64_t), uint64_t interval);
mxImport void fxEndMetering(xsMachine* the);
mxImport void fxMeterHostFunction(xsMachine* the, uint64_t count);
mxImport void fxPatchHostFunction(xsMachine* the, xsCallback patch);
mxImport uint64_t fxGetCurrentMeter(xsMachine* the);
mxImport void fxSetCurrentMeter(xsMachine* the, uint64_t value);
#endif

mxImport xsMachine* fxReadSnapshot(xsSnapshot* snapshot, xsStringValue theName, void* theContext);
mxImport int fxUseSnapshot(xsMachine* the, xsSnapshot* snapshot);
mxImport int fxWriteSnapshot(xsMachine* the, xsSnapshot* snapshot);

mxImport void fxRunDebugger(xsMachine* the);
mxImport void fxRunModuleFile(xsMachine* the, xsStringValue path);
mxImport void fxRunProgramFile(xsMachine* the, xsStringValue path);
mxImport void fxRunProgramBuffer(xsMachine* the, xsStringValue buffer, xsIntegerValue size);
mxImport void fxRunLoop(xsMachine* the);

mxImport void fxClearTimer(xsMachine* the);
mxImport void fxSetTimer(xsMachine* the, xsNumberValue interval, xsBooleanValue repeat);

#ifdef mxInstrument	
mxImport void fxDescribeInstrumentation(xsMachine* the, xsIntegerValue count, xsStringValue* names, xsStringValue* units);
mxImport void fxSampleInstrumentation(xsMachine* the, xsIntegerValue count, xsIntegerValue* values);
#endif

mxImport void fxVersion(xsStringValue theBuffer, xsUnsignedValue theSize);

mxImport void fx_lockdown(xsMachine* the);
mxImport void fx_harden(xsMachine* the);
mxImport void fx_petrify(xsMachine* the);
mxImport void fx_mutabilities(xsMachine* the);
mxImport void fx_unicodeCompare(xsMachine* the);

typedef void (*txSharedTimerCallback)(txSharedTimer* timer, void *refcon, int refconSize);

extern void fxInitializeSharedTimers();
extern void fxTerminateSharedTimers();
extern void fxRescheduleSharedTimer(txSharedTimer* timer, double timeout, double interval);
extern void* fxScheduleSharedTimer(double timeout, double interval, txSharedTimerCallback callback, void* refcon, int refconSize);
extern void fxUnscheduleSharedTimer(txSharedTimer* timer);

#ifdef __cplusplus
}
#endif

#endif /* __XSNAP__ */
