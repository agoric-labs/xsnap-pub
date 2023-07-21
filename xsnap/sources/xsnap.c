#include "xsnap.h"

#define SNAPSHOT_SIGNATURE "xsnap 1"

extern void fxDumpSnapshot(xsMachine* the, xsSnapshot* snapshot);

static void xsBuildAgent(xsMachine* the);
static void xsPrintUsage();
static void xsReplay(xsMachine* machine);

//static void xs_clearTimer(xsMachine* the);
static void xs_currentMeterLimit(xsMachine* the);
static void xs_gc(xsMachine* the);
static void xs_issueCommand(xsMachine* the);
//static void xs_lockdown(xsMachine *the);
static void xs_performance_now(xsMachine* the);
static void xs_print(xsMachine* the);
static void xs_resetMeter(xsMachine* the);
static void xs_setImmediate(xsMachine* the);
//static void xs_setInterval(xsMachine* the);
//static void xs_setTimeout(xsMachine* the);

extern void xs_textdecoder(xsMachine *the);
extern void xs_textdecoder_decode(xsMachine *the);
extern void xs_textdecoder_get_encoding(xsMachine *the);
extern void xs_textdecoder_get_ignoreBOM(xsMachine *the);
extern void xs_textdecoder_get_fatal(xsMachine *the);

extern void xs_textencoder(xsMachine *the);
extern void xs_textencoder_encode(xsMachine *the);
extern void xs_textencoder_encodeInto(xsMachine *the);

extern void modInstallTextDecoder(xsMachine *the);
extern void modInstallTextEncoder(xsMachine *the);

extern void xs_base64_encode(xsMachine *the);
extern void xs_base64_decode(xsMachine *the);
extern void modInstallBase64(xsMachine *the);

// The order of the callbacks materially affects how they are introduced to
// code that runs from a snapshot, so must be consistent in the face of
// upgrade.
#define mxSnapshotCallbackCount 18
xsCallback gxSnapshotCallbacks[mxSnapshotCallbackCount] = {
	xs_issueCommand, // 0
	xs_print, // 1
	xs_setImmediate, // 2
	xs_gc, // 3
	xs_performance_now, // 4
	xs_currentMeterLimit, // 5
	xs_resetMeter, // 6

	xs_textdecoder, // 7
	xs_textdecoder_decode, // 8
	xs_textdecoder_get_encoding, // 9
	xs_textdecoder_get_ignoreBOM, // 10
	xs_textdecoder_get_fatal, // 11

	xs_textencoder, // 12
	xs_textencoder_encode, // 13
	xs_textencoder_encodeInto, // 14

	xs_base64_encode, // 15
	xs_base64_decode, // 16

	fx_harden, // 17
	// fx_setInterval,
	// fx_setTimeout,
	// fx_clearTimer,
};

static int xsSnapshopRead(void* stream, void* address, size_t size)
{
	return (fread(address, size, 1, stream) == 1) ? 0 : errno;
}

static int xsSnapshopWrite(void* stream, void* address, size_t size)
{
	return (fwrite(address, size, 1, stream) == 1) ? 0 : errno;
}
	
static xsUnsignedValue gxCurrentMeter = 0;
static xsBooleanValue gxMeteringPrint = 0;
static xsUnsignedValue gxMeteringLimit = 0;
#ifdef mxMetering
static xsBooleanValue xsMeteringCallback(xsMachine* the, xsUnsignedValue index)
{
	if (index > gxMeteringLimit) {
// 		fprintf(stderr, "too much computation\n");
		return 0;
	}
// 	fprintf(stderr, "%d\n", index);
	return 1;
}
#endif

int main(int argc, char* argv[]) 
{
	int argi;
	int argd = 0;
	int argp = 0;
	int argr = 0;
	int argw = 0;
	int error = 0;
	int interval = 0;
	int option = 0;
	int parserBufferSize = 8192 * 1024;
	int profiling = 0;
	xsCreation _creation = {
		32 * 1024 * 1024,	/* initialChunkSize */
		4 * 1024 * 1024,	/* incrementalChunkSize */
		256 * 1024,			/* initialHeapCount */
		128 * 1024,			/* incrementalHeapCount */
		4096,				/* stackCount */
		32000, 				/* initialKeyCount */
		8000,				/* incrementalKeyCount */
		1993,				/* nameModulo */
		127,				/* symbolModulo */
		parserBufferSize,	/* parserBufferSize */
		1993,				/* parserTableModulo */
	};
	xsCreation* creation = &_creation;
	xsSnapshot snapshot = {
		SNAPSHOT_SIGNATURE,
		sizeof(SNAPSHOT_SIGNATURE) - 1,
		gxSnapshotCallbacks,
		mxSnapshotCallbackCount,
		xsSnapshopRead,
		xsSnapshopWrite,
		NULL,
		0,
		NULL,
		NULL,
		NULL,
		0,
		NULL,
	};
	xsMachine* machine;
	char path[C_PATH_MAX];
	char* dot;

	if (argc == 1) {
		xsPrintUsage();
		return 0;
	}
	for (argi = 1; argi < argc; argi++) {
		if (argv[argi][0] != '-')
			continue;
		if (!strcmp(argv[argi], "-d")) {
			argi++;
			if (argi < argc)
				argd = argi;
			else {
				xsPrintUsage();
				return 1;
			}
			option = 5;
		}
		else if (!strcmp(argv[argi], "-e"))
			option = 1;
		else if (!strcmp(argv[argi], "-h"))
			xsPrintUsage();
		else if (!strcmp(argv[argi], "-i")) {
			argi++;
			if (argi < argc)
				interval = atoi(argv[argi]);
			else {
				xsPrintUsage();
				return 1;
			}
		}
		else if (!strcmp(argv[argi], "-l")) {
			argi++;
			if (argi < argc)
				gxMeteringLimit = atoi(argv[argi]);
			else {
				xsPrintUsage();
				return 1;
			}
		}
		else if (!strcmp(argv[argi], "-m"))
			option = 2;
		else if (!strcmp(argv[argi], "-p")) {
			profiling = 1;
			argi++;
			if ((argi < argc) && (argv[argi][0] != '-'))
				argp = argi;
			else
				argi--;
		}
		else if (!strcmp(argv[argi], "-q"))
			gxMeteringPrint = 1;
		else if (!strcmp(argv[argi], "-r")) {
			argi++;
			if (argi < argc)
				argr = argi;
			else {
				xsPrintUsage();
				return 1;
			}
		}
		else if (!strcmp(argv[argi], "-s"))
			option = 3;
		else if (!strcmp(argv[argi], "-t"))
			option = 4;
		else if (!strcmp(argv[argi], "-v")) {
			xsVersion(path, sizeof(path));
			printf("XS %s\n", path);
		}
		else if (!strcmp(argv[argi], "-w")) {
			argi++;
			if (argi < argc)
				argw = argi;
			else {
				xsPrintUsage();
				return 1;
			}
		}
		else {
			xsPrintUsage();
			return 1;
		}
	}
	if (gxMeteringLimit) {
		if (interval == 0)
			interval = 1;
	}
	xsInitializeSharedCluster();
	if (argr) {
		snapshot.stream = fopen(argv[argr], "rb");
		if (snapshot.stream) {
			machine = xsReadSnapshot(&snapshot, "xsnap", NULL);
			fclose(snapshot.stream);
		}
		else
			snapshot.error = errno;
		if (snapshot.error) {
			fprintf(stderr, "cannot read snapshot %s: %s\n", argv[argr], strerror(snapshot.error));
			return 1;
		}
	}
	else {
		machine = xsCreateMachine(creation, "xsnap", NULL);
		xsBuildAgent(machine);
	}
	if (profiling)
		fxStartProfiling(machine);
	xsBeginMetering(machine, xsMeteringCallback, interval);
	{
		if (option == 5) {
			snapshot.stream = fopen(argv[argd], "rb");
			if (snapshot.stream) {
				fxDumpSnapshot(machine, &snapshot);
				fclose(snapshot.stream);
			}
			else
				snapshot.error = errno;
			if (snapshot.error) {
				fprintf(stderr, "cannot dump snapshot %s: %s\n", argv[argr], strerror(snapshot.error));
				return 1;
			}
		}
		else if (option == 4) {
			fprintf(stderr, "%p\n", machine);
			xsReplay(machine);
		}
		else {
			xsBeginHost(machine);
			{
				xsVars(1);
				for (argi = 1; argi < argc; argi++) {
					if (!strcmp(argv[argi], "-i")) {
						argi++;
						continue;
					}
					if (!strcmp(argv[argi], "-l")) {
						argi++;
						continue;
					}
					if (argv[argi][0] == '-')
						continue;
					if (argi == argp)
						continue;
					if (argi == argr)
						continue;
					if (argi == argw)
						continue;
					if (option == 1) {
						xsResult = xsString(argv[argi]);
						xsCall1(xsGlobal, xsID("eval"), xsResult);
					}
					else {	
						if (!c_realpath(argv[argi], path))
							xsURIError("file not found: %s", argv[argi]);
						dot = strrchr(path, '.');
						if (((option == 0) && dot && !c_strcmp(dot, ".mjs")) || (option == 2))
							xsRunModuleFile(path);
						else
							xsRunProgramFile(path);
					}
				}
			}
			xsRunLoop(machine);
			xsEndHost(machine);
			if (argw) {
				snapshot.stream = fopen(argv[argw], "wb");
				if (snapshot.stream) {
					xsWriteSnapshot(machine, &snapshot);
					fclose(snapshot.stream);
				}
				else
					snapshot.error = errno;
				if (snapshot.error) {
					fprintf(stderr, "cannot write snapshot %s: %s\n", argv[argw], strerror(snapshot.error));
				}
			}
		}
	}
	xsEndMetering(machine);
	if (profiling) {
		if (argp) {
			FILE* stream = fopen(argv[argp], "w");
			if (stream)
				fxStopProfiling(machine, stream);
			else
				fprintf(stderr, "cannot write profile %s: %s\n", argv[argp], strerror(errno));
		}
		else
			fxStopProfiling(machine, C_NULL);
	}
	if (machine->abortStatus)
		error = machine->abortStatus;
	xsDeleteMachine(machine);
	xsTerminateSharedCluster();
	return error;
}

void xsBuildAgent(xsMachine* machine) 
{
	xsBeginHost(machine);
	xsVars(1);
	
// 	xsResult = xsNewHostFunction(xs_clearTimer, 1);
// 	xsDefine(xsGlobal, xsID("clearImmediate"), xsResult, xsDontEnum);
	xsResult = xsNewHostFunction(xs_setImmediate, 1);
	xsDefine(xsGlobal, xsID("setImmediate"), xsResult, xsDontEnum);
	
// 	xsResult = xsNewHostFunction(xs_clearTimer, 1);
// 	xsDefine(xsGlobal, xsID("clearInterval"), xsResult, xsDontEnum);
// 	xsResult = xsNewHostFunction(xs_setInterval, 1);
// 	xsDefine(xsGlobal, xsID("setInterval"), xsResult, xsDontEnum);

// 	xsResult = xsNewHostFunction(xs_clearTimer, 1);
// 	xsDefine(xsGlobal, xsID("clearTimeout"), xsResult, xsDontEnum);
// 	xsResult = xsNewHostFunction(xs_setTimeout, 1);
// 	xsDefine(xsGlobal, xsID("setTimeout"), xsResult, xsDontEnum);
	
	xsResult = xsNewHostFunction(xs_gc, 1);
	xsDefine(xsGlobal, xsID("gc"), xsResult, xsDontEnum);
	xsResult = xsNewHostFunction(xs_print, 1);
	xsDefine(xsGlobal, xsID("print"), xsResult, xsDontEnum);
	
	xsResult = xsNewHostFunction(xs_issueCommand, 1);
	xsDefine(xsGlobal, xsID("issueCommand"), xsResult, xsDontEnum);
	
	xsResult = xsNewObject();
	xsVar(0) = xsNewHostFunction(xs_performance_now, 0);
	xsDefine(xsResult, xsID("now"), xsVar(0), xsDontEnum);
	xsDefine(xsGlobal, xsID("performance"), xsResult, xsDontEnum);
	
	xsResult = xsNewHostFunction(xs_currentMeterLimit, 1);
	xsDefine(xsGlobal, xsID("currentMeterLimit"), xsResult, xsDontEnum);
	xsResult = xsNewHostFunction(xs_resetMeter, 1);
	xsDefine(xsGlobal, xsID("resetMeter"), xsResult, xsDontEnum);

	modInstallTextDecoder(the);
	modInstallTextEncoder(the);
	modInstallBase64(the);
// 	
 	xsResult = xsNewHostFunction(fx_harden, 1);
 	xsDefine(xsGlobal, xsID("harden"), xsResult, xsDontEnum);
// 	xsResult = xsNewHostFunction(xs_lockdown, 0);
// 	xsDefine(xsGlobal, xsID("lockdown"), xsResult, xsDontEnum);
// 	xsResult = xsNewHostFunction(fx_petrify, 1);
// 	xsDefine(xsGlobal, xsID("petrify"), xsResult, xsDontEnum);
// 	xsResult = xsNewHostFunction(fx_mutabilities, 1);
// 	xsDefine(xsGlobal, xsID("mutabilities"), xsResult, xsDontEnum);

	xsEndHost(machine);
}

void xsPrintUsage()
{
	printf("xsnap [-h] [-e] [i <interval] [l <limit] [-m] [-r <snapshot>] [-s] [-v] [-w <snapshot>] strings...\n");
	printf("\t-d <snapshot>: dump snapshot to stderr\n");
	printf("\t-e: eval strings\n");
	printf("\t-h: print this help message\n");
	printf("\t-i <interval>: metering interval (default to 1)\n");
	printf("\t-l <limit>: metering limit (default to none)\n");
	printf("\t-m: strings are paths to modules\n");
	printf("\t-r <snapshot>: read snapshot to create the XS machine\n");
	printf("\t-s: strings are paths to scripts\n");
	printf("\t-v: print XS version\n");
	printf("\t-w <snapshot>: write snapshot of the XS machine at exit\n");
	printf("without -e, -m, -s:\n");
	printf("\tif the extension is .mjs, strings are paths to modules\n");
	printf("\telse strings are paths to scripts\n");
}

static int gxStep = 0;

void xsReplay(xsMachine* machine)
{
	char path[C_PATH_MAX];
	char* names[6] = { "-evaluate.dat", "-issueCommand.dat", "-snapshot.dat", "-command.dat", "-reply.dat", "-options.json", };
	for (;;) {
		int which;
		for (which = 0; which < 6; which++) {
			sprintf(path, "%05d%s", gxStep, names[which]);
			{
			#if mxWindows
				DWORD attributes = GetFileAttributes(path);
				if ((attributes != 0xFFFFFFFF) && (!(attributes & FILE_ATTRIBUTE_DIRECTORY)))
			#else
				struct stat a_stat;
				if ((stat(path, &a_stat) == 0) && (S_ISREG(a_stat.st_mode)))
			#endif
				{
					gxStep++;
					fprintf(stderr, "### %s\n", path);
					FILE* file = fopen(path, "rb");
					if (file) {
						size_t length;
						fseek(file, 0, SEEK_END);
						length = ftell(file);
						fseek(file, 0, SEEK_SET);
						if (which == 0) {
							xsBeginHost(machine);
							xsStringValue string;
							xsResult = xsStringBuffer(NULL, (xsIntegerValue)length);
							string = xsToString(xsResult);
							length = fread(string, 1, length, file);
							string[length] = 0;
							fclose(file);
							xsCall1(xsGlobal, xsID("eval"), xsResult);
							fxRunLoop(machine);
							xsEndHost(machine);
						}
						else if (which == 1) {
							xsBeginHost(machine);
							xsResult = xsArrayBuffer(NULL, (xsIntegerValue)length);
							length = fread(xsToArrayBuffer(xsResult), 1, length, file);	
							fclose(file);
							xsCall1(xsGlobal, xsID("handleCommand"), xsResult);
							fxRunLoop(machine);
							xsEndHost(machine);
						}
						else if (which == 2) {
// 							xsBeginHost(machine);
// 							xsCollectGarbage();
// 							xsEndHost(machine);
// 							fclose(file);
							char buffer[1024];
							char* slash;
							xsSnapshot snapshot = {
								SNAPSHOT_SIGNATURE,
								sizeof(SNAPSHOT_SIGNATURE) - 1,
								gxSnapshotCallbacks,
								mxSnapshotCallbackCount,
								xsSnapshopRead,
								xsSnapshopWrite,
								NULL,
								0,
								NULL,
								NULL,
								NULL,
								0,
								NULL
							};
							length = fread(buffer, 1, length, file);
							buffer[length] = 0;
							fclose(file);
							slash = c_strrchr(buffer, '/');
							if (slash) slash++;
							else slash = buffer;
							snapshot.stream = fopen(slash, "wb");
							if (snapshot.stream) {
								xsWriteSnapshot(machine, &snapshot);
								fclose(snapshot.stream);
							}
						}
						else
							fclose(file);
					}
					break;
				}
			}
		}
		if (which == 6)
			break;
	}
}

void xs_clearTimer(xsMachine* the)
{
	xsClearTimer();
}

void xs_currentMeterLimit(xsMachine* the)
{
#if mxMetering
	xsResult = xsInteger(gxCurrentMeter);
#endif
}

void xs_gc(xsMachine* the)
{
	xsCollectGarbage();
}

void xs_issueCommand(xsMachine* the)
{
	char path[C_PATH_MAX];
	FILE* file;
	size_t length;
	void* data;
	size_t argLength;
	void* argData;
	
	sprintf(path, "%05d-command.dat", gxStep);
	gxStep++;
	
	file = fopen(path, "rb");
	if (!file) xsUnknownError("cannot open %s", path);
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = c_malloc(length);
	length = fread(data, 1, length, file);	
	fclose(file);
	
	argLength = xsGetArrayBufferLength(xsArg(0));
	argData = xsToArrayBuffer(xsArg(0));
	
	if ((length != argLength) || c_memcmp(data, argData, length)) {
		fprintf(stderr, "### %s %.*s\n", path, (int)argLength, (char*)argData);
// 		fprintf(stderr, "@@@ %s %.*s\n", path, (int)length, (char*)data);
	}
	else
		fprintf(stderr, "### %s\n", path);
	c_free(data);
	
	sprintf(path, "%05d-reply.dat", gxStep);
	fprintf(stderr, "### %s\n", path);
	gxStep++;
	file = fopen(path, "rb");
	if (!file) xsUnknownError("cannot open %s", path);
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	xsResult = xsArrayBuffer(NULL, (xsIntegerValue)length);
	data = xsToArrayBuffer(xsResult);
	length = fread(data, 1, length, file);	
	fclose(file);
}

#if 0
void xs_lockdown(xsMachine *the)
{
	fx_lockdown(the);
	
	xsResult = xsGet(xsGlobal, xsID("Base64"));
	xsCall1(xsGlobal, xsID("harden"), xsResult);
	xsResult = xsGet(xsGlobal, xsID("TextDecoder"));
	xsCall1(xsGlobal, xsID("harden"), xsResult);
	xsResult = xsGet(xsGlobal, xsID("TextEncoder"));
	xsCall1(xsGlobal, xsID("harden"), xsResult);
	
// 	xsResult = xsGet(xsGlobal, xsID("clearImmediate"));
// 	xsCall1(xsGlobal, xsID("harden"), xsResult);
// 	xsResult = xsGet(xsGlobal, xsID("clearInterval"));
// 	xsCall1(xsGlobal, xsID("harden"), xsResult);
// 	xsResult = xsGet(xsGlobal, xsID("clearTimeout"));
// 	xsCall1(xsGlobal, xsID("harden"), xsResult);
	xsResult = xsGet(xsGlobal, xsID("currentMeterLimit"));
	xsCall1(xsGlobal, xsID("harden"), xsResult);
	xsResult = xsGet(xsGlobal, xsID("gc"));
	xsCall1(xsGlobal, xsID("harden"), xsResult);
	xsResult = xsGet(xsGlobal, xsID("harden"));
	xsCall1(xsGlobal, xsID("harden"), xsResult);
	xsResult = xsGet(xsGlobal, xsID("issueCommand"));
	xsCall1(xsGlobal, xsID("harden"), xsResult);
	xsResult = xsGet(xsGlobal, xsID("lockdown"));
	xsCall1(xsGlobal, xsID("harden"), xsResult);
	xsResult = xsGet(xsGlobal, xsID("mutabilities"));
	xsCall1(xsGlobal, xsID("harden"), xsResult);
	xsResult = xsGet(xsGlobal, xsID("performance"));
	xsCall1(xsGlobal, xsID("harden"), xsResult);
	xsResult = xsGet(xsGlobal, xsID("petrify"));
	xsCall1(xsGlobal, xsID("harden"), xsResult);
	xsResult = xsGet(xsGlobal, xsID("print"));
	xsCall1(xsGlobal, xsID("harden"), xsResult);
	xsResult = xsGet(xsGlobal, xsID("resetMeter"));
	xsCall1(xsGlobal, xsID("harden"), xsResult);
	xsResult = xsGet(xsGlobal, xsID("setImmediate"));
	xsCall1(xsGlobal, xsID("harden"), xsResult);
// 	xsResult = xsGet(xsGlobal, xsID("setInterval"));
// 	xsCall1(xsGlobal, xsID("harden"), xsResult);
// 	xsResult = xsGet(xsGlobal, xsID("setTimeout"));
// 	xsCall1(xsGlobal, xsID("harden"), xsResult);
}
#endif

void xs_performance_now(xsMachine *the)
{
	c_timeval tv;
	c_gettimeofday(&tv, NULL);
	xsResult = xsNumber((double)(tv.tv_sec * 1000.0) + ((double)(tv.tv_usec) / 1000.0));
}

void xs_print(xsMachine* the)
{
	xsIntegerValue c = xsToInteger(xsArgc), i;
	xsStringValue string, p, q;
	xsVars(1);
	xsVar(0) = xsGet(xsGlobal, xsID("String"));
	for (i = 0; i < c; i++) {
		xsArg(i) = xsCallFunction1(xsVar(0), xsUndefined, xsArg(i));
	}
#ifdef mxMetering
	if (gxMeteringPrint)
		fprintf(stdout, "[%u] ", xsGetCurrentMeter(the));
#endif
	for (i = 0; i < c; i++) {
		if (i)
			fprintf(stdout, " ");
		xsArg(i) = xsCallFunction1(xsVar(0), xsUndefined, xsArg(i));
		p = string = xsToString(xsArg(i));
	#if mxCESU8
		for (;;) {
			xsIntegerValue character;
			q = fxUTF8Decode(p, &character);
		again:
			if (character == C_EOF)
				break;
			if (character == 0) {
				if (p > string) {
					char c = *p;
					*p = 0;
					fprintf(stdout, "%s", string);
					*p = c;
				}
				string = q;
			}
			else if ((0x0000D800 <= character) && (character <= 0x0000DBFF)) {
				xsStringValue r = q;
				xsIntegerValue surrogate;
				q = fxUTF8Decode(r, &surrogate);
				if ((0x0000DC00 <= surrogate) && (surrogate <= 0x0000DFFF)) {
					char buffer[5];
					character = (xsIntegerValue)(0x00010000 + ((character & 0x03FF) << 10) + (surrogate & 0x03FF));
					if (p > string) {
						char c = *p;
						*p = 0;
						fprintf(stdout, "%s", string);
						*p = c;
					}
					p = fxUTF8Encode(buffer, character);
					*p = 0;
					fprintf(stdout, "%s", buffer);
					string = q;
				}
				else {
					p = r;
					character = surrogate;
					goto again;
				}
			}
			p = q;
		}
	#endif	
		fprintf(stdout, "%s", string);
	}
	fprintf(stdout, "\n");
}

void xs_resetMeter(xsMachine* the)
{
#if mxMetering
	xsIntegerValue argc = xsToInteger(xsArgc);
	if (argc < 2) {
		xsTypeError("expected newMeterLimit, newMeterIndex");
	}
	xsResult = xsInteger(xsGetCurrentMeter(the));
	gxCurrentMeter = xsToInteger(xsArg(0));
	xsSetCurrentMeter(the, xsToInteger(xsArg(1)));
#endif
}

void xs_setImmediate(xsMachine* the)
{
	xsSetTimer(0, 0);
}

void xs_setInterval(xsMachine* the)
{
	xsSetTimer(xsToNumber(xsArg(1)), 1);
}

void xs_setTimeout(xsMachine* the)
{
	xsSetTimer(xsToNumber(xsArg(1)), 0);
}




