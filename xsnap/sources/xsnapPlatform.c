#include "xsAll.h"
#include "xsScript.h"
#include "xsSnapshot.h"
#include "xsnapPlatform.h"

#if mxMacOSX || mxLinux
  #include <dlfcn.h>
#endif

#ifndef mxReserveChunkSize
	#define mxReserveChunkSize 1024 * 1024 * 1024
#endif


mxExport void fxRunModuleFile(txMachine* the, txString path);
mxExport void fxRunProgramFile(txMachine* the, txString path);
mxExport void fxRunLoop(txMachine* the);

mxExport void fxClearTimer(txMachine* the);
mxExport void fxSetTimer(txMachine* the, txNumber interval, txBoolean repeat);

mxExport void fxVersion(txString theBuffer, txSize theSize);
#ifdef mxMetering
mxExport uint64_t fxGetCurrentMeter(txMachine* the);
mxExport void fxSetCurrentMeter(txMachine* the, uint64_t value);
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

typedef struct sxSharedTimer txSharedTimer;
struct sxSharedTimer {
	txSharedTimer* next;
	txThread thread;
	txNumber when;
	txNumber interval;
	txSharedTimerCallback callback;
	txInteger refconSize;
	char refcon[1];
};

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

static void fxFulfillModuleFile(txMachine* the);
static void fxRejectModuleFile(txMachine* the);
static txScript* fxLoadScript(txMachine* the, txString path, txUnsigned flags);

void fxAbort(txMachine* the, int status)
{
	switch (status) {
	case XS_STACK_OVERFLOW_EXIT:
		fxReport(the, "stack overflow\n");
#ifdef mxDebug
		fxDebugger(the, (char *)__FILE__, __LINE__);
#endif
		the->exitStatus = status;
		fxExitToHost(the);
		break;
	case XS_NOT_ENOUGH_MEMORY_EXIT:
		fxReport(the, "memory full\n");
#ifdef mxDebug
		fxDebugger(the, (char *)__FILE__, __LINE__);
#endif
		the->exitStatus = status;
		fxExitToHost(the);
		break;
	case XS_NO_MORE_KEYS_EXIT:
		fxReport(the, "not enough keys\n");
#ifdef mxDebug
		fxDebugger(the, (char *)__FILE__, __LINE__);
#endif
		the->exitStatus = status;
		fxExitToHost(the);
		break;
	case XS_TOO_MUCH_COMPUTATION_EXIT:
		fxReport(the, "too much computation\n");
#ifdef mxDebug
		fxDebugger(the, (char *)__FILE__, __LINE__);
#endif
		the->exitStatus = status;
		fxExitToHost(the);
		break;
	case XS_UNHANDLED_EXCEPTION_EXIT: {
		mxPush(mxException);
		txSlot *exc = the->stack;
		fxReport(the, "unhandled exception: %s\n", fxToString(the, &mxException));
		mxException = mxUndefined;
		mxTry(the) {
			mxOverflow(-8);
			mxPush(mxGlobal);
			fxGetID(the, fxFindName(the, "console"));
			fxCallID(the, fxFindName(the, "error"));
			mxPushStringC("Unhandled exception:");
			mxPushSlot(exc);
			fxRunCount(the, 2);
			mxPop();
		}
		mxCatch(the) {
			fprintf(stderr, "Unhandled exception %s\n", fxToString(the, exc));
		}
		the->exitStatus = status;
		fxExitToHost(the);
		mxPop();
		break;
	}
	case XS_UNHANDLED_REJECTION_EXIT: {
		mxPush(mxException);
		txSlot *exc = the->stack;
		fxReport(the, "unhandled rejection: %s\n", fxToString(the, &mxException));
		mxException = mxUndefined;
		mxTry(the) {
			mxOverflow(-8);
			mxPush(mxGlobal);
			fxGetID(the, fxFindName(the, "console"));
			fxCallID(the, fxFindName(the, "error"));
			mxPushStringC("UnhandledPromiseRejectionWarning:");
			mxPushSlot(exc);
			fxRunCount(the, 2);
			mxPop();
		}
		mxCatch(the) {
			fprintf(stderr, "Unhandled exception %s\n", fxToString(the, exc));
		}
		break;
	}
	default:
		fxReport(the, "fxAbort(%d) - %s\n", status, fxToString(the, &mxException));
#ifdef mxDebug
		fxDebugger(the, (char *)__FILE__, __LINE__);
#endif
		the->exitStatus = status;
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
	else {
#if mxWindows
		base = result = VirtualAlloc(NULL, mxReserveChunkSize, MEM_RESERVE, PAGE_READWRITE);
#else
		base = result = mmap(NULL, mxReserveChunkSize, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
#endif
    the->growHeapDirection = 1;
  }
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
#ifdef mxInstrument
#else
	the->connection = mxNoSocket;
#endif
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

void fxRunDebugger(txMachine* the)
{
#ifdef mxDebug
	fxDebugCommand(the);
#endif
}

/* SHARED TIMERS */

typedef struct sxSharedTimers txSharedTimers;
struct sxSharedTimers {
	txSharedTimer* first;
	txMutex mutex;
};

void fxInitializeSharedTimers()
{
}

void fxTerminateSharedTimers()
{
}

void fxRescheduleSharedTimer(txSharedTimer* timer, txNumber timeout, txNumber interval)
{
}

void* fxScheduleSharedTimer(txNumber timeout, txNumber interval, txSharedTimerCallback callback, void* refcon, txInteger refconSize)
{
  fprintf(stderr, "xsnap does not support shared timers\n");
  //c_exit(-1);
  return C_NULL;
}

void fxUnscheduleSharedTimer(txSharedTimer* timer)
{
  fprintf(stderr, "xsnap does not support shared timers\n");
  //c_exit(-1);
}

void fxRunLoop(txMachine* the)
{
	c_timeval tv;
	txNumber when;
	txJob* job;
	txJob** address;
	for (;;) {
		fxEndJob(the);
		while (the->promiseJobs) {
			the->promiseJobs = 0;
			fxRunPromiseJobs(the);
		}
		fxEndJob(the);
		if (the->promiseJobs) {
			continue;
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
						fxAccess(the, &job->self);
						*mxResult = the->scratch;
						fxForget(the, &job->self);
						fxSetHostData(the, mxResult, NULL);
						job->the = NULL;
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
	fxCheckUnhandledRejections(the, 1);
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
	mxPushUndefined();
	fxRunImport(the, realm, C_NULL);
	mxDub();
	fxGetID(the, mxID(_then));
	mxCall();
	fxNewHostFunction(the, fxFulfillModuleFile, 1, XS_NO_ID, XS_NO_ID);
	fxNewHostFunction(the, fxRejectModuleFile, 1, XS_NO_ID, XS_NO_ID);
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

void fxRunProgramBuffer(txMachine* the, txString buffer, txSize size)
{
	txSlot* realm = mxProgram.value.reference->next->value.module.realm;
	txStringCStream stream;
	stream.buffer = buffer;
	stream.offset = 0;
	stream.size = size;
	fxRunScript(the, fxParseScript(the, &stream, fxStringCGetter, mxProgramFlag | mxDebugFlag), mxRealmGlobal(realm), C_NULL, mxRealmClosures(realm)->value.reference, C_NULL, mxProgram.value.reference);
}

txID fxFindModule(txMachine* the, txSlot* realm, txID moduleID, txSlot* slot)
{
	char name[C_PATH_MAX];
	char path[C_PATH_MAX];
	txInteger dot = 0;
	txString slash;
	fxToStringBuffer(the, slot, name, sizeof(name));
	if (name[0] == '.') {
		if (name[1] == '/') {
			dot = 1;
		}
		else if ((name[1] == '.') && (name[2] == '/')) {
			dot = 2;
		}
	}
	if (dot) {
		if (moduleID == XS_NO_ID)
			return XS_NO_ID;
		c_strncpy(path, fxGetKeyName(the, moduleID), C_PATH_MAX - 1);
		path[C_PATH_MAX - 1] = 0;
		slash = c_strrchr(path, mxSeparator);
		if (!slash)
			return XS_NO_ID;
		if (dot == 2) {
			*slash = 0;
			slash = c_strrchr(path, mxSeparator);
			if (!slash)
				return XS_NO_ID;
		}
#if mxWindows
		{
			char c;
			char* s = name;
			while ((c = *s)) {
				if (c == '/')
					*s = '\\';
				s++;
			}
		}
#endif
	}
	else
		slash = path;
	*slash = 0;
	if ((c_strlen(path) + c_strlen(name + dot)) >= sizeof(path))
		mxRangeError("path too long");
	c_strcat(path, name + dot);
	return fxNewNameC(the, path);
}

void fxLoadModule(txMachine* the, txSlot* module, txID moduleID)
{
	char path[C_PATH_MAX];
	char real[C_PATH_MAX];
	txString dot;
	txScript* script;
#ifdef mxDebug
	txUnsigned flags = mxDebugFlag;
#else
	txUnsigned flags = 0;
#endif
	c_strncpy(path, fxGetKeyName(the, moduleID), C_PATH_MAX - 1);
	path[C_PATH_MAX - 1] = 0;
	if (c_realpath(path, real)) {
		#if mxWindows
			DWORD attributes;
			attributes = GetFileAttributes(path);
			if (attributes != 0xFFFFFFFF) {
				if (attributes & FILE_ATTRIBUTE_DIRECTORY)
					return;
			}
		#else
			struct stat a_stat;
			if (stat(path, &a_stat) == 0) {
				if (S_ISDIR(a_stat.st_mode))
					return;
			}
		#endif
		dot = c_strrchr(real, '.');
		if (dot && !c_strcmp(dot, ".json"))
			flags |= mxJSONModuleFlag;
		script = fxLoadScript(the, real, flags);
		if (script)
			fxResolveModule(the, module, moduleID, script, C_NULL, C_NULL);
	}
}

txScript* fxLoadScript(txMachine* the, txString path, txUnsigned flags)
{
	txParser _parser;
	txParser* parser = &_parser;
	txParserJump jump;
	FILE* file = NULL;
	txString name = NULL;
	char map[C_PATH_MAX];
	txScript* script = NULL;
	fxInitializeParser(parser, the, the->parserBufferSize, the->parserTableModulo);
	parser->firstJump = &jump;
	file = fopen(path, "r");
	if (c_setjmp(jump.jmp_buf) == 0) {
		mxParserThrowElse(file);
		parser->path = fxNewParserSymbol(parser, path);
		fxParserTree(parser, file, (txGetter)fgetc, flags, &name);
		fclose(file);
		file = NULL;
		if (name) {
			mxParserThrowElse(c_realpath(fxCombinePath(parser, path, name), map));
			parser->path = fxNewParserSymbol(parser, map);
			file = fopen(map, "r");
			mxParserThrowElse(file);
			fxParserSourceMap(parser, file, (txGetter)fgetc, flags, &name);
			fclose(file);
			file = NULL;
			if ((parser->errorCount == 0) && name) {
				mxParserThrowElse(c_realpath(fxCombinePath(parser, map, name), map));
				parser->path = fxNewParserSymbol(parser, map);
			}
		}
		fxParserHoist(parser);
		fxParserBind(parser);
		script = fxParserCode(parser);
	}
	if (file)
		fclose(file);
#ifdef mxInstrument
	if (the->peakParserSize < parser->total)
		the->peakParserSize = parser->total;
#endif
	fxTerminateParser(parser);
	return script;
}

/* DEBUG */

#ifdef mxDebug

#ifdef mxInstrument

void fxConnect(txMachine* the)
{
}

void fxDisconnect(txMachine* the)
{
}

txBoolean fxIsConnected(txMachine* the)
{
	return 1;
}

txBoolean fxIsReadable(txMachine* the)
{
	return 0;
}

void fxReceive(txMachine* the)
{
	ssize_t count;
again:
	count = read(5, the->debugBuffer, sizeof(the->debugBuffer) - 1);
	if (count < 0) {
		if (errno == EINTR)
			goto again;
		the->debugOffset = 0;
	}
	else
		the->debugOffset = count;
	the->debugBuffer[the->debugOffset] = 0;
}

void fxSend(txMachine* the, txBoolean more)
{
	ssize_t count;
again:
	count = write(6, the->echoBuffer, the->echoOffset);
	if (count < 0) {
		if (errno == EINTR)
			goto again;
	}
}

#else

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
		// Require XSBUG_HOST to be set for debugging.
		return;
		// strcpy(name, "localhost");
		// port = 5002;
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
	int flag;
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
		the->connection = mxNoSocket;
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

#endif /* mxInstrument */

#endif /* mxDebug */

void fxVersion(txString theBuffer, txSize theSize)
{
	c_snprintf(theBuffer, theSize, "%d.%d.%d", XS_MAJOR_VERSION, XS_MINOR_VERSION, XS_PATCH_VERSION);
}

#ifdef mxMetering
uint64_t fxGetCurrentMeter(txMachine* the)
{
	return the->meterIndex >> 16;
}

void fxSetCurrentMeter(txMachine* the, uint64_t value)
{
	the->meterIndex = value << 16;
}
#endif

txSize fxGetCurrentHeapCount(txMachine* the)
{
	return the->currentHeapCount;
}


extern txCallback fxUnprojectCallback(txMachine* the, txSnapshot* snapshot, txCallback callback);

extern void fxDumpSnapshot(txMachine* the, txSnapshot* snapshot);

#define mxThrowIf(_ERROR) { if (_ERROR) { snapshot->error = _ERROR; fxJump(the); } }

typedef struct sxDumper txDumper;
struct sxDumper {
	txMachine* machine;
	txSnapshot* snapshot;
	FILE* file;
};

typedef void (*txDumpChunk)(txDumper* dumper, txByte* data, txSize size);

static void fxDumpChunk(txSlot* slot, txByte* block);
static void fxDumpChunkAddress(txDumper* dumper, void* address);
static void fxDumpChunkArray(txDumper* dumper, txByte* data, txSize size);
static void fxDumpChunkCode(txDumper* dumper, txByte* data, txSize size);
static void fxDumpChunkData(txDumper* dumper, txByte* data, txSize size);
static void fxDumpChunkString(txDumper* dumper, txByte* data, txSize size);
static void fxDumpChunkTable(txDumper* dumper, txByte* data, txSize size); 
static void fxDumpID(txDumper* dumper, txID id);
static void fxDumpNumber(txDumper* dumper, txNumber value);
static void fxDumpSlot(txDumper* dumper, txSlot* slot);
static void fxDumpSlotAddress(txDumper* dumper, void* address);
static void fxDumpSlotTable(txDumper* dumper, txByte* buffer, txSize size);

void fxDumpSnapshot(txMachine* the, txSnapshot* snapshot)
{
	txDumper _dumper;
	txDumper* dumper = &_dumper;
	Atom atom;
	txByte byte;
	txCreation creation;
	txID profileID;
	txInteger tag;
	Atom blockAtom;
	txByte* block = C_NULL;
// 	txByte* blockLimit;
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
		dumper->machine = the;
		dumper->snapshot = snapshot;
		dumper->file = stderr;
	
		mxThrowIf((*snapshot->read)(snapshot->stream, &atom, sizeof(Atom)));
		atom.atomSize = ntohl(atom.atomSize) - 8;
		fprintf(dumper->file, "%4.4s %d\n", (txString)&(atom.atomType), atom.atomSize + 8);
		
		mxThrowIf((*snapshot->read)(snapshot->stream, &atom, sizeof(Atom)));
		atom.atomSize = ntohl(atom.atomSize) - 8;
		fprintf(dumper->file, "%4.4s %d\n", (txString)&(atom.atomType), atom.atomSize + 8);
		mxThrowIf((*snapshot->read)(snapshot->stream, &byte, 1));
		fprintf(dumper->file, "\t%d.", byte);
		mxThrowIf((*snapshot->read)(snapshot->stream, &byte, 1));
		fprintf(dumper->file, "%d.", byte);
		mxThrowIf((*snapshot->read)(snapshot->stream, &byte, 1));
		fprintf(dumper->file, "%d ", byte);
		mxThrowIf((*snapshot->read)(snapshot->stream, &byte, 1));
		fprintf(dumper->file, "(%d)\n", byte);
		
		mxThrowIf((*snapshot->read)(snapshot->stream, &atom, sizeof(Atom)));
		atom.atomSize = ntohl(atom.atomSize) - 8;
		buffer = c_malloc(atom.atomSize);
		mxThrowIf(buffer == C_NULL);
		mxThrowIf((*snapshot->read)(snapshot->stream, buffer, atom.atomSize));
		fprintf(dumper->file, "%4.4s %d\n", (txString)&(atom.atomType), atom.atomSize + 8);
		fprintf(dumper->file, "\t%s\n", (txString)buffer);
		c_free(buffer);
		buffer = C_NULL;
	
		mxThrowIf((*snapshot->read)(snapshot->stream, &atom, sizeof(Atom)));
		atom.atomSize = ntohl(atom.atomSize) - 8;
		mxThrowIf((*snapshot->read)(snapshot->stream, &creation, sizeof(txCreation)));
		mxThrowIf((*snapshot->read)(snapshot->stream, &profileID, sizeof(txID)));
		mxThrowIf((*snapshot->read)(snapshot->stream, &tag, sizeof(txInteger)));
		fprintf(dumper->file, "%4.4s %d\n", (txString)&(atom.atomType), atom.atomSize + 8);
		fprintf(dumper->file, "\tinitialChunkSize: %d\n", creation.initialChunkSize);
		fprintf(dumper->file, "\tincrementalChunkSize: %d\n", creation.incrementalChunkSize);
		fprintf(dumper->file, "\tinitialHeapCount: %d\n", creation.initialHeapCount);
		fprintf(dumper->file, "\tincrementalHeapCount: %d\n", creation.incrementalHeapCount);
		fprintf(dumper->file, "\tstackCount: %d\n", creation.stackCount);
		fprintf(dumper->file, "\tinitialKeyCount: %d\n", creation.initialKeyCount);
		fprintf(dumper->file, "\tincrementalKeyCount: %d\n", creation.incrementalKeyCount);
		fprintf(dumper->file, "\tnameModulo: %d\n", creation.nameModulo);
		fprintf(dumper->file, "\tsymbolModulo: %d\n", creation.symbolModulo);
		fprintf(dumper->file, "\tparserBufferSize: %d\n", creation.parserBufferSize);
		fprintf(dumper->file, "\tparserTableModulo: %d\n", creation.parserTableModulo);
		fprintf(dumper->file, "\tstaticSize: %d\n", creation.staticSize);
		fprintf(dumper->file, "\tprofileID: %d\n", profileID);
		fprintf(dumper->file, "\ttag: %d\n", tag);

		mxThrowIf((*snapshot->read)(snapshot->stream, &blockAtom, sizeof(Atom)));
		blockAtom.atomSize = ntohl(blockAtom.atomSize) - 8;
		block = c_malloc(blockAtom.atomSize);
		mxThrowIf(block == C_NULL);
		mxThrowIf((*snapshot->read)(snapshot->stream, block, blockAtom.atomSize));
//		blockLimit = block + blockAtom.atomSize;

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

		fprintf(dumper->file, "%4.4s %d\n", (txString)&(blockAtom.atomType), blockAtom.atomSize + 8);
		address = block;
		offset = 0;
		while (offset < blockAtom.atomSize) {
			txChunk* chunk = (txChunk*)address;
			fprintf(dumper->file, "\t<%8.8lu> %8d ", offset + sizeof(txChunk), chunk->size);
			if (chunk->temporary)
				(*(txDumpChunk)(chunk->temporary))(dumper, address + sizeof(txChunk), chunk->size - sizeof(txChunk));
			else
				fxDumpChunkData(dumper, address + sizeof(txChunk), chunk->size - sizeof(txChunk));
// 			fxDumpChunkData(dumper, address + sizeof(txChunk), chunk->size - sizeof(txChunk));
			fprintf(dumper->file, "\n");
			address += chunk->size;
			offset += chunk->size;
		}
		
		fprintf(dumper->file, "%4.4s %d\n", (txString)&(heapAtom.atomType), heapAtom.atomSize + 8);
		current = heap;
		offset = 0;
		while (current < heapLimit) {
			fprintf(dumper->file, "\t[%8.8d] ", offset);
			fxDumpSlotAddress(dumper, current->next);
			fprintf(dumper->file, " ");
			fxDumpSlot(dumper, current);
			fprintf(dumper->file, "\n");
			current++;
			offset++;
		}
		
		fprintf(dumper->file, "%4.4s %d\n", (txString)&(stackAtom.atomType), stackAtom.atomSize + 8);
		current = stack;
		while (current < stackLimit) {
			fprintf(dumper->file, "\t           ");
			fxDumpSlotAddress(dumper, current->next);
			fprintf(dumper->file, " ");
			fxDumpSlot(dumper, current);
			fprintf(dumper->file, "\n");
			current++;
		}

		mxThrowIf((*snapshot->read)(snapshot->stream, &atom, sizeof(Atom)));
		atom.atomSize = ntohl(atom.atomSize) - 8;
		buffer = c_malloc(atom.atomSize);
		mxThrowIf(buffer == C_NULL);
		mxThrowIf((*snapshot->read)(snapshot->stream, buffer, atom.atomSize));
		fprintf(dumper->file, "%4.4s %d\n", (txString)&(atom.atomType), atom.atomSize + 8);
		address = buffer;
		offset = 0;
		size = atom.atomSize / sizeof(txSlot*);
		while (offset < size) {
			txSlot* slot = *((txSlot**)address);
			fprintf(dumper->file, "\tID_%6.6d", offset);
			if (slot) {
				fprintf(dumper->file, " [%8.8zu]", (size_t)slot);
				slot = ((txSlot*)heap) + (size_t)slot;
				if (slot->kind == XS_KEY_KIND) {
					string = ((txString)block) + (size_t)(slot->value.key.string);
					fprintf(dumper->file, " %s\n", string);
				}
				else if (slot->kind == XS_REFERENCE_KIND) {
					fprintf(dumper->file, " // symbol\n");
				} 
				else {
					fprintf(dumper->file, " // hole\n");
				}
			}
			else
				fprintf(dumper->file, " [        ]\n");
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
		fprintf(dumper->file, "%4.4s %d", (txString)&(atom.atomType), atom.atomSize + 8);
		fxDumpSlotTable(dumper, buffer, atom.atomSize);
		fprintf(dumper->file, "\n");
		c_free(buffer);
		buffer = C_NULL;

		mxThrowIf((*snapshot->read)(snapshot->stream, &atom, sizeof(Atom)));
		atom.atomSize = ntohl(atom.atomSize) - 8;
		buffer = c_malloc(atom.atomSize);
		mxThrowIf(buffer == C_NULL);
		mxThrowIf((*snapshot->read)(snapshot->stream, buffer, atom.atomSize));
		fprintf(dumper->file, "%4.4s %d", (txString)&(atom.atomType), atom.atomSize + 8);
		fxDumpSlotTable(dumper, buffer, atom.atomSize);
		fprintf(dumper->file, "\n");
		c_free(buffer);
		buffer = C_NULL;
		
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

#define mxDumpChunkTemporary(chunk, dump) \
	chunk->temporary = (txByte*)dump

void fxDumpChunk(txSlot* slot, txByte* block) 
{
	txChunk* chunk;
	switch (slot->kind) {
	case XS_STRING_KIND: {
		chunk = (txChunk*)(block + (size_t)(slot->value.string) - sizeof(txChunk));
		mxDumpChunkTemporary(chunk, fxDumpChunkString);
	} break;
	case XS_BIGINT_KIND: {
		chunk = (txChunk*)(block + (size_t)(slot->value.bigint.data) - sizeof(txChunk));
		mxDumpChunkTemporary(chunk, fxDumpChunkData);
	} break;
	case XS_ARGUMENTS_SLOPPY_KIND:
	case XS_ARGUMENTS_STRICT_KIND:
	case XS_ARRAY_KIND:
	case XS_STACK_KIND: {
		if (slot->value.array.address) {
			chunk = (txChunk*)(block + (size_t)(slot->value.array.address) - sizeof(txChunk));
			mxDumpChunkTemporary(chunk, fxDumpChunkArray);
			
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
			mxDumpChunkTemporary(chunk, fxDumpChunkData);
		}
	} break;
	case XS_CODE_KIND:  {
		chunk = (txChunk*)(block + (size_t)(slot->value.code.address) - sizeof(txChunk));
		mxDumpChunkTemporary(chunk, fxDumpChunkCode);
	} break;
	case XS_REGEXP_KIND: {
		if (slot->value.regexp.code) {
			chunk = (txChunk*)(block + (size_t)(slot->value.regexp.code) - sizeof(txChunk));
			mxDumpChunkTemporary(chunk, fxDumpChunkData);
		}
		if (slot->value.regexp.data) {
			chunk = (txChunk*)(block + (size_t)(slot->value.regexp.data) - sizeof(txChunk));
			mxDumpChunkTemporary(chunk, fxDumpChunkData);
		}
	} break;
	case XS_KEY_KIND: {
		if (slot->value.key.string) {
			chunk = (txChunk*)(block + (size_t)(slot->value.key.string) - sizeof(txChunk));
			mxDumpChunkTemporary(chunk, fxDumpChunkString);
		}
	} break;
	case XS_GLOBAL_KIND:
	case XS_MAP_KIND:
	case XS_SET_KIND: {
		chunk = (txChunk*)(block + (size_t)(slot->value.table.address) - sizeof(txChunk));
		mxDumpChunkTemporary(chunk, fxDumpChunkTable);
	} break;
	case XS_HOST_KIND: {
		if (slot->value.host.data) {
			chunk = (txChunk*)(block + (size_t)(slot->value.host.data) - sizeof(txChunk));
			mxDumpChunkTemporary(chunk, fxDumpChunkData);
		}
	} break;
	default:
		break;
	}
}

void fxDumpChunkAddress(txDumper* dumper, void* address) 
{
	if (address)
		fprintf(dumper->file, "<%8.8zu>", (size_t)address);
	else
		fprintf(dumper->file, "<        >");
}

void fxDumpChunkArray(txDumper* dumper, txByte* data, txSize size) 
{
	txSize offset = 0;
	txSlot* slot = (txSlot*)data;
	size /= sizeof(txSlot);
	while (offset < size) {
		fprintf(dumper->file, "\n\t\t%8zu ", (size_t)slot->next);
		fxDumpSlot(dumper, slot);
		offset++;
		slot++;
	}
}

void fxDumpChunkCode(txDumper* dumper, txByte* data, txSize size) 
{
	txByte* p = data;
	txByte* q = p + size;
	txByte* r;
	txU1 code;
	txSize offset = 0;
	txID id;
	while (p < q) {
		code = *((txU1*)p);
		fprintf(dumper->file, "\n\t\t\t%8.8zu %s ", p - data, gxCodeNames[code]);
		offset = (txSize)gxCodeSizes[code];
		p++;
		if (0 == offset) {
			mxDecodeID(p, id);
			fxDumpID(dumper, id);
		}
		else if (1 == offset) {
		}
		else if (2 == offset) {
			switch(code) {
			case XS_CODE_ARGUMENT:
			case XS_CODE_ARGUMENTS:
			case XS_CODE_ARGUMENTS_SLOPPY:
			case XS_CODE_ARGUMENTS_STRICT:
			case XS_CODE_BEGIN_SLOPPY:
			case XS_CODE_BEGIN_STRICT:
			case XS_CODE_BEGIN_STRICT_BASE:
			case XS_CODE_BEGIN_STRICT_DERIVED:
			case XS_CODE_BEGIN_STRICT_FIELD:
			case XS_CODE_MODULE:

			case XS_CODE_CONST_CLOSURE_1:
			case XS_CODE_CONST_LOCAL_1:
			case XS_CODE_GET_CLOSURE_1:
			case XS_CODE_GET_LOCAL_1:
			case XS_CODE_GET_PRIVATE_1:
			case XS_CODE_HAS_PRIVATE_1:
			case XS_CODE_LET_CLOSURE_1:
			case XS_CODE_LET_LOCAL_1:
			case XS_CODE_NEW_PRIVATE_1:
			case XS_CODE_PULL_CLOSURE_1:
			case XS_CODE_PULL_LOCAL_1:
			case XS_CODE_REFRESH_CLOSURE_1:
			case XS_CODE_REFRESH_LOCAL_1:
			case XS_CODE_RESERVE_1:
			case XS_CODE_RESET_CLOSURE_1:
			case XS_CODE_RESET_LOCAL_1:
			case XS_CODE_RETRIEVE_1:
			case XS_CODE_SET_CLOSURE_1:
			case XS_CODE_SET_LOCAL_1:
			case XS_CODE_SET_PRIVATE_1:
			case XS_CODE_STORE_1:
			case XS_CODE_UNWIND_1:
#if mxExplicitResourceManagement
			case XS_CODE_USED_1:
#endif
			case XS_CODE_VAR_CLOSURE_1:
			case XS_CODE_VAR_LOCAL_1: {
				txU1 value = *((txU1*)p++);
				fprintf(dumper->file, "%d", value);
				} break;
			case XS_CODE_BRANCH_1:
			case XS_CODE_BRANCH_CHAIN_1:
			case XS_CODE_BRANCH_COALESCE_1:
			case XS_CODE_BRANCH_ELSE_1:
			case XS_CODE_BRANCH_IF_1:
			case XS_CODE_BRANCH_STATUS_1:
			case XS_CODE_CATCH_1:
			case XS_CODE_CODE_1:
			case XS_CODE_CODE_ARCHIVE_1:
			case XS_CODE_INTEGER_1:
			case XS_CODE_RUN_1:
			case XS_CODE_RUN_TAIL_1: {
				txS1 value = *((txS1*)p++);
				fprintf(dumper->file, "%d", value);
				} break;
			default:
				fprintf(dumper->file, "OOPS 1");
				p++;
				break;
			}
		}
		else if (3 == offset) {
			switch(code) {
			case XS_CODE_CONST_LOCAL_2:
			case XS_CODE_GET_CLOSURE_2:
			case XS_CODE_GET_LOCAL_2:
			case XS_CODE_GET_PRIVATE_2:
			case XS_CODE_HAS_PRIVATE_2:
			case XS_CODE_LET_CLOSURE_2:
			case XS_CODE_LET_LOCAL_2:
			case XS_CODE_NEW_PRIVATE_2:
			case XS_CODE_PULL_CLOSURE_2:
			case XS_CODE_PULL_LOCAL_2:
			case XS_CODE_REFRESH_CLOSURE_2:
			case XS_CODE_REFRESH_LOCAL_2:
			case XS_CODE_RESERVE_2:
			case XS_CODE_RESET_CLOSURE_2:
			case XS_CODE_RESET_LOCAL_2:
			case XS_CODE_RETRIEVE_2:
			case XS_CODE_SET_CLOSURE_2:
			case XS_CODE_SET_LOCAL_2:
			case XS_CODE_SET_PRIVATE_2:
			case XS_CODE_STORE_2:
			case XS_CODE_UNWIND_2:
#if mxExplicitResourceManagement
			case XS_CODE_USED_2:
#endif
			case XS_CODE_VAR_CLOSURE_2:
			case XS_CODE_VAR_LOCAL_2: {
				txU2 value;
				mxDecode2(p, value);
				fprintf(dumper->file, "%d", value);
				} break;
			case XS_CODE_LINE:
			case XS_CODE_BRANCH_2:
			case XS_CODE_BRANCH_CHAIN_2:
			case XS_CODE_BRANCH_COALESCE_2:
			case XS_CODE_BRANCH_ELSE_2:
			case XS_CODE_BRANCH_IF_2:
			case XS_CODE_BRANCH_STATUS_2:
			case XS_CODE_CATCH_2:
			case XS_CODE_CODE_2:
			case XS_CODE_CODE_ARCHIVE_2:
			case XS_CODE_INTEGER_2:
			case XS_CODE_RUN_2:
			case XS_CODE_RUN_TAIL_2:{
				txS2 value;
				mxDecode2(p, value);
				fprintf(dumper->file, "%d", value);
				} break;
			default:
				fprintf(dumper->file, "OOPS 2");
				p += 2;
				break;
			}
		}
		else if (5 == offset) {
			switch(code) {
			case XS_CODE_PROFILE:
			case XS_CODE_BRANCH_4:
			case XS_CODE_BRANCH_CHAIN_4:
			case XS_CODE_BRANCH_COALESCE_4:
			case XS_CODE_BRANCH_ELSE_4:
			case XS_CODE_BRANCH_IF_4:
			case XS_CODE_BRANCH_STATUS_4:
			case XS_CODE_CATCH_4:
			case XS_CODE_CODE_4:
			case XS_CODE_CODE_ARCHIVE_4:
			case XS_CODE_INTEGER_4:
			case XS_CODE_RUN_4:
			case XS_CODE_RUN_TAIL_4:{
				txS4 value;
				mxDecode4(p, value);
				fprintf(dumper->file, "%d", value);
				} break;
			default:
				fprintf(dumper->file, "OOPS 4");
				p += 2;
				break;
			}
		}
		else if (9 == offset) {
			txNumber value;
			mxDecode8(p, value);
			fprintf(dumper->file, "%f", value);
		}
		else {
			if (-1 == offset) {
				txU1 value = *((txU1*)p++);
				r = p + value;
			}
			else if (-2 == offset) {
				txU2 value;
				mxDecode2(p, value);
				r = p + value;
			}
			else if (-4 == offset) {
				txU4 value;
				mxDecode4(p, value);
				r = p + value;
			}
			if ((code == XS_CODE_BIGINT_1) || (code == XS_CODE_BIGINT_2)) {
				while (p < r) {
					code = *((txU1*)p);
					fprintf(dumper->file, "%2.2x", code);
					p++;
				}
			}
			else {
				 fprintf(dumper->file, "%s", p);
				 p = r;
			}
		}
	}
}

void fxDumpChunkData(txDumper* dumper, txByte* data, txSize size) 
{
	txSize offset = 0;
	txU1* address = (txU1*)data;
	while (offset < size) {
		if (offset % 32)
			fprintf(dumper->file, " ");
		else
			fprintf(dumper->file, "\n\t\t");
		fprintf(dumper->file, "%2.2x", address[offset]);
		offset++;
	}
}

void fxDumpChunkString(txDumper* dumper, txByte* data, txSize size) 
{
	fprintf(dumper->file, " %s", data);
}

void fxDumpChunkTable(txDumper* dumper, txByte* data, txSize size) 
{
	txSize offset = 0;
	txSlot** address = (txSlot**)data;
	size /= sizeof(txSlot*);
	while (offset < size) {
		txSlot* slot = *((txSlot**)address);
		if (offset % 8)
			fprintf(dumper->file, " ");
		else
			fprintf(dumper->file, "\n\t\t");
		fxDumpSlotAddress(dumper, slot);
		offset++;
		address++;
	}
}

void fxDumpID(txDumper* dumper, txID id)
{
	if (id == 0)
		fprintf(dumper->file, "         ");
	else
		fprintf(dumper->file, "ID_%6.6d", id);
}

void fxDumpNumber(txDumper* dumper, txNumber value) 
{
	switch (c_fpclassify(value)) {
	case C_FP_INFINITE:
		if (value < 0)
			fprintf(dumper->file, "-C_INFINITY");
		else
			fprintf(dumper->file, "C_INFINITY");
		break;
	case C_FP_NAN:
		fprintf(dumper->file, "C_NAN");
		break;
	default:
		fprintf(dumper->file, "%.20e", value);
		break;
	}
}


void fxDumpSlot(txDumper* dumper, txSlot* slot)
{
	if (slot->flag & XS_MARK_FLAG)
		fprintf(dumper->file, "M");
	else
		fprintf(dumper->file, "_");
	if (slot->kind == XS_INSTANCE_KIND) {
		if (slot->flag & XS_DONT_MARSHALL_FLAG)
			fprintf(dumper->file, "H");
		else
			fprintf(dumper->file, "_");
		if (slot->flag & XS_LEVEL_FLAG)
			fprintf(dumper->file, "L");
		else
			fprintf(dumper->file, "_");
		if (slot->flag & XS_DONT_PATCH_FLAG)
			fprintf(dumper->file, "P");
		else
			fprintf(dumper->file, "_");
		if (slot->flag & XS_FIELD_FLAG)
			fprintf(dumper->file, "F");
		else
			fprintf(dumper->file, "_");
		if (slot->flag & XS_CAN_CONSTRUCT_FLAG)
			fprintf(dumper->file, "N");
		else
			fprintf(dumper->file, "_");
		if (slot->flag & XS_CAN_CALL_FLAG)
			fprintf(dumper->file, "C");
		else
			fprintf(dumper->file, "_");
		if (slot->flag & XS_EXOTIC_FLAG)
			fprintf(dumper->file, "X");
		else
			fprintf(dumper->file, "_");
	}
	else {
		if (slot->flag & XS_DERIVED_FLAG)
			fprintf(dumper->file, "H");
		else
			fprintf(dumper->file, "_");
		if (slot->flag & XS_BASE_FLAG)
			fprintf(dumper->file, "B");
		else
			fprintf(dumper->file, "_");
		if (slot->flag & XS_INSPECTOR_FLAG)
			fprintf(dumper->file, "L");
		else
			fprintf(dumper->file, "_");
		if (slot->flag & XS_DONT_SET_FLAG)
			fprintf(dumper->file, "S");
		else
			fprintf(dumper->file, "_");
		if (slot->flag & XS_DONT_ENUM_FLAG)
			fprintf(dumper->file, "E");
		else
			fprintf(dumper->file, "_");
		if (slot->flag & XS_DONT_DELETE_FLAG)
			fprintf(dumper->file, "D");
		else
			fprintf(dumper->file, "_");
		if (slot->flag & XS_INTERNAL_FLAG)
			fprintf(dumper->file, "I");
		else
			fprintf(dumper->file, "_");
	
	}
	fprintf(dumper->file, " ");
	fxDumpID(dumper, slot->ID);
	fprintf(dumper->file, " ");
	switch (slot->kind) {
	case XS_UNINITIALIZED_KIND: {
		fprintf(dumper->file, "unititialized");
	} break;
	case XS_UNDEFINED_KIND: {
		fprintf(dumper->file, "undefined");
	} break;
	case XS_NULL_KIND: {
		fprintf(dumper->file, "null");
	} break;
	case XS_BOOLEAN_KIND: {
		fprintf(dumper->file, "boolean = %d", slot->value.boolean);
	} break;
	case XS_INTEGER_KIND: {
		fprintf(dumper->file, "integer = %d", slot->value.integer);
	} break;
	case XS_NUMBER_KIND: {
		fprintf(dumper->file, "number = ");
		fxDumpNumber(dumper, slot->value.number);
	} break;
	case XS_STRING_KIND: {
		fprintf(dumper->file, "string = ");
		fxDumpChunkAddress(dumper, slot->value.string);
	} break;
	case XS_SYMBOL_KIND: {
		fprintf(dumper->file, "symbol = ");
		fxDumpID(dumper, slot->value.symbol);
	} break;
	case XS_BIGINT_KIND: {
		fprintf(dumper->file, "bigint = { .data = ");
		fxDumpChunkAddress(dumper, slot->value.bigint.data);
		fprintf(dumper->file, ", .size = %d, ", slot->value.bigint.size);
		fprintf(dumper->file, ".sign = %d, ", slot->value.bigint.sign);
		fprintf(dumper->file, " }");
	} break;
	case XS_REFERENCE_KIND: {
		fprintf(dumper->file, "reference = ");
		fxDumpSlotAddress(dumper, slot->value.reference);
	} break;
	case XS_CLOSURE_KIND: {
		fprintf(dumper->file, "closure = ");
		fxDumpSlotAddress(dumper, slot->value.closure);
	} break; 
	case XS_INSTANCE_KIND: {
		fprintf(dumper->file, "instance = { .garbage = ");
		fxDumpSlotAddress(dumper, slot->value.instance.garbage);
		fprintf(dumper->file, ", .prototype = ");
		fxDumpSlotAddress(dumper, slot->value.instance.prototype);
		fprintf(dumper->file, " }");
	} break;
	case XS_ARGUMENTS_SLOPPY_KIND:
	case XS_ARGUMENTS_STRICT_KIND:
	case XS_ARRAY_KIND: {
		fprintf(dumper->file, "array = { .address = ");
		fxDumpChunkAddress(dumper, slot->value.array.address);
		fprintf(dumper->file, ", .length = %d }", (int)slot->value.array.length);
	} break;
	case XS_ARRAY_BUFFER_KIND: {
		fprintf(dumper->file, "arrayBuffer = { .address = ");
		fxDumpChunkAddress(dumper, slot->value.arrayBuffer.address);
		fprintf(dumper->file, " }");
	} break;
	case XS_BUFFER_INFO_KIND: {
		fprintf(dumper->file, "bufferInfo = { .length = %d, maxLength = %d }", slot->value.bufferInfo.length, slot->value.bufferInfo.maxLength);
	} break;
	case XS_CALLBACK_KIND: {
#if mxMacOSX
		txCallback callback = fxUnprojectCallback(dumper->machine, dumper->snapshot, slot->value.callback.address);
		Dl_info info;
		if (dladdr(callback, &info) && info.dli_sname)
			fprintf(dumper->file, "callback = %s",info.dli_sname);
		else
#endif
			fprintf(dumper->file, "callback = ?");
	} break;
	case XS_CODE_KIND:  {
		fprintf(dumper->file, "code = { .address = ");
		fxDumpChunkAddress(dumper, slot->value.code.address);
		fprintf(dumper->file, ", .closures = ");
		fxDumpSlotAddress(dumper, slot->value.code.closures);
		fprintf(dumper->file, " }");
	} break;
	case XS_DATE_KIND: {
		fprintf(dumper->file, "date = ");
		fxDumpNumber(dumper, slot->value.number);
	} break;
	case XS_DATA_VIEW_KIND: {
		fprintf(dumper->file, "dataView = { .offset = %d, .size = %d }", slot->value.dataView.offset, slot->value.dataView.size);
	} break;
	case XS_FINALIZATION_CELL_KIND: {
		fprintf(dumper->file, "finalizationCell = { .target = ");
		fxDumpSlotAddress(dumper, slot->value.finalizationCell.target);
		fprintf(dumper->file, ", .token = ");
		fxDumpSlotAddress(dumper, slot->value.finalizationCell.token);
		fprintf(dumper->file, " }");
	} break;
	case XS_FINALIZATION_REGISTRY_KIND: {
		fprintf(dumper->file, "finalizationRegistry = { .target = ");
		fxDumpSlotAddress(dumper, slot->value.finalizationRegistry.callback);
		fprintf(dumper->file, ", .flags = %d }", slot->value.finalizationRegistry.flags);
	} break;
	case XS_GLOBAL_KIND: {
		fprintf(dumper->file, "global = { .address = ");
		fxDumpChunkAddress(dumper, slot->value.table.address);
		fprintf(dumper->file, ", .length = %d }", (int)slot->value.table.length);
	} break;
	case XS_MAP_KIND: {
		fprintf(dumper->file, "map = { .address = ");
		fxDumpChunkAddress(dumper, slot->value.table.address);
		fprintf(dumper->file, ", .length = %d }", (int)slot->value.table.length);
	} break;
	case XS_MODULE_KIND: {
		fprintf(dumper->file, "module = { .realm = ");
		fxDumpSlotAddress(dumper, slot->value.module.realm);
		fprintf(dumper->file, ", .id = ");
		fxDumpID(dumper, slot->value.module.id);
		fprintf(dumper->file, " }");
	} break;
	case XS_PROGRAM_KIND: {
		fprintf(dumper->file, "program = { .realm = ");
		fxDumpSlotAddress(dumper, slot->value.module.realm);
		fprintf(dumper->file, ", .id = ");
		fxDumpID(dumper, slot->value.module.id);
		fprintf(dumper->file, " }");
	} break;
	case XS_PROMISE_KIND: {
		fprintf(dumper->file, "promise = %d }", slot->value.integer);
	} break;
	case XS_PROXY_KIND: {
		fprintf(dumper->file, "proxy = { .handler = ");
		fxDumpSlotAddress(dumper, slot->value.proxy.handler);
		fprintf(dumper->file, ", .target = ");
		fxDumpSlotAddress(dumper, slot->value.proxy.target);
		fprintf(dumper->file, " }");
	} break;
	case XS_REGEXP_KIND: {
		fprintf(dumper->file, "regexp = { .code = ");
		fxDumpChunkAddress(dumper, slot->value.regexp.code);
		fprintf(dumper->file, ", .data = ");
		fxDumpChunkAddress(dumper, slot->value.regexp.data);
		fprintf(dumper->file, " }");
	} break;
	case XS_SET_KIND: {
		fprintf(dumper->file, "set = { .address = ");
		fxDumpChunkAddress(dumper, slot->value.table.address);
		fprintf(dumper->file, ", .length = %d }", (int)slot->value.table.length);
	} break;
	case XS_TYPED_ARRAY_KIND: {
		fprintf(dumper->file, ".kind = XS_TYPED_ARRAY_KIND}, ");
		fprintf(dumper->file, ".value = { .typedArray = { .dispatch = gxTypeDispatches[%zu], .atomics = gxTypeAtomics[%zu] }", (size_t)slot->value.typedArray.dispatch, (size_t)slot->value.typedArray.atomics);
	} break;
	case XS_WEAK_MAP_KIND: {
		fprintf(dumper->file, "weakMap = { .first = ");
		fxDumpSlotAddress(dumper, slot->value.weakList.first);
		fprintf(dumper->file, ", .link = ");
		fxDumpSlotAddress(dumper, slot->value.weakList.link);
		fprintf(dumper->file, " }");
	} break;
	case XS_WEAK_SET_KIND: {
		fprintf(dumper->file, "weakSet = { .first = ");
		fxDumpSlotAddress(dumper, slot->value.weakList.first);
		fprintf(dumper->file, ", .link = ");
		fxDumpSlotAddress(dumper, slot->value.weakList.link);
		fprintf(dumper->file, " }");
	} break;
	case XS_WEAK_REF_KIND: {
		fprintf(dumper->file, "weakRef = { .target = ");
		fxDumpSlotAddress(dumper, slot->value.weakRef.target);
		fprintf(dumper->file, ", .link = ");
		fxDumpSlotAddress(dumper, slot->value.weakRef.link);
		fprintf(dumper->file, " }");
	} break;
	case XS_ACCESSOR_KIND: {
		fprintf(dumper->file, "accessor = { .getter = ");
		fxDumpSlotAddress(dumper, slot->value.accessor.getter);
		fprintf(dumper->file, ", .setter = ");
		fxDumpSlotAddress(dumper, slot->value.accessor.setter);
		fprintf(dumper->file, " }");
	} break;
	case XS_AT_KIND: {
		fprintf(dumper->file, "at = { 0x%x, %d }", slot->value.at.index, slot->value.at.id);
	} break;
	case XS_ENTRY_KIND: {
		fprintf(dumper->file, "entry = { ");
		fxDumpSlotAddress(dumper, slot->value.entry.slot);
		fprintf(dumper->file, ", 0x%x }", slot->value.entry.sum);
	} break;
	case XS_ERROR_KIND: {
		fprintf(dumper->file, "error = { ");
		fxDumpSlotAddress(dumper, slot->value.error.info);
		fprintf(dumper->file, ", %d }", slot->value.error.which);
	} break;
	case XS_EXPORT_KIND: {
		fprintf(dumper->file, "export = { .closure = ");
		fxDumpSlotAddress(dumper, slot->value.export.closure);
		fprintf(dumper->file, ", .module = ");
		fxDumpSlotAddress(dumper, slot->value.export.module);
		fprintf(dumper->file, " }");
	} break;
	case XS_HOME_KIND: {
		fprintf(dumper->file, "home = { .object = ");
		fxDumpSlotAddress(dumper, slot->value.home.object);
		fprintf(dumper->file, ", .module = ");
		fxDumpSlotAddress(dumper, slot->value.home.module);
		fprintf(dumper->file, " }");
	} break;
	case XS_KEY_KIND: {
		fprintf(dumper->file, "key = { .string = ");
		fxDumpChunkAddress(dumper, slot->value.key.string);
		fprintf(dumper->file, ", .sum = 0x%x }", slot->value.key.sum);
	} break;
	case XS_LIST_KIND: {
		fprintf(dumper->file, "list = { .first = ");
		fxDumpSlotAddress(dumper, slot->value.list.first);
		fprintf(dumper->file, ", .last = ");
		fxDumpSlotAddress(dumper, slot->value.list.last);
		fprintf(dumper->file, " }");
	} break;
	case XS_PRIVATE_KIND: {
		fprintf(dumper->file, "private = { .check = ");
		fxDumpSlotAddress(dumper, slot->value.private.check);
		fprintf(dumper->file, ", .first = ");
		fxDumpSlotAddress(dumper, slot->value.private.first);
		fprintf(dumper->file, " }");
	} break;
	case XS_STACK_KIND: {
		fprintf(dumper->file, "stack = { .address = ");
		fxDumpChunkAddress(dumper, slot->value.array.address);
		fprintf(dumper->file, ", .length = %d }", (int)slot->value.array.length);
	} break;
	case XS_WEAK_ENTRY_KIND: {
		fprintf(dumper->file, "weakEntry = { .check = ");
		fxDumpSlotAddress(dumper, slot->value.weakEntry.check);
		fprintf(dumper->file, ", .value = ");
		fxDumpSlotAddress(dumper, slot->value.weakEntry.value);
		fprintf(dumper->file, " }");
	} break;
	case XS_HOST_KIND: {
		fprintf(dumper->file, "host = { .data = ");
		fxDumpChunkAddress(dumper, slot->value.host.data);
		fprintf(dumper->file, " }");
	} break;
	default:
		break;
	}
}

void fxDumpSlotAddress(txDumper* dumper, void* address) 
{
	if (address)
		fprintf(dumper->file, "[%8.8zu]", (size_t)address);
	else
		fprintf(dumper->file, "[        ]");
}

void fxDumpSlotTable(txDumper* dumper, txByte* buffer, txSize size)
{
	txSize offset = 0;
	txSlot** address = (txSlot**)buffer;
	size /= sizeof(txSlot*);
	while (offset < size) {
		txSlot* slot = *((txSlot**)address);
		if (offset % 8)
			fprintf(dumper->file, " ");
		else
			fprintf(dumper->file, "\n\t");
		fxDumpSlotAddress(dumper, slot);
		offset++;
		address++;
	}
}

