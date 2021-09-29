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
	txJob* job = fxGetHostData(the, mxArgv(0));
	if (job) {
        fxForget(the, &job->self);
        fxSetHostData(the, mxArgv(0), NULL);
		job->the = NULL;
	}
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
