#include "xsAll.h"
#include "xsScript.h"
#include "xsSnapshot.h"

#ifndef mxReserveChunkSize
	#define mxReserveChunkSize 1024 * 1024 * 1024
#endif

extern txScript* fxLoadScript(txMachine* the, txString path, txUnsigned flags);

mxExport void fxRunModuleFile(txMachine* the, txString path);
mxExport void fxRunProgramFile(txMachine* the, txString path);
mxExport void fxRunLoop(txMachine* the);

mxExport void fxClearTimer(txMachine* the);
mxExport void fxSetTimer(txMachine* the, txNumber interval, txBoolean repeat);

mxExport void fxVersion(txString theBuffer, txSize theSize);
#ifdef mxMetering
mxExport txUnsigned fxGetCurrentMeter(txMachine* the);
mxExport void fxSetCurrentMeter(txMachine* the, txUnsigned value);
#endif

typedef struct sxJob txJob;

struct sxJob {
	txJob* next;
	txMachine* the;
	txNumber when;
	txSlot self;
	txSlot function;
	txSlot argument;
	txNumber interval;
};

static void fxFulfillModuleFile(txMachine* the);
static void fxRejectModuleFile(txMachine* the);

static void fxDestroyTimer(void* data);
static void fxMarkTimer(txMachine* the, void* it, txMarkRoot markRoot);

static txHostHooks gxTimerHooks = {
	fxDestroyTimer,
	fxMarkTimer
};

void fxClearTimer(txMachine* the)
{
	txHostHooks* hooks = fxGetHostHooks(the, mxArgv(0));
	if (hooks == &gxTimerHooks) {
		txJob* job = fxGetHostData(the, mxArgv(0));
		if (job) {
			fxForget(the, &job->self);
			fxSetHostData(the, mxArgv(0), NULL);
			job->the = NULL;
		}
	}
	else
		mxTypeError("no timer");
}

void fxDestroyTimer(void* data)
{
}

void fxMarkTimer(txMachine* the, void* it, txMarkRoot markRoot)
{
	txJob* job = it;
	if (job) {
		(*markRoot)(the, &job->function);
		(*markRoot)(the, &job->argument);
	}
}

void fxSetTimer(txMachine* the, txNumber interval, txBoolean repeat)
{
	c_timeval tv;
	txJob* job;
	txJob** address = (txJob**)&(the->timerJobs);
	while ((job = *address))
		address = &(job->next);
	job = *address = malloc(sizeof(txJob));
	c_memset(job, 0, sizeof(txJob));
	job->the = the;
	c_gettimeofday(&tv, NULL);
	if (repeat)
		job->interval = interval;
	job->when = ((txNumber)(tv.tv_sec) * 1000.0) + ((txNumber)(tv.tv_usec) / 1000.0) + interval;
	fxNewHostObject(the, NULL);
    mxPull(job->self);
	job->function = *mxArgv(0);
	if (mxArgc > 2)
		job->argument = *mxArgv(2);
	else
		job->argument = mxUndefined;
	fxSetHostData(the, &job->self, job);
	fxSetHostHooks(the, &job->self, &gxTimerHooks);
	fxRemember(the, &job->self);
	fxAccess(the, &job->self);
	*mxResult = the->scratch;
}

/* PLATFORM */

void fxAbort(txMachine* the, int status)
{
	switch (status) {
	case XS_STACK_OVERFLOW_EXIT:
		fxReport(the, "stack overflow\n");
#ifdef mxDebug
		fxDebugger(the, (char *)__FILE__, __LINE__);
#endif
		the->abortStatus = status;
		fxExitToHost(the);
		break;
	case XS_NOT_ENOUGH_MEMORY_EXIT:
		fxReport(the, "memory full\n");
#ifdef mxDebug
		fxDebugger(the, (char *)__FILE__, __LINE__);
#endif
		the->abortStatus = status;
		fxExitToHost(the);
		break;
	case XS_NO_MORE_KEYS_EXIT:
		fxReport(the, "not enough keys\n");
#ifdef mxDebug
		fxDebugger(the, (char *)__FILE__, __LINE__);
#endif
		the->abortStatus = status;
		fxExitToHost(the);
		break;
	case XS_TOO_MUCH_COMPUTATION_EXIT:
		fxReport(the, "too much computation\n");
#ifdef mxDebug
		fxDebugger(the, (char *)__FILE__, __LINE__);
#endif
		the->abortStatus = status;
		fxExitToHost(the);
		break;
	case XS_UNHANDLED_EXCEPTION_EXIT:
		fxReport(the, "unhandled exception: %s\n", fxToString(the, &mxException));
		mxException = mxUndefined;
		break;
	case XS_UNHANDLED_REJECTION_EXIT:
		fxReport(the, "unhandled rejection: %s\n", fxToString(the, &mxException));
		mxException = mxUndefined;
		break;
	default:
		fxReport(the, "fxAbort(%d) - %s\n", status, fxToString(the, &mxException));
#ifdef mxDebug
		fxDebugger(the, (char *)__FILE__, __LINE__);
#endif
		the->abortStatus = status;
		fxExitToHost(the);
		break;
	}
}

static txSize gxPageSize = 0;

static txSize fxRoundToPageSize(txMachine* the, txSize size)
{
	txSize modulo;
	if (!gxPageSize) {
#if mxWindows
		SYSTEM_INFO info;
		GetSystemInfo(&info);
		gxPageSize = (txSize)info.dwAllocationGranularity;
#else
		gxPageSize = getpagesize();
#endif
	}
	modulo = size & (gxPageSize - 1);
	if (modulo)
		size = fxAddChunkSizes(the, size, gxPageSize - modulo);
	return size;
}

static void adjustSpaceMeter(txMachine* the, txSize theSize)
{
	size_t previous = the->allocatedSpace;
	the->allocatedSpace += theSize;
	if (the->allocatedSpace > the->allocationLimit ||
		// overflow?
		the->allocatedSpace < previous) {
		fxAbort(the, XS_NOT_ENOUGH_MEMORY_EXIT);
	}
}

void* fxAllocateChunks(txMachine* the, txSize size)
{
	txByte* base;
	txByte* result;
	adjustSpaceMeter(the, size);
	if (the->firstBlock) {
		base = (txByte*)(the->firstBlock);
		result = (txByte*)(the->firstBlock->limit);
	}
	else
#if mxWindows
		base = result = VirtualAlloc(NULL, mxReserveChunkSize, MEM_RESERVE, PAGE_READWRITE);
#else
		base = result = mmap(NULL, mxReserveChunkSize, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
#endif
	if (result) {
		txSize current = (txSize)(result - base);
		size = fxAddChunkSizes(the, current, size);
		current = fxRoundToPageSize(the, current);
		size = fxRoundToPageSize(the, size);
#if mxWindows
		if (!VirtualAlloc(base + current, size - current, MEM_COMMIT, PAGE_READWRITE))
#else
		if (size > mxReserveChunkSize)
			result = NULL;
		else if (mprotect(base + current, size - current, PROT_READ | PROT_WRITE))
#endif
			result = NULL;
	}
	return result;
}

void fxFreeChunks(txMachine* the, void* theChunks)
{
#if mxWindows
	VirtualFree(theChunks, 0, MEM_RELEASE);
#else
	munmap(theChunks, mxReserveChunkSize);
#endif
}

txSlot* fxAllocateSlots(txMachine* the, txSize theCount)
{
	// fprintf(stderr, "fxAllocateSlots(%u) * %d = %ld\n", theCount, sizeof(txSlot), theCount * sizeof(txSlot));
	adjustSpaceMeter(the, theCount * sizeof(txSlot));
	return (txSlot*)c_malloc(theCount * sizeof(txSlot));
}

void fxFreeSlots(txMachine* the, void* theSlots)
{
	c_free(theSlots);
}

void fxCreateMachinePlatform(txMachine* the)
{
#ifdef mxDebug
	the->connection = mxNoSocket;
#endif
	// Original 10x strategy:
	// SLOGFILE=out.slog agoric start local-chain
	// jq -s '.|.[]|.dr[2].allocate' < out.slog|grep -v null|sort -u | sort -nr
	// int MB = 1024 * 1024;
	// int measured_max = 30 * MB;
	// the->allocationLimit = 10 * measured_max;

	size_t GB = 1024 * 1024 * 1024;
	the->allocationLimit = 2 * GB;
}

void fxDeleteMachinePlatform(txMachine* the)
{
}

void fxQueuePromiseJobs(txMachine* the)
{
	the->promiseJobs = 1;
}

void fxRunLoop(txMachine* the)
{
	c_timeval tv;
	txNumber when;
	txJob* job;
	txJob** address;
	for (;;) {
		while (the->promiseJobs) {
			while (the->promiseJobs) {
				the->promiseJobs = 0;
				fxRunPromiseJobs(the);
			}
			fxEndJob(the);
		}
		c_gettimeofday(&tv, NULL);
		when = ((txNumber)(tv.tv_sec) * 1000.0) + ((txNumber)(tv.tv_usec) / 1000.0);
		address = (txJob**)&(the->timerJobs);
		if (!*address)
			break;
		while ((job = *address)) {
			txMachine* the = job->the;
			if (the) {
				if (job->when <= when) {
					fxBeginHost(the);
					mxTry(the) {
						mxPushUndefined();
						mxPush(job->function);
						mxCall();
						mxPush(job->argument);
						mxRunCount(1);
						mxPop();
						if (job->the) {
							if (job->interval) {
								job->when += job->interval;
							}
							else {
								fxAccess(the, &job->self);
								*mxResult = the->scratch;
								fxForget(the, &job->self);
								fxSetHostData(the, mxResult, NULL);
								job->the = NULL;
							}
						}
					}
					mxCatch(the) {
						fxAbort(the, XS_UNHANDLED_EXCEPTION_EXIT);
					}
					fxEndHost(the);
					break; // to run promise jobs queued by the timer in the same "tick"
				}
				address = &(job->next);
			}
			else {
				*address = job->next;
				c_free(job);
			}
		}
	}
}

void fxFulfillModuleFile(txMachine* the)
{
	mxException = mxUndefined;
}

void fxRejectModuleFile(txMachine* the)
{
	mxException = *mxArgv(0);
}

void fxRunModuleFile(txMachine* the, txString path)
{
	txSlot* realm = mxProgram.value.reference->next->value.module.realm;
	mxPushStringC(path);
	fxRunImport(the, realm, XS_NO_ID);
	mxDub();
	fxGetID(the, mxID(_then));
	mxCall();
	fxNewHostFunction(the, fxFulfillModuleFile, 1, XS_NO_ID);
	fxNewHostFunction(the, fxRejectModuleFile, 1, XS_NO_ID);
	mxRunCount(2);
	mxPop();
}

void fxRunProgramFile(txMachine* the, txString path)
{
	txSlot* realm = mxProgram.value.reference->next->value.module.realm;
	txScript* script = fxLoadScript(the, path, mxProgramFlag | mxDebugFlag);
	mxModuleInstanceInternal(mxProgram.value.reference)->value.module.id = fxID(the, path);
	fxRunScript(the, script, mxRealmGlobal(realm), C_NULL, mxRealmClosures(realm)->value.reference, C_NULL, mxProgram.value.reference);
	mxPullSlot(mxResult);
}

/* DEBUG */

#ifdef mxDebug

void fxConnect(txMachine* the)
{
	char name[256];
	char* colon;
	int port;
	struct sockaddr_in address;
#if mxWindows
	if (GetEnvironmentVariable("XSBUG_HOST", name, sizeof(name))) {
#else
	colon = getenv("XSBUG_HOST");
	if ((colon) && (c_strlen(colon) + 1 < sizeof(name))) {
		c_strcpy(name, colon);
#endif		
		colon = strchr(name, ':');
		if (colon == NULL)
			port = 5002;
		else {
			*colon = 0;
			colon++;
			port = strtol(colon, NULL, 10);
		}
	}
	else {
		strcpy(name, "localhost");
		port = 5002;
	}
	memset(&address, 0, sizeof(address));
  	address.sin_family = AF_INET;
	address.sin_addr.s_addr = inet_addr(name);
	if (address.sin_addr.s_addr == INADDR_NONE) {
		struct hostent *host = gethostbyname(name);
		if (!host)
			return;
		memcpy(&(address.sin_addr), host->h_addr, host->h_length);
	}
  	address.sin_port = htons(port);
#if mxWindows
{  	
	WSADATA wsaData;
	unsigned long flag;
	if (WSAStartup(0x202, &wsaData) == SOCKET_ERROR)
		return;
	the->connection = socket(AF_INET, SOCK_STREAM, 0);
	if (the->connection == INVALID_SOCKET)
		return;
  	flag = 1;
  	ioctlsocket(the->connection, FIONBIO, &flag);
	if (connect(the->connection, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
		if (WSAEWOULDBLOCK == WSAGetLastError()) {
			fd_set fds;
			struct timeval timeout = { 2, 0 }; // 2 seconds, 0 micro-seconds
			FD_ZERO(&fds);
			FD_SET(the->connection, &fds);
			if (select(0, NULL, &fds, NULL, &timeout) == 0)
				goto bail;
			if (!FD_ISSET(the->connection, &fds))
				goto bail;
		}
		else
			goto bail;
	}
 	flag = 0;
 	ioctlsocket(the->connection, FIONBIO, &flag);
}
#else
{  	
	int	flag;
	the->connection = socket(AF_INET, SOCK_STREAM, 0);
	if (the->connection <= 0)
		goto bail;
	c_signal(SIGPIPE, SIG_IGN);
#if mxMacOSX
	{
		int set = 1;
		setsockopt(the->connection, SOL_SOCKET, SO_NOSIGPIPE, (void *)&set, sizeof(int));
	}
#endif
	flag = fcntl(the->connection, F_GETFL, 0);
	fcntl(the->connection, F_SETFL, flag | O_NONBLOCK);
	if (connect(the->connection, (struct sockaddr*)&address, sizeof(address)) < 0) {
    	 if (errno == EINPROGRESS) { 
			fd_set fds;
			struct timeval timeout = { 2, 0 }; // 2 seconds, 0 micro-seconds
			int error = 0;
			unsigned int length = sizeof(error);
			FD_ZERO(&fds);
			FD_SET(the->connection, &fds);
			if (select(the->connection + 1, NULL, &fds, NULL, &timeout) == 0)
				goto bail;
			if (!FD_ISSET(the->connection, &fds))
				goto bail;
			if (getsockopt(the->connection, SOL_SOCKET, SO_ERROR, &error, &length) < 0)
				goto bail;
			if (error)
				goto bail;
		}
		else
			goto bail;
	}
	fcntl(the->connection, F_SETFL, flag);
	c_signal(SIGPIPE, SIG_DFL);
}
#endif
	return;
bail:
	fxDisconnect(the);
}

void fxDisconnect(txMachine* the)
{
#if mxWindows
	if (the->connection != INVALID_SOCKET) {
		closesocket(the->connection);
		the->connection = INVALID_SOCKET;
	}
	WSACleanup();
#else
	if (the->connection >= 0) {
		close(the->connection);
		the->connection = -1;
	}
#endif
}

txBoolean fxIsConnected(txMachine* the)
{
	return (the->connection != mxNoSocket) ? 1 : 0;
}

txBoolean fxIsReadable(txMachine* the)
{
	return 0;
}

void fxReceive(txMachine* the)
{
	int count;
	if (the->connection != mxNoSocket) {
#if mxWindows
		count = recv(the->connection, the->debugBuffer, sizeof(the->debugBuffer) - 1, 0);
		if (count < 0)
			fxDisconnect(the);
		else
			the->debugOffset = count;
#else
	again:
		count = read(the->connection, the->debugBuffer, sizeof(the->debugBuffer) - 1);
		if (count < 0) {
			if (errno == EINTR)
				goto again;
			else
				fxDisconnect(the);
		}
		else
			the->debugOffset = count;
#endif
	}
	the->debugBuffer[the->debugOffset] = 0;
}

void fxSend(txMachine* the, txBoolean more)
{
	if (the->connection != mxNoSocket) {
#if mxWindows
		if (send(the->connection, the->echoBuffer, the->echoOffset, 0) <= 0)
			fxDisconnect(the);
#else
	again:
		if (write(the->connection, the->echoBuffer, the->echoOffset) <= 0) {
			if (errno == EINTR)
				goto again;
			else
				fxDisconnect(the);
		}
#endif
	}
}

#endif /* mxDebug */

void fxVersion(txString theBuffer, txSize theSize)
{
	c_snprintf(theBuffer, theSize, "%d.%d.%d", XS_MAJOR_VERSION, XS_MINOR_VERSION, XS_PATCH_VERSION);
}

#ifdef mxMetering
txUnsigned fxGetCurrentMeter(txMachine* the)
{
	return the->meterIndex;
}

void fxSetCurrentMeter(txMachine* the, txUnsigned value)
{
	the->meterIndex = value;
}
#endif

void fxSetHostFunctionProperty(txMachine* the, txSlot* property, txCallback call, txInteger length, txID id)
{
	txSlot* home = the->stack;
	txSlot* function = fxNewHostFunction(the, call, length, id);
	txSlot* slot = mxFunctionInstanceHome(function);
	slot->value.home.object = home->value.reference;
	property->kind = the->stack->kind;
	property->value = the->stack->value;
	mxPop();
}

txSize fxGetCurrentHeapCount(txMachine* the)
{
	return the->currentHeapCount;
}

void fx_lockdown(txMachine* the)
{
#define mxHardenBuiltInCall \
	mxPush(mxGlobal); \
	mxPushSlot(harden); \
	mxCall()
#define mxHardenBuiltInRun \
	mxRunCount(1); \
	mxPop()

	txSlot* instance;
	txSlot* property;
	txSlot* item;
	txSlot* harden;
	txInteger id;
	
	if (mxProgram.value.reference->flag & XS_DONT_MARSHALL_FLAG)
		mxTypeError("lockdown already called");
	mxProgram.value.reference->flag |= XS_DONT_MARSHALL_FLAG;

	property = mxBehaviorGetProperty(the, mxAsyncFunctionPrototype.value.reference, mxID(_constructor), 0, XS_OWN);
	property->kind = mxThrowTypeErrorFunction.kind;
	property->value = mxThrowTypeErrorFunction.value;
	property = mxBehaviorGetProperty(the, mxAsyncGeneratorFunctionPrototype.value.reference, mxID(_constructor), 0, XS_OWN);
	property->kind = mxThrowTypeErrorFunction.kind;
	property->value = mxThrowTypeErrorFunction.value;
	property = mxBehaviorGetProperty(the, mxFunctionPrototype.value.reference, mxID(_constructor), 0, XS_OWN);
	property->kind = mxThrowTypeErrorFunction.kind;
	property->value = mxThrowTypeErrorFunction.value;
	property = mxBehaviorGetProperty(the, mxGeneratorFunctionPrototype.value.reference, mxID(_constructor), 0, XS_OWN);
	property->kind = mxThrowTypeErrorFunction.kind;
	property->value = mxThrowTypeErrorFunction.value;
	property = mxBehaviorGetProperty(the, mxCompartmentPrototype.value.reference, mxID(_constructor), 0, XS_OWN);
	property->kind = mxThrowTypeErrorFunction.kind;
	property->value = mxThrowTypeErrorFunction.value;

	instance = fxNewArray(the, _Compartment);
	property = the->stackPrototypes - 1;
	item = instance->next->value.array.address;
	for (id = 0; id < XS_SYMBOL_ID_COUNT; id++) {
		*((txIndex*)item) = id;
		property--;
		item++;
	}
	for (; id < _Compartment; id++) {
		*((txIndex*)item) = id;
		item->kind = property->kind;
		item->value = property->value;
		property--;
		item++;
	}
	
	fxDuplicateInstance(the, mxDateConstructor.value.reference);
	property = mxFunctionInstanceCode(the->stack->value.reference);
	property->value.callback.address = mxCallback(fx_Date_secure);
	property = mxBehaviorGetProperty(the, the->stack->value.reference, mxID(_now), 0, XS_OWN);
	fxSetHostFunctionProperty(the, property, mxCallback(fx_Date_now_secure), 0, mxID(_now));
	property = mxBehaviorGetProperty(the, mxDatePrototype.value.reference, mxID(_constructor), 0, XS_OWN);
	property->kind = the->stack->kind;
	property->value = the->stack->value;
	mxPull(instance->next->value.array.address[_Date]);
	
	fxDuplicateInstance(the, mxMathObject.value.reference);
	property = mxBehaviorGetProperty(the, the->stack->value.reference, mxID(_random), 0, XS_OWN);
	fxSetHostFunctionProperty(the, property, mxCallback(fx_Math_random_secure), 0, mxID(_random));
	mxPull(instance->next->value.array.address[_Math]);

	mxPull(mxCompartmentGlobal);

	mxTemporary(harden);
	mxPush(mxGlobal);
	fxGetID(the, fxID(the, "harden"));
	mxPullSlot(harden);
	
	for (id = XS_SYMBOL_ID_COUNT; id < _Infinity; id++) {
		mxHardenBuiltInCall; mxPush(the->stackPrototypes[-1 - id]); mxHardenBuiltInRun;
	}
	for (id = _Compartment; id < XS_INTRINSICS_COUNT; id++) {
		mxHardenBuiltInCall; mxPush(the->stackPrototypes[-1 - id]); mxHardenBuiltInRun;
	}
	
	mxHardenBuiltInCall; mxPush(mxArgumentsSloppyPrototype); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPush(mxArgumentsStrictPrototype); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPush(mxArrayIteratorPrototype); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPush(mxAsyncFromSyncIteratorPrototype); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPush(mxAsyncFunctionPrototype); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPush(mxAsyncGeneratorFunctionPrototype); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPush(mxAsyncGeneratorPrototype); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPush(mxAsyncIteratorPrototype); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPush(mxGeneratorFunctionPrototype); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPush(mxGeneratorPrototype); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPush(mxHostPrototype); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPush(mxIteratorPrototype); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPush(mxMapIteratorPrototype); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPush(mxModulePrototype); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPush(mxRegExpStringIteratorPrototype); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPush(mxSetIteratorPrototype); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPush(mxStringIteratorPrototype); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPush(mxTransferPrototype); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPush(mxTypedArrayPrototype); mxHardenBuiltInRun;

	mxHardenBuiltInCall; mxPush(mxAssignObjectFunction); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPush(mxCopyObjectFunction); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPush(mxEnumeratorFunction); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPush(mxInitializeRegExpFunction); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPush(mxOnRejectedPromiseFunction); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPush(mxOnResolvedPromiseFunction); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPush(mxOnThenableFunction); mxHardenBuiltInRun;
	
	mxHardenBuiltInCall; mxPushReference(mxArrayLengthAccessor.value.accessor.getter); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPushReference(mxArrayLengthAccessor.value.accessor.setter); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPushReference(mxStringAccessor.value.accessor.getter); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPushReference(mxStringAccessor.value.accessor.setter); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPushReference(mxProxyAccessor.value.accessor.getter); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPushReference(mxProxyAccessor.value.accessor.setter); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPushReference(mxTypedArrayAccessor.value.accessor.getter); mxHardenBuiltInRun;
	mxHardenBuiltInCall; mxPushReference(mxTypedArrayAccessor.value.accessor.setter); mxHardenBuiltInRun;
	
	mxHardenBuiltInCall; mxPush(mxArrayPrototype); fxGetID(the, mxID(_Symbol_unscopables)); mxHardenBuiltInRun;
	
	mxHardenBuiltInCall; mxPush(mxCompartmentGlobal); mxHardenBuiltInRun;
	
	mxFunctionInstanceCode(mxThrowTypeErrorFunction.value.reference)->ID = XS_NO_ID; 
	mxFunctionInstanceHome(mxThrowTypeErrorFunction.value.reference)->value.home.object = NULL;

	mxPop();
}

static void fx_hardenQueue(txMachine* the, txSlot* list, txSlot* instance, txFlag flag)
{
	txSlot* item;
	if (instance->flag & flag)
		return;
	item = fxNewSlot(the);
	item->value.reference = instance;
	item->kind = XS_REFERENCE_KIND;
	list->value.list.last->next = item;
	list->value.list.last = item;
}

static void fx_hardenFreezeAndTraverse(txMachine* the, txSlot* reference, txSlot* freeze, txSlot* list, txFlag flag)
{
	txSlot* instance = reference->value.reference;
	txSlot* at;
	txSlot* property;
	txSlot* slot;
	
	mxPush(mxObjectConstructor);
	mxPushSlot(freeze);
	mxCall();
	mxPushSlot(reference);
	mxRunCount(1);
	mxPop();
	if (flag == XS_DONT_MODIFY_FLAG) {
		property = instance->next;
		while (property) {
			if (property->flag & XS_INTERNAL_FLAG) {
				switch (property->kind) {
				case XS_ARRAY_BUFFER_KIND:
				case XS_DATE_KIND:
				case XS_MAP_KIND:
				case XS_SET_KIND:
				case XS_WEAK_MAP_KIND:
				case XS_WEAK_SET_KIND:
					property->flag |= XS_DONT_SET_FLAG;
					break;				
				case XS_PRIVATE_KIND:
					slot = property->value.private.first;
					while (slot) {
						if (slot->kind != XS_ACCESSOR_KIND) 
							slot->flag |= XS_DONT_SET_FLAG;
						slot->flag |= XS_DONT_DELETE_FLAG;
						slot = slot->next;
					}
					break;
				}
			}	
			property = property->next;
		}	
	}	
	instance->flag |= flag;

	at = fxNewInstance(the);
	mxBehaviorOwnKeys(the, instance, XS_EACH_NAME_FLAG | XS_EACH_SYMBOL_FLAG, at);
	
	mxTemporary(property);
	mxBehaviorGetPrototype(the, instance, property);
	if (property->kind == XS_REFERENCE_KIND)
		fx_hardenQueue(the, list, property->value.reference, flag);
	
	while ((at = at->next)) {
		if (mxBehaviorGetOwnProperty(the, instance, at->value.at.id, at->value.at.index, property)) {
			if (property->kind == XS_REFERENCE_KIND)
				fx_hardenQueue(the, list, property->value.reference, flag);
			else if (property->kind == XS_ACCESSOR_KIND) {
				if (property->value.accessor.getter)
					fx_hardenQueue(the, list, property->value.accessor.getter, flag);
				if (property->value.accessor.setter)
					fx_hardenQueue(the, list, property->value.accessor.setter, flag);
			}
		}
	}
	
	if (flag == XS_DONT_MODIFY_FLAG) {
		property = instance->next;
		while (property) {
			if (property->flag & XS_INTERNAL_FLAG) {
				if (property->kind == XS_PRIVATE_KIND) {
					txSlot* item = property->value.private.first;
					while (item) {
						if (property->kind == XS_REFERENCE_KIND)
							fx_hardenQueue(the, list, property->value.reference, flag);
						else if (property->kind == XS_ACCESSOR_KIND) {
							if (property->value.accessor.getter)
								fx_hardenQueue(the, list, property->value.accessor.getter, flag);
							if (property->value.accessor.setter)
								fx_hardenQueue(the, list, property->value.accessor.setter, flag);
						}
						item = item->next;
					}
				}
				else if (property->kind == XS_DATA_VIEW_KIND) {
					property = property->next;
					fx_hardenQueue(the, list, property->value.reference, flag);
				}
			}
			property = property->next;
		}
	}
	
	mxPop();
	mxPop();
}

void fx_harden(txMachine* the)
{
	txFlag flag = XS_DONT_MARSHALL_FLAG;
	txSlot* freeze;
	txSlot* slot;
	txSlot* list;
	txSlot* item;

	if (!(mxProgram.value.reference->flag & XS_DONT_MARSHALL_FLAG))
		mxTypeError("call lockdown before harden");

	if (mxArgc == 0)
		return;
		
	*mxResult = *mxArgv(0);
		
	slot = mxArgv(0);	
	if (slot->kind != XS_REFERENCE_KIND)
		return;
// 	if (mxArgc > 1) {
// 		txString string = fxToString(the, mxArgv(1));
// 		if (c_strcmp(string, "freeze") == 0)
// 			flag = XS_DONT_MARSHALL_FLAG;
// 		else if (c_strcmp(string, "petrify") == 0)
// 			flag = XS_DONT_MODIFY_FLAG;
// 		else
// 			mxTypeError("invalid integrity");
// 	}
	slot = slot->value.reference;
	if (slot->flag & flag)
		return;

	mxTemporary(freeze);
	mxPush(mxObjectConstructor);
	mxGetID(mxID(_freeze));
	mxPullSlot(freeze);
	
	mxTemporary(list);
	list->value.list.first = C_NULL;	
	list->value.list.last = C_NULL;	
	list->kind = XS_LIST_KIND;
		
	item = fxNewSlot(the);
	item->value.reference = slot;
	item->kind = XS_REFERENCE_KIND;
	list->value.list.first = item;
	list->value.list.last = item;
		
	{
		mxTry(the) {
			while (item) {
				fx_hardenFreezeAndTraverse(the, item, freeze, list, flag);
				item = item->next;
			}
		}
		mxCatch(the) {
			item = list->value.list.first;
			while (item) {
				item->value.reference->flag &= ~flag;
				item = item->next;
			}
			fxJump(the);
		}
	}
		
	mxPop();
	mxPop();
}

static void fxVerifyError(txMachine* the, txSlot* path, txID id, txIndex index, txString name);
static void fxVerifyErrorString(txMachine* the, txID id, txIndex index, txString name);
static void fxVerifyInstance(txMachine* the, txSlot* list, txSlot* path, txSlot* instance);
static void fxVerifyProperty(txMachine* the, txSlot *list, txSlot *path, txSlot* property, txID id, txIndex index, txString name);
static void fxVerifyQueue(txMachine* the, txSlot* list, txSlot* path, txSlot* instance, txID id, txIndex index, txString name);

void fx_purify(txMachine* the)
{
	txSlot* slot;
	txSlot* list;
	txSlot* item;
	
	fxString(the, mxResult, "");
	if (mxArgc == 0)
		return;
	slot = mxArgv(0);	
	if (slot->kind != XS_REFERENCE_KIND)
		return;
	
	mxTemporary(list);
	list->value.list.first = C_NULL;	
	list->value.list.last = C_NULL;	
	list->kind = XS_LIST_KIND;
		
	item = fxNewSlot(the);
	item->value.list.first = C_NULL;
	item->value.list.last = slot->value.reference;
	item->kind = XS_LIST_KIND;
	list->value.list.first = item;
	list->value.list.last = item;
		
	{
		mxTry(the) {
			while (item) {
				fxVerifyInstance(the, list, item->value.list.first, item->value.list.last);
				item = item->next;
			}
			item = list->value.list.first;
			while (item) {
				item->value.list.last->flag &= ~XS_LEVEL_FLAG;
				item = item->next;
			}
		}
		mxCatch(the) {
			item = list->value.list.first;
			while (item) {
				item->value.list.last->flag &= ~XS_LEVEL_FLAG;
				item = item->next;
			}
			fxJump(the);
		}
	}
		
	mxPop();
}

void fxVerifyError(txMachine* the, txSlot* path, txID id, txIndex index, txString string)
{
	if (mxResult->value.string[0])
		fxConcatStringC(the, mxResult, "\n");
	txSlot* current = path;
	txSlot* next = C_NULL;
	txSlot* previous = C_NULL;
	while (current) {
		next = current->next;
		current->next = previous;
		previous = current;
		current = next;
	}
	path = previous;
	current = path;
	while (current) {
		if (current->kind == XS_STRING_X_KIND) {
			fxVerifyErrorString(the, XS_NO_ID, 0, current->value.string);
		}
		else {
			fxVerifyErrorString(the, current->value.at.id, current->value.at.index, C_NULL);
		}
		current = current->next;
	}
	fxVerifyErrorString(the, id, index, string);
	current = path;
	next = C_NULL;
	previous = C_NULL;
	while (current) {
		next = current->next;
		current->next = previous;
		previous = current;
		current = next;
	}
}

void fxVerifyErrorString(txMachine* the, txID id, txIndex index, txString string)
{
	if (string) {
		fxConcatStringC(the, mxResult, "[[");
		fxConcatStringC(the, mxResult, string);
		fxConcatStringC(the, mxResult, "]]");
	}
	else if (id != XS_NO_ID) {
		txSlot* key = fxGetKey(the, id);
		if (key) {
			if (key->flag & XS_DONT_ENUM_FLAG) {
				c_snprintf(the->nameBuffer, sizeof(the->nameBuffer), "%s", key->value.key.string);
				fxConcatStringC(the, mxResult, ".");
				fxConcatStringC(the, mxResult, the->nameBuffer);
			}
			else {
				if ((key->kind == XS_KEY_KIND) || (key->kind == XS_KEY_X_KIND))
					c_snprintf(the->nameBuffer, sizeof(the->nameBuffer), "%s", key->value.key.string);
				else if ((key->kind == XS_STRING_KIND) || (key->kind == XS_STRING_X_KIND))
					c_snprintf(the->nameBuffer, sizeof(the->nameBuffer), "%s", key->value.string);
				else
					c_snprintf(the->nameBuffer, sizeof(the->nameBuffer), "");
				fxConcatStringC(the, mxResult, "[Symbol(");
				fxConcatStringC(the, mxResult, the->nameBuffer);
				fxConcatStringC(the, mxResult, ")]");
			}
		}
		else {
			fxConcatStringC(the, mxResult, "[Symbol()]");
		}
	}
	else {
		fxNumberToString(the->dtoa, index, the->nameBuffer, sizeof(the->nameBuffer), 0, 0);
		fxConcatStringC(the, mxResult, "[");
		fxConcatStringC(the, mxResult, the->nameBuffer);
		fxConcatStringC(the, mxResult, "]");
	}
}

void fxVerifyInstance(txMachine* the, txSlot* list, txSlot* path, txSlot* instance)
{
	txSlot* property;
	txSlot* prototype;
	
	instance->flag |= XS_LEVEL_FLAG;
	
	if (instance->next && (instance->next->ID == XS_ENVIRONMENT_BEHAVIOR)) {
		txSlot* property = instance->next->next;
		while (property) {
			if (property->ID != XS_NO_ID) { // skip private fields initializers
				txSlot* closure = property->value.closure;
				if (!(closure->flag & XS_DONT_SET_FLAG)) {
					fxVerifyError(the, path, property->ID, 0, C_NULL);
				}
				if (closure->kind == XS_REFERENCE_KIND) {
					fxVerifyQueue(the, list, path, closure->value.reference, property->ID, 0, C_NULL);
				}
			}
			property = property->next;
		}
		return;
	}
	
	if (!(instance->flag & XS_DONT_PATCH_FLAG)) {
		fxVerifyError(the, path, XS_NO_ID, 0, "Extensible");
	}

	prototype = fxGetPrototype(the, instance);
	if (prototype) {
		fxVerifyQueue(the, list, path, prototype, mxID(___proto__), 0, C_NULL);
	}
	
	property = instance->next;
	while (property) {
		if (property->flag & XS_INTERNAL_FLAG) {
			switch (property->kind) {
			case XS_ARRAY_KIND: 
				{
					txSlot* address = property->value.array.address;
					if (address) {
						txIndex offset = 0, size = (((txChunk*)(((txByte*)address) - sizeof(txChunk)))->size) / sizeof(txSlot);
						while (offset < size) {
							address = property->value.array.address + offset;
							fxVerifyProperty(the, list, path, address, XS_NO_ID, *((txIndex*)address), C_NULL);
							offset++;
						}
					}
				} 
				break;
			case XS_ARRAY_BUFFER_KIND:
				if (!(property->flag & XS_DONT_SET_FLAG)) {
					if (property->value.arrayBuffer.address != C_NULL)
						fxVerifyError(the, path, XS_NO_ID, 0, "ArrayBufferData");
				}
				break;
			case XS_CODE_KIND:
			case XS_CODE_X_KIND:
				if (property->value.code.closures) {
					fxVerifyQueue(the, list, path, property->value.code.closures, XS_NO_ID, 0, "Environment");
				}
				break;
			case XS_DATA_VIEW_KIND:
				property = property->next;
				fxVerifyQueue(the, list, path, property->value.reference, XS_NO_ID, 0, "ViewedArrayBuffer");
				break;
			case XS_DATE_KIND:
				if (!(property->flag & XS_DONT_SET_FLAG))
					fxVerifyError(the, path, XS_NO_ID, 0, "DateValue");
				break;
			case XS_REGEXP_KIND:
				break;
			case XS_MAP_KIND:
				if (!(property->flag & XS_DONT_SET_FLAG))
					fxVerifyError(the, path, XS_NO_ID, 0, "MapData");
				break;
			case XS_PRIVATE_KIND:
				{
					txSlot* item = property->value.private.first;
					while (item) {
						fxVerifyProperty(the, list, path, item, item->ID, 0, C_NULL);
						item = item->next;
					}
				}
				break;
			case XS_PROXY_KIND:
				if (property->value.proxy.handler) {
					fxVerifyQueue(the, list, path, property->value.proxy.target, XS_NO_ID, 0, "ProxyHandler");
				}
				if (property->value.proxy.target) {
					fxVerifyQueue(the, list, path, property->value.proxy.target, XS_NO_ID, 0, "ProxyTarget");
				}
				break;
			case XS_SET_KIND:
				if (!(property->flag & XS_DONT_SET_FLAG))
					fxVerifyError(the, path, XS_NO_ID, 0, "SetData");
				break;
			case XS_TYPED_ARRAY_KIND:
				property = property->next;
				property = property->next;
				fxVerifyQueue(the, list, path, property->value.reference, XS_NO_ID, 0, "ViewedArrayBuffer");
				break;
			case XS_WEAK_MAP_KIND:
				if (!(property->flag & XS_DONT_SET_FLAG))
					fxVerifyError(the, path, XS_NO_ID, 0, "WeakMapData");
				break;
			case XS_WEAK_SET_KIND:
				if (!(property->flag & XS_DONT_SET_FLAG))
					fxVerifyError(the, path, XS_NO_ID, 0, "WeakSetData");
				break;
			}
		}
		else {
			fxVerifyProperty(the, list, path, property, property->ID, 0, C_NULL);
		}
		property = property->next;
	}
}

void fxVerifyProperty(txMachine* the, txSlot *list, txSlot *path, txSlot* property, txID id, txIndex index, txString string)
{
	txBoolean immutable = 1;

	if (property->kind != XS_ACCESSOR_KIND) 
		if (!(property->flag & XS_DONT_SET_FLAG))
			immutable = 0;
	if (!(property->flag & XS_DONT_DELETE_FLAG))
		immutable = 0;
	if (!immutable) {
		fxVerifyError(the, path, id, index, string);
	}	
	if (property->kind == XS_REFERENCE_KIND)
		fxVerifyQueue(the, list, path, property->value.reference, id, index, string);
	else if (property->kind == XS_ACCESSOR_KIND) {
		if (property->value.accessor.getter)
			fxVerifyQueue(the, list, path, property->value.accessor.getter, id, index, string);
		if (property->value.accessor.setter)
			fxVerifyQueue(the, list, path, property->value.accessor.setter, id, index, string);
	}
}

void fxVerifyQueue(txMachine* the, txSlot* list, txSlot* path, txSlot* instance, txID id, txIndex index, txString string)
{
	txSlot* item;
	txSlot* name;
	if (instance->flag & XS_LEVEL_FLAG)
		return;
	item = fxNewSlot(the);
	list->value.list.last->next = item;
	list->value.list.last = item;
	
	item->value.list.first = name = fxNewSlot(the);
	if (string) {
		name->value.string = string;
		name->kind = XS_STRING_X_KIND;
	}
	else {
		name->value.at.id = id;
		name->value.at.index = index;
		name->kind = XS_AT_KIND;
	}
	name->next = path;
	item->value.list.last = instance;
	item->kind = XS_LIST_KIND;
}

extern void fxDumpSnapshot(txMachine* the, txSnapshot* snapshot);

typedef void (*txDumpChunk)(FILE* file, txByte* data, txSize size);

#define mxThrowIf(_ERROR) { if (_ERROR) { snapshot->error = _ERROR; fxJump(the); } }

static void fxDumpChunk(txSlot* slot, txByte* block);
static void fxDumpChunkAddress(FILE* file, void* address);
static void fxDumpChunkArray(FILE* file, txByte* data, txSize size);
static void fxDumpChunkData(FILE* file, txByte* data, txSize size);
static void fxDumpChunkString(FILE* file, txByte* data, txSize size);
static void fxDumpChunkTable(FILE* file, txByte* data, txSize size); 
static void fxDumpID(FILE* file, txID id);
static void fxDumpNumber(FILE* file, txNumber value);
static void fxDumpSlot(FILE* file, txSlot* slot);
static void fxDumpSlotAddress(FILE* file, void* address);
static void fxDumpSlotTable(FILE* file, txByte* buffer, txSize size);

void fxDumpSnapshot(txMachine* the, txSnapshot* snapshot)
{
	Atom atom;
	txByte byte;
	txCreation creation;
	Atom blockAtom;
	txByte* block = C_NULL;
	txByte* blockLimit;
	Atom heapAtom;
	txSlot* heap = C_NULL;
	txSlot* heapLimit;
	Atom stackAtom;
	txSlot* stack = C_NULL;
	txSlot* stackLimit;
	
	txSlot* current;
	
	txByte* buffer = C_NULL;
	txByte* address;
	txSize offset, size;
	txString string;

	mxTry(the) {
		mxThrowIf((*snapshot->read)(snapshot->stream, &atom, sizeof(Atom)));
		atom.atomSize = ntohl(atom.atomSize) - 8;
		fprintf(stderr, "%4.4s %d\n", (txString)&(atom.atomType), atom.atomSize + 8);
		
		mxThrowIf((*snapshot->read)(snapshot->stream, &atom, sizeof(Atom)));
		atom.atomSize = ntohl(atom.atomSize) - 8;
		fprintf(stderr, "%4.4s %d\n", (txString)&(atom.atomType), atom.atomSize + 8);
		mxThrowIf((*snapshot->read)(snapshot->stream, &byte, 1));
		fprintf(stderr, "\t%d.", byte);
		mxThrowIf((*snapshot->read)(snapshot->stream, &byte, 1));
		fprintf(stderr, "%d.", byte);
		mxThrowIf((*snapshot->read)(snapshot->stream, &byte, 1));
		fprintf(stderr, "%d ", byte);
		mxThrowIf((*snapshot->read)(snapshot->stream, &byte, 1));
		fprintf(stderr, "(%d)\n", byte);
		
		mxThrowIf((*snapshot->read)(snapshot->stream, &atom, sizeof(Atom)));
		atom.atomSize = ntohl(atom.atomSize) - 8;
		buffer = c_malloc(atom.atomSize);
		mxThrowIf(buffer == C_NULL);
		mxThrowIf((*snapshot->read)(snapshot->stream, buffer, atom.atomSize));
		fprintf(stderr, "%4.4s %d\n", (txString)&(atom.atomType), atom.atomSize + 8);
		fprintf(stderr, "\t%s\n", (txString)buffer);
		c_free(buffer);
	
		mxThrowIf((*snapshot->read)(snapshot->stream, &atom, sizeof(Atom)));
		atom.atomSize = ntohl(atom.atomSize) - 8;
		mxThrowIf((*snapshot->read)(snapshot->stream, &creation, sizeof(txCreation)));
		fprintf(stderr, "%4.4s %d\n", (txString)&(atom.atomType), atom.atomSize + 8);
		fprintf(stderr, "\tinitialChunkSize: %d\n", creation.initialChunkSize);
		fprintf(stderr, "\tincrementalChunkSize: %d\n", creation.incrementalChunkSize);
		fprintf(stderr, "\tinitialHeapCount: %d\n", creation.initialHeapCount);
		fprintf(stderr, "\tincrementalHeapCount: %d\n", creation.incrementalHeapCount);
		fprintf(stderr, "\tstackCount: %d\n", creation.stackCount);
		fprintf(stderr, "\tkeyCount: %d\n", creation.keyCount);
		fprintf(stderr, "\tnameModulo: %d\n", creation.nameModulo);
		fprintf(stderr, "\tsymbolModulo: %d\n", creation.symbolModulo);
		fprintf(stderr, "\tparserBufferSize: %d\n", creation.parserBufferSize);
		fprintf(stderr, "\tparserTableModulo: %d\n", creation.parserTableModulo);
		fprintf(stderr, "\tstaticSize: %d\n", creation.staticSize);

		mxThrowIf((*snapshot->read)(snapshot->stream, &blockAtom, sizeof(Atom)));
		blockAtom.atomSize = ntohl(blockAtom.atomSize) - 8;
		block = c_malloc(blockAtom.atomSize);
		mxThrowIf(block == C_NULL);
		mxThrowIf((*snapshot->read)(snapshot->stream, block, blockAtom.atomSize));
		blockLimit = block + blockAtom.atomSize;

		mxThrowIf((*snapshot->read)(snapshot->stream, &heapAtom, sizeof(Atom)));
		heapAtom.atomSize = ntohl(heapAtom.atomSize) - 8;
		heap = c_malloc(sizeof(txSlot) + heapAtom.atomSize);
		mxThrowIf(heap == C_NULL);
		c_memset(heap, 0, sizeof(txSlot));
		mxThrowIf((*snapshot->read)(snapshot->stream, heap + 1, heapAtom.atomSize));
		heapLimit = heap + 1 + (heapAtom.atomSize / sizeof(txSlot));
		
		mxThrowIf((*snapshot->read)(snapshot->stream, &stackAtom, sizeof(Atom)));
		stackAtom.atomSize = ntohl(stackAtom.atomSize) - 8;
		stack = c_malloc(stackAtom.atomSize);
		mxThrowIf(stack == C_NULL);
		mxThrowIf((*snapshot->read)(snapshot->stream, stack, stackAtom.atomSize));
		stackLimit = stack + (stackAtom.atomSize / sizeof(txSlot));
		
		current = heap;
		while (current < heapLimit) {
			fxDumpChunk(current, block);
			current++;
		}
		current = stack;
		while (current < stackLimit) {
			fxDumpChunk(current, block);
			current++;
		}

		fprintf(stderr, "%4.4s %d\n", (txString)&(blockAtom.atomType), blockAtom.atomSize + 8);
		address = block;
		offset = 0;
		while (offset < blockAtom.atomSize) {
			txChunk* chunk = (txChunk*)address;
			fprintf(stderr, "\t<%8.8lu> %8d ", offset + sizeof(txChunk), chunk->size);
			if (chunk->temporary)
				(*(txDumpChunk)(chunk->temporary))(stderr, address + sizeof(txChunk), chunk->size - sizeof(txChunk));
			else
				fprintf(stderr, "\n\t\t?");
			fprintf(stderr, "\n");
			address += chunk->size;
			offset += chunk->size;
		}
		
		fprintf(stderr, "%4.4s %d\n", (txString)&(heapAtom.atomType), heapAtom.atomSize + 8);
		current = heap;
		offset = 0;
		while (current < heapLimit) {
			fprintf(stderr, "\t[%8.8d] ", offset);
			fxDumpSlotAddress(stderr, current->next);
			fprintf(stderr, " ");
			fxDumpSlot(stderr, current);
			fprintf(stderr, "\n");
			current++;
			offset++;
		}
		
		fprintf(stderr, "%4.4s %d\n", (txString)&(stackAtom.atomType), stackAtom.atomSize + 8);
		current = stack;
		while (current < stackLimit) {
			fprintf(stderr, "\t           ");
			fxDumpSlotAddress(stderr, current->next);
			fprintf(stderr, " ");
			fxDumpSlot(stderr, current);
			fprintf(stderr, "\n");
			current++;
		}

		mxThrowIf((*snapshot->read)(snapshot->stream, &atom, sizeof(Atom)));
		atom.atomSize = ntohl(atom.atomSize) - 8;
		buffer = c_malloc(atom.atomSize);
		mxThrowIf(buffer == C_NULL);
		mxThrowIf((*snapshot->read)(snapshot->stream, buffer, atom.atomSize));
		fprintf(stderr, "%4.4s %d\n", (txString)&(atom.atomType), atom.atomSize + 8);
		address = buffer;
		offset = 0;
		size = atom.atomSize / sizeof(txSlot*);
		while (offset < size) {
			txSlot* slot = *((txSlot**)address);
			fprintf(stderr, "\tID_%6.6d", offset);
			if (slot) {
				fprintf(stderr, " [%8.8zu]", (size_t)slot);
				slot = ((txSlot*)heap) + (size_t)slot;
				string = ((txString)block) + (size_t)(slot->value.key.string);
				fprintf(stderr, " %s\n", string);
			}
			else
				fprintf(stderr, " [        ]\n");
			address += sizeof(txSlot*);
			offset++;
		}
		c_free(buffer);
		buffer = C_NULL;

		mxThrowIf((*snapshot->read)(snapshot->stream, &atom, sizeof(Atom)));
		atom.atomSize = ntohl(atom.atomSize) - 8;
		buffer = c_malloc(atom.atomSize);
		mxThrowIf(buffer == C_NULL);
		mxThrowIf((*snapshot->read)(snapshot->stream, buffer, atom.atomSize));
		fprintf(stderr, "%4.4s %d", (txString)&(atom.atomType), atom.atomSize + 8);
		fxDumpSlotTable(stderr, buffer, atom.atomSize);
		fprintf(stderr, "\n");

		mxThrowIf((*snapshot->read)(snapshot->stream, &atom, sizeof(Atom)));
		atom.atomSize = ntohl(atom.atomSize) - 8;
		buffer = c_malloc(atom.atomSize);
		mxThrowIf(buffer == C_NULL);
		mxThrowIf((*snapshot->read)(snapshot->stream, buffer, atom.atomSize));
		fprintf(stderr, "%4.4s %d", (txString)&(atom.atomType), atom.atomSize + 8);
		fxDumpSlotTable(stderr, buffer, atom.atomSize);
		fprintf(stderr, "\n");
		
		c_free(stack);
		c_free(heap);
		c_free(block);
	}
	mxCatch(the) {
		if (buffer)
			c_free(buffer);
		if (stack)
			c_free(stack);
		if (heap)
			c_free(heap);
		if (block)
			c_free(block);
	}
}

void fxDumpChunk(txSlot* slot, txByte* block) 
{
	txChunk* chunk;
	switch (slot->kind) {
	case XS_STRING_KIND: {
		chunk = (txChunk*)(block + (size_t)(slot->value.string) - sizeof(txChunk));
		chunk->temporary = (txByte*)fxDumpChunkString;
	} break;
	case XS_BIGINT_KIND: {
		chunk = (txChunk*)(block + (size_t)(slot->value.bigint.data) - sizeof(txChunk));
		chunk->temporary = (txByte*)fxDumpChunkData;
	} break;
	case XS_ARRAY_KIND: {
		if (slot->value.array.address) {
			chunk = (txChunk*)(block + (size_t)(slot->value.array.address) - sizeof(txChunk));
			chunk->temporary = (txByte*)fxDumpChunkArray;
			
			{
				txIndex size = chunk->size / sizeof(txSlot);
				txSlot* item = (txSlot*)(block + (size_t)(slot->value.array.address));
				while (size) {
					fxDumpChunk(item, block);
					size--;
					item++;
				}
			}
			
		}
	} break;
	case XS_ARRAY_BUFFER_KIND: {
		if (slot->value.arrayBuffer.address) {
			chunk = (txChunk*)(block + (size_t)(slot->value.arrayBuffer.address) - sizeof(txChunk));
			chunk->temporary = (txByte*)fxDumpChunkData;
		}
	} break;
	case XS_CODE_KIND:  {
		chunk = (txChunk*)(block + (size_t)(slot->value.code.address) - sizeof(txChunk));
		chunk->temporary = (txByte*)fxDumpChunkData;
	} break;
	case XS_GLOBAL_KIND: {
		chunk = (txChunk*)(block + (size_t)(slot->value.table.address) - sizeof(txChunk));
		chunk->temporary = (txByte*)fxDumpChunkTable;
	} break;
	case XS_MAP_KIND: {
		chunk = (txChunk*)(block + (size_t)(slot->value.table.address) - sizeof(txChunk));
		chunk->temporary = (txByte*)fxDumpChunkTable;
	} break;
	case XS_REGEXP_KIND: {
		if (slot->value.regexp.code) {
			chunk = (txChunk*)(block + (size_t)(slot->value.regexp.code) - sizeof(txChunk));
			chunk->temporary = (txByte*)fxDumpChunkData;
		}
		if (slot->value.regexp.data) {
			chunk = (txChunk*)(block + (size_t)(slot->value.regexp.data) - sizeof(txChunk));
			chunk->temporary = (txByte*)fxDumpChunkData;
		}
	} break;
	case XS_SET_KIND: {
		chunk = (txChunk*)(block + (size_t)(slot->value.table.address) - sizeof(txChunk));
		chunk->temporary = (txByte*)fxDumpChunkTable;
	} break;
	case XS_KEY_KIND: {
		if (slot->value.key.string) {
			chunk = (txChunk*)(block + (size_t)(slot->value.key.string) - sizeof(txChunk));
			chunk->temporary = (txByte*)fxDumpChunkString;
		}
	} break;
	default:
		break;
	}
}

void fxDumpChunkAddress(FILE* file, void* address) 
{
	if (address)
		fprintf(file, "<%8.8zu>", (size_t)address);
	else
		fprintf(file, "<        >");
}

void fxDumpChunkArray(FILE* file, txByte* data, txSize size) 
{
	txSize offset = 0;
	txSlot* slot = (txSlot*)data;
	size /= sizeof(txSlot);
	while (offset < size) {
		fprintf(file, "\n\t\t%8zu ", (size_t)slot->next);
		fxDumpSlot(file, slot);
		offset++;
		slot++;
	}
}

void fxDumpChunkData(FILE* file, txByte* data, txSize size) 
{
	txSize offset = 0;
	txU1* address = (txU1*)data;
	while (offset < size) {
		if (offset % 32)
			fprintf(file, " ");
		else
			fprintf(file, "\n\t\t");
		fprintf(file, "%2.2x", address[offset]);
		offset++;
	}
}

void fxDumpChunkString(FILE* file, txByte* data, txSize size) 
{
	fprintf(file, " %s", data);
}

void fxDumpChunkTable(FILE* file, txByte* data, txSize size) 
{
	txSize offset = 0;
	txSlot** address = (txSlot**)data;
	size /= sizeof(txSlot*);
	while (offset < size) {
		txSlot* slot = *((txSlot**)address);
		if (offset % 8)
			fprintf(file, " ");
		else
			fprintf(file, "\n\t\t");
		fxDumpSlotAddress(file, slot);
		offset++;
		address++;
	}
}

void fxDumpID(FILE* file, txID id)
{
	if (id < 0)
		fprintf(file, "ID_?     ");
	else if (id == 0)
		fprintf(file, "         ");
	else
		fprintf(file, "ID_%6.6d", id);
}

void fxDumpNumber(FILE* file, txNumber value) 
{
	switch (c_fpclassify(value)) {
	case C_FP_INFINITE:
		if (value < 0)
			fprintf(file, "-C_INFINITY");
		else
			fprintf(file, "C_INFINITY");
		break;
	case C_FP_NAN:
		fprintf(file, "C_NAN");
		break;
	default:
		fprintf(file, "%.20e", value);
		break;
	}
}

void fxDumpSlot(FILE* file, txSlot* slot)
{
	if (slot->flag & XS_MARK_FLAG)
		fprintf(file, "M");
	else
		fprintf(file, "_");
	if (slot->kind == XS_INSTANCE_KIND) {
		if (slot->flag & XS_DONT_MARSHALL_FLAG)
			fprintf(file, "H");
		else
			fprintf(file, "_");
		if (slot->flag & XS_LEVEL_FLAG)
			fprintf(file, "L");
		else
			fprintf(file, "_");
		if (slot->flag & XS_DONT_PATCH_FLAG)
			fprintf(file, "P");
		else
			fprintf(file, "_");
		if (slot->flag & XS_FIELD_FLAG)
			fprintf(file, "F");
		else
			fprintf(file, "_");
		if (slot->flag & XS_CAN_CONSTRUCT_FLAG)
			fprintf(file, "N");
		else
			fprintf(file, "_");
		if (slot->flag & XS_CAN_CALL_FLAG)
			fprintf(file, "C");
		else
			fprintf(file, "_");
		if (slot->flag & XS_EXOTIC_FLAG)
			fprintf(file, "X");
		else
			fprintf(file, "_");
	}
	else {
		if (slot->flag & XS_DERIVED_FLAG)
			fprintf(file, "H");
		else
			fprintf(file, "_");
		if (slot->flag & XS_BASE_FLAG)
			fprintf(file, "B");
		else
			fprintf(file, "_");
		if (slot->flag & XS_INSPECTOR_FLAG)
			fprintf(file, "L");
		else
			fprintf(file, "_");
		if (slot->flag & XS_DONT_SET_FLAG)
			fprintf(file, "S");
		else
			fprintf(file, "_");
		if (slot->flag & XS_DONT_ENUM_FLAG)
			fprintf(file, "E");
		else
			fprintf(file, "_");
		if (slot->flag & XS_DONT_DELETE_FLAG)
			fprintf(file, "D");
		else
			fprintf(file, "_");
		if (slot->flag & XS_INTERNAL_FLAG)
			fprintf(file, "I");
		else
			fprintf(file, "_");
	
	}
	fprintf(file, " ");
	fxDumpID(file, slot->ID);
	fprintf(file, " ");
	switch (slot->kind) {
	case XS_UNINITIALIZED_KIND: {
		fprintf(file, "unititialized");
	} break;
	case XS_UNDEFINED_KIND: {
		fprintf(file, "undefined");
	} break;
	case XS_NULL_KIND: {
		fprintf(file, "null");
	} break;
	case XS_BOOLEAN_KIND: {
		fprintf(file, "boolean = %d", slot->value.boolean);
	} break;
	case XS_INTEGER_KIND: {
		fprintf(file, "integer = %d", slot->value.integer);
	} break;
	case XS_NUMBER_KIND: {
		fprintf(file, "number = ");
		fxDumpNumber(file, slot->value.number);
	} break;
	case XS_STRING_KIND: {
		fprintf(file, "string = ");
		fxDumpChunkAddress(file, slot->value.string);
	} break;
	case XS_SYMBOL_KIND: {
		fprintf(file, "symbol = ");
		fxDumpID(file, slot->value.symbol);
	} break;
	case XS_BIGINT_KIND: {
		fprintf(file, "bigint = { .data = ");
		fxDumpChunkAddress(file, slot->value.bigint.data);
		fprintf(file, ", .size = %d, ", slot->value.bigint.size);
		fprintf(file, ".sign = %d, ", slot->value.bigint.sign);
		fprintf(file, " }");
	} break;
	case XS_REFERENCE_KIND: {
		fprintf(file, "reference = ");
		fxDumpSlotAddress(file, slot->value.reference);
	} break;
	case XS_CLOSURE_KIND: {
		fprintf(file, "closure = ");
		fxDumpSlotAddress(file, slot->value.closure);
	} break; 
	case XS_INSTANCE_KIND: {
		fprintf(file, "instance = { .garbage = ");
		fxDumpSlotAddress(file, slot->value.instance.garbage);
		fprintf(file, ", .prototype = ");
		fxDumpSlotAddress(file, slot->value.instance.prototype);
		fprintf(file, " }");
	} break;
	case XS_ARRAY_KIND: {
		fprintf(file, "array = { .address = ");
		fxDumpChunkAddress(file, slot->value.array.address);
		fprintf(file, ", .length = %d }", (int)slot->value.array.length);
	} break;
	case XS_ARRAY_BUFFER_KIND: {
		fprintf(file, "arrayBuffer = { .address = ");
		fxDumpChunkAddress(file, slot->value.arrayBuffer.address);
		fprintf(file, " }");
	} break;
	case XS_BUFFER_INFO_KIND: {
		fprintf(file, "bufferInfo = { .length = %d, maxLength = %d }", slot->value.bufferInfo.length, slot->value.bufferInfo.maxLength);
	} break;
	case XS_CALLBACK_KIND: {
		fprintf(file, "callback");
	} break;
	case XS_CODE_KIND:  {
		fprintf(file, "code = { .address = ");
		fxDumpChunkAddress(file, slot->value.code.address);
		fprintf(file, ", .closures = ");
		fxDumpSlotAddress(file, slot->value.code.closures);
		fprintf(file, " }");
	} break;
	case XS_DATE_KIND: {
		fprintf(file, "date = ");
		fxDumpNumber(file, slot->value.number);
	} break;
	case XS_DATA_VIEW_KIND: {
		fprintf(file, "dataView = { .offset = %d, .size = %d }", slot->value.dataView.offset, slot->value.dataView.size);
	} break;
	case XS_FINALIZATION_CELL_KIND: {
		fprintf(file, "finalizationCell = { .target = ");
		fxDumpSlotAddress(file, slot->value.finalizationCell.target);
		fprintf(file, ", .token = ");
		fxDumpSlotAddress(file, slot->value.finalizationCell.token);
		fprintf(file, " }");
	} break;
	case XS_FINALIZATION_REGISTRY_KIND: {
		fprintf(file, "finalizationRegistry = { .target = ");
		fxDumpSlotAddress(file, slot->value.finalizationRegistry.callback);
		fprintf(file, ", .flags = %d }", slot->value.finalizationRegistry.flags);
	} break;
	case XS_GLOBAL_KIND: {
		fprintf(file, "global = { .address = ");
		fxDumpChunkAddress(file, slot->value.table.address);
		fprintf(file, ", .length = %d }", (int)slot->value.table.length);
	} break;
	case XS_HOST_KIND: {
		fprintf(file, ".kind = XS_HOST_KIND}, ");
	} break;
	case XS_MAP_KIND: {
		fprintf(file, "map = { .address = ");
		fxDumpChunkAddress(file, slot->value.table.address);
		fprintf(file, ", .length = %d }", (int)slot->value.table.length);
	} break;
	case XS_MODULE_KIND: {
		fprintf(file, "module = { .realm = ");
		fxDumpSlotAddress(file, slot->value.module.realm);
		fprintf(file, ", .id = ");
		fxDumpID(file, slot->value.module.id);
		fprintf(file, " }");
	} break;
	case XS_PROGRAM_KIND: {
		fprintf(file, "program = { .realm = ");
		fxDumpSlotAddress(file, slot->value.module.realm);
		fprintf(file, ", .id = ");
		fxDumpID(file, slot->value.module.id);
		fprintf(file, " }");
	} break;
	case XS_PROMISE_KIND: {
		fprintf(file, "promise = %d }", slot->value.integer);
	} break;
	case XS_PROXY_KIND: {
		fprintf(file, "proxy = { .handler = ");
		fxDumpSlotAddress(file, slot->value.proxy.handler);
		fprintf(file, ", .target = ");
		fxDumpSlotAddress(file, slot->value.proxy.target);
		fprintf(file, " }");
	} break;
	case XS_REGEXP_KIND: {
		fprintf(file, "regexp = { .code = ");
		fxDumpChunkAddress(file, slot->value.regexp.code);
		fprintf(file, ", .data = ");
		fxDumpChunkAddress(file, slot->value.regexp.data);
		fprintf(file, " }");
	} break;
	case XS_SET_KIND: {
		fprintf(file, "set = { .address = ");
		fxDumpChunkAddress(file, slot->value.table.address);
		fprintf(file, ", .length = %d }", (int)slot->value.table.length);
	} break;
	case XS_TYPED_ARRAY_KIND: {
		fprintf(file, ".kind = XS_TYPED_ARRAY_KIND}, ");
		fprintf(file, ".value = { .typedArray = { .dispatch = gxTypeDispatches[%zu], .atomics = gxTypeAtomics[%zu] }", (size_t)slot->value.typedArray.dispatch, (size_t)slot->value.typedArray.atomics);
	} break;
	case XS_WEAK_MAP_KIND: {
		fprintf(file, "weakMap = { .first = ");
		fxDumpSlotAddress(file, slot->value.weakList.first);
		fprintf(file, ", .link = ");
		fxDumpSlotAddress(file, slot->value.weakList.link);
		fprintf(file, " }");
	} break;
	case XS_WEAK_SET_KIND: {
		fprintf(file, "weakSet = { .first = ");
		fxDumpSlotAddress(file, slot->value.weakList.first);
		fprintf(file, ", .link = ");
		fxDumpSlotAddress(file, slot->value.weakList.link);
		fprintf(file, " }");
	} break;
	case XS_WEAK_REF_KIND: {
		fprintf(file, "weakRef = { .target = ");
		fxDumpSlotAddress(file, slot->value.weakRef.target);
		fprintf(file, ", .link = ");
		fxDumpSlotAddress(file, slot->value.weakRef.link);
		fprintf(file, " }");
	} break;
	case XS_ACCESSOR_KIND: {
		fprintf(file, "accessor = { .getter = ");
		fxDumpSlotAddress(file, slot->value.accessor.getter);
		fprintf(file, ", .setter = ");
		fxDumpSlotAddress(file, slot->value.accessor.setter);
		fprintf(file, " }");
	} break;
	case XS_AT_KIND: {
		fprintf(file, "at = { 0x%x, %d }", slot->value.at.index, slot->value.at.id);
	} break;
	case XS_ENTRY_KIND: {
		fprintf(file, "entry = { ");
		fxDumpSlotAddress(file, slot->value.entry.slot);
		fprintf(file, ", 0x%x }", slot->value.entry.sum);
	} break;
	case XS_ERROR_KIND: {
		fprintf(file, "error = ");
		fxDumpSlotAddress(file, slot->value.reference);
		fprintf(file, " }");
	} break;
	case XS_EXPORT_KIND: {
		fprintf(file, "export = { .closure = ");
		fxDumpSlotAddress(file, slot->value.export.closure);
		fprintf(file, ", .module = ");
		fxDumpSlotAddress(file, slot->value.export.module);
		fprintf(file, " }");
	} break;
	case XS_HOME_KIND: {
		fprintf(file, "home = { .object = ");
		fxDumpSlotAddress(file, slot->value.home.object);
		fprintf(file, ", .module = ");
		fxDumpSlotAddress(file, slot->value.home.module);
		fprintf(file, " }");
	} break;
	case XS_KEY_KIND: {
		fprintf(file, "key = { .string = ");
		fxDumpChunkAddress(file, slot->value.key.string);
		fprintf(file, ", .sum = 0x%x }", slot->value.key.sum);
	} break;
	case XS_LIST_KIND: {
		fprintf(file, "list = { .first = ");
		fxDumpSlotAddress(file, slot->value.list.first);
		fprintf(file, ", .last = ");
		fxDumpSlotAddress(file, slot->value.list.last);
		fprintf(file, " }");
	} break;
	case XS_PRIVATE_KIND: {
		fprintf(file, "private = { .check = ");
		fxDumpSlotAddress(file, slot->value.private.check);
		fprintf(file, ", .first = ");
		fxDumpSlotAddress(file, slot->value.private.first);
		fprintf(file, " }");
	} break;
	case XS_STACK_KIND: {
		fprintf(file, "stack");
	} break;
	case XS_WEAK_ENTRY_KIND: {
		fprintf(file, "weakEntry = { .check = ");
		fxDumpSlotAddress(file, slot->value.weakEntry.check);
		fprintf(file, ", .value = ");
		fxDumpSlotAddress(file, slot->value.weakEntry.value);
		fprintf(file, " }");
	} break;
	default:
		break;
	}
}

void fxDumpSlotAddress(FILE* file, void* address) 
{
	if (address)
		fprintf(file, "[%8.8zu]", (size_t)address);
	else
		fprintf(file, "[        ]");
}

void fxDumpSlotTable(FILE* file, txByte* buffer, txSize size)
{
	txSize offset = 0;
	txSlot** address = (txSlot**)buffer;
	size /= sizeof(txSlot*);
	while (offset < size) {
		txSlot* slot = *((txSlot**)address);
		if (offset % 8)
			fprintf(file, " ");
		else
			fprintf(file, "\n\t");
		fxDumpSlotAddress(file, slot);
		offset++;
		address++;
	}
}



