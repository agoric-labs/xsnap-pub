#include "xsAll.h"
#include "xsScript.h"
#include "xsSnapshot.h"

#ifndef mxReserveChunkSize
	#define mxReserveChunkSize 1024 * 1024 * 1024
#endif

extern txScript* fxLoadScript(txMachine* the, txString path, txUnsigned flags);

mxExport txInteger fxCheckAliases(txMachine* the);
mxExport void fxFreezeBuiltIns(txMachine* the);

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

typedef struct sxAliasIDLink txAliasIDLink;
typedef struct sxAliasIDList txAliasIDList;

struct sxAliasIDLink {
	txAliasIDLink* previous;
	txAliasIDLink* next;
	txInteger id;
	txInteger flag;
};

struct sxAliasIDList {
	txAliasIDLink* first;
	txAliasIDLink* last;
	txFlag* aliases;
	txInteger errorCount;
};

static void fxCheckAliasesError(txMachine* the, txAliasIDList* list, txFlag flag);
static void fxCheckEnvironmentAliases(txMachine* the, txSlot* environment, txAliasIDList* list);
static void fxCheckInstanceAliases(txMachine* the, txSlot* instance, txAliasIDList* list);

#define mxPushLink(name,ID,FLAG) \
	txAliasIDLink name = { C_NULL, C_NULL, ID, FLAG }; \
	name.previous = list->last; \
	if (list->last) \
		list->last->next = &name; \
	else \
		list->first = &name; \
	list->last = &name

#define mxPopLink(name) \
	if (name.previous) \
		name.previous->next = C_NULL; \
	else \
		list->first = C_NULL; \
	list->last = name.previous

enum {
	XSL_MODULE_FLAG,
	XSL_EXPORT_FLAG,
	XSL_ENVIRONMENT_FLAG,
	XSL_PROPERTY_FLAG,
	XSL_ITEM_FLAG,
	XSL_GETTER_FLAG,
	XSL_SETTER_FLAG,
	XSL_PROXY_HANDLER_FLAG,
	XSL_PROXY_TARGET_FLAG,
	XSL_GLOBAL_FLAG,
};

txInteger fxCheckAliases(txMachine* the) 
{
	txAliasIDList _list = { C_NULL, C_NULL }, *list = &_list;
	txSlot* module = mxProgram.value.reference->next; //@@
	list->aliases = c_calloc(the->aliasCount, sizeof(txFlag));
	while (module) {
		txSlot* export = mxModuleExports(module)->value.reference->next;
		if (export) {
			mxPushLink(moduleLink, module->ID, XSL_MODULE_FLAG);
			while (export) {
				txSlot* closure = export->value.export.closure;
				if (closure) {
					mxPushLink(exportLink, export->ID, XSL_EXPORT_FLAG);
					if (closure->ID != XS_NO_ID) {
						if (list->aliases[closure->ID] == 0) {
							list->aliases[closure->ID] = 1;
							fxCheckAliasesError(the, list, 0);
						}
					}
					if (closure->kind == XS_REFERENCE_KIND) {
						fxCheckInstanceAliases(the, closure->value.reference, list);
					}
					mxPopLink(exportLink);
				}
				export = export->next;
			}
			mxPopLink(moduleLink);
		}
		module = module->next;
	}
	{
		txSlot* global = mxGlobal.value.reference->next->next;
		while (global) {
			if ((global->ID != mxID(_global)) && (global->ID != mxID(_globalThis))) {
				mxPushLink(globalLink, global->ID, XSL_GLOBAL_FLAG);
				if (global->kind == XS_REFERENCE_KIND) {
					fxCheckInstanceAliases(the, global->value.reference, list);
				}
				mxPopLink(globalLink);
			}
			global = global->next;
		}
	}
	{
		fxCheckEnvironmentAliases(the, mxException.value.reference, list);
	}
	return list->errorCount;
}

void fxCheckAliasesError(txMachine* the, txAliasIDList* list, txFlag flag) 
{
	txAliasIDLink* link = list->first;
	if (flag > 1)
		fprintf(stderr, "### error");
	else
		fprintf(stderr, "### warning");
	while (link) {
		switch (link->flag) {
		case XSL_PROPERTY_FLAG: fprintf(stderr, "."); break;
		case XSL_ITEM_FLAG: fprintf(stderr, "["); break;
		case XSL_GETTER_FLAG: fprintf(stderr, ".get "); break;
		case XSL_SETTER_FLAG: fprintf(stderr, ".set "); break;
		case XSL_ENVIRONMENT_FLAG: fprintf(stderr, "() "); break;
		case XSL_PROXY_HANDLER_FLAG: fprintf(stderr, ".(handler)"); break;
		case XSL_PROXY_TARGET_FLAG: fprintf(stderr, ".(target)"); break;
		default: fprintf(stderr, ": "); break;
		}
		if (link->id < 0) {
			if (link->id != XS_NO_ID) {
				char* string = fxGetKeyName(the, link->id);
				if (string) {
					if (link->flag == XSL_MODULE_FLAG) {
						char* dot = c_strrchr(string, '.');
						if (dot) {
							*dot = 0;
							fprintf(stderr, "\"%s\"", string);
							*dot = '.';
						}
						else
							fprintf(stderr, "%s", string);
					}
					else if (link->flag == XSL_GLOBAL_FLAG) {
						fprintf(stderr, "globalThis."); 
						fprintf(stderr, "%s", string);
					}
					else
						fprintf(stderr, "%s", string);
				}
				else
					fprintf(stderr, "%d", link->id);
			}
		}
		else 
			fprintf(stderr, "%d", link->id);
		if (link->flag == XSL_ITEM_FLAG)
			fprintf(stderr, "]");
		link = link->next;
	}
	if (flag == 3) {
		fprintf(stderr, ": generator\n");
		list->errorCount++;
	}
	else if (flag == 2) {
		fprintf(stderr, ": regexp\n");
		list->errorCount++;
	}
	else if (flag)
		fprintf(stderr, ": not frozen\n");
	else
		fprintf(stderr, ": no const\n");
}

void fxCheckEnvironmentAliases(txMachine* the, txSlot* environment, txAliasIDList* list) 
{
	txSlot* closure = environment->next;
	if (environment->flag & XS_LEVEL_FLAG)
		return;
	environment->flag |= XS_LEVEL_FLAG;
	if (environment->value.instance.prototype)
		fxCheckEnvironmentAliases(the, environment->value.instance.prototype, list);
	while (closure) {
		if (closure->kind == XS_CLOSURE_KIND) {
			txSlot* slot = closure->value.closure;
			mxPushLink(closureLink, closure->ID, XSL_ENVIRONMENT_FLAG);
			if (slot->ID != XS_NO_ID) {
				if (list->aliases[slot->ID] == 0) {
					list->aliases[slot->ID] = 1;
					fxCheckAliasesError(the, list, 0);
				}
			}
			if (slot->kind == XS_REFERENCE_KIND) {
				fxCheckInstanceAliases(the, slot->value.reference, list);
			}
			mxPopLink(closureLink);
		}
		closure = closure->next;
	}
	//environment->flag &= ~XS_LEVEL_FLAG;
}

void fxCheckInstanceAliases(txMachine* the, txSlot* instance, txAliasIDList* list) 
{
	txSlot* property = instance->next;
	if (instance->flag & XS_LEVEL_FLAG)
		return;
	instance->flag |= XS_LEVEL_FLAG;
	if (instance->value.instance.prototype) {
		mxPushLink(propertyLink, mxID(___proto__), XSL_PROPERTY_FLAG);
		fxCheckInstanceAliases(the, instance->value.instance.prototype, list);
		mxPopLink(propertyLink);
	}
	if (instance->ID != XS_NO_ID) {
		if (list->aliases[instance->ID] == 0) {
			list->aliases[instance->ID] = 1;
			fxCheckAliasesError(the, list, 1);
		}
	}
	while (property) {
		if (property->kind == XS_ACCESSOR_KIND) {
			if (property->value.accessor.getter) {
				mxPushLink(propertyLink, property->ID, XSL_GETTER_FLAG);
				fxCheckInstanceAliases(the, property->value.accessor.getter, list);
				mxPopLink(propertyLink);
			}
			if (property->value.accessor.setter) {
				mxPushLink(propertyLink, property->ID, XSL_SETTER_FLAG);
				fxCheckInstanceAliases(the, property->value.accessor.setter, list);
				mxPopLink(propertyLink);
			}
		}
		else if (property->kind == XS_ARRAY_KIND) {
			txSlot* item = property->value.array.address;
			txInteger length = (txInteger)fxGetIndexSize(the, property);
			while (length > 0) {
				if (item->kind == XS_REFERENCE_KIND) {
					mxPushLink(propertyLink, *((txInteger*)item), XSL_ITEM_FLAG);
					fxCheckInstanceAliases(the, item->value.reference, list);
					mxPopLink(propertyLink);
				}
				item++;
				length--;
			}
		}
		else if ((property->kind == XS_CODE_KIND) || (property->kind == XS_CODE_X_KIND)) {
			if (property->value.code.closures)
				fxCheckEnvironmentAliases(the, property->value.code.closures, list);
		}
		else if (property->kind == XS_PROXY_KIND) {
			if (property->value.proxy.handler) {
				mxPushLink(propertyLink, XS_NO_ID, XSL_PROXY_HANDLER_FLAG);
				fxCheckInstanceAliases(the, property->value.proxy.handler, list);
				mxPopLink(propertyLink);
			}
			if (property->value.proxy.target) {
				mxPushLink(propertyLink, XS_NO_ID, XSL_PROXY_TARGET_FLAG);
				fxCheckInstanceAliases(the, property->value.proxy.target, list);
				mxPopLink(propertyLink);
			}
		}
		else if (property->kind == XS_REFERENCE_KIND) {
			mxPushLink(propertyLink, property->ID, XSL_PROPERTY_FLAG);
			fxCheckInstanceAliases(the, property->value.reference, list);
			mxPopLink(propertyLink);
		}
		property = property->next;
	}
// 	instance->flag &= ~XS_LEVEL_FLAG;
}

void fxFreezeBuiltIns(txMachine* the)
{
#define mxFreezeBuiltInCall \
	mxPush(mxObjectConstructor); \
	mxPushSlot(freeze); \
	mxCall()
#define mxFreezeBuiltInRun \
	mxPushBoolean(1); \
	mxRunCount(2); \
	mxPop()

	txSlot* freeze;
	txInteger id;

	mxTemporary(freeze);
	mxPush(mxObjectConstructor);
	fxGetID(the, mxID(_freeze));
	mxPullSlot(freeze);
	
	for (id = XS_SYMBOL_ID_COUNT; id < _Infinity; id++) {
		mxFreezeBuiltInCall; mxPush(the->stackPrototypes[-1 - id]); mxFreezeBuiltInRun;
	}
	for (id = _Compartment; id < XS_INTRINSICS_COUNT; id++) {
		mxFreezeBuiltInCall; mxPush(the->stackPrototypes[-1 - id]); mxFreezeBuiltInRun;
	}
	
	mxFreezeBuiltInCall; mxPush(mxArgumentsSloppyPrototype); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPush(mxArgumentsStrictPrototype); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPush(mxArrayIteratorPrototype); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPush(mxAsyncFromSyncIteratorPrototype); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPush(mxAsyncFunctionPrototype); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPush(mxAsyncGeneratorFunctionPrototype); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPush(mxAsyncGeneratorPrototype); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPush(mxAsyncIteratorPrototype); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPush(mxGeneratorFunctionPrototype); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPush(mxGeneratorPrototype); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPush(mxHostPrototype); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPush(mxIteratorPrototype); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPush(mxMapIteratorPrototype); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPush(mxModulePrototype); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPush(mxRegExpStringIteratorPrototype); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPush(mxSetIteratorPrototype); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPush(mxStringIteratorPrototype); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPush(mxTransferPrototype); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPush(mxTypedArrayPrototype); mxFreezeBuiltInRun;

	mxFreezeBuiltInCall; mxPush(mxAssignObjectFunction); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPush(mxCopyObjectFunction); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPush(mxEnumeratorFunction); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPush(mxInitializeRegExpFunction); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPush(mxOnRejectedPromiseFunction); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPush(mxOnResolvedPromiseFunction); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPush(mxOnThenableFunction); mxFreezeBuiltInRun;
	
	mxFreezeBuiltInCall; mxPushReference(mxArrayLengthAccessor.value.accessor.getter); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPushReference(mxArrayLengthAccessor.value.accessor.setter); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPushReference(mxStringAccessor.value.accessor.getter); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPushReference(mxStringAccessor.value.accessor.setter); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPushReference(mxProxyAccessor.value.accessor.getter); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPushReference(mxProxyAccessor.value.accessor.setter); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPushReference(mxTypedArrayAccessor.value.accessor.getter); mxFreezeBuiltInRun;
	mxFreezeBuiltInCall; mxPushReference(mxTypedArrayAccessor.value.accessor.setter); mxFreezeBuiltInRun;
	
	mxFreezeBuiltInCall; mxPush(mxArrayPrototype); fxGetID(the, mxID(_Symbol_unscopables)); mxFreezeBuiltInRun;
	
	mxFreezeBuiltInCall; mxPush(mxProgram); mxFreezeBuiltInRun; //@@
	mxFreezeBuiltInCall; mxPush(mxHosts); mxFreezeBuiltInRun; //@@
	
	mxPop();
}

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
