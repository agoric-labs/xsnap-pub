#include "xsnap.h"

#define SNAPSHOT_SIGNATURE "xsnap 1"

static void xsBuildAgent(xsMachine* the);
static void xsFreezeAgent(xsMachine* machine);
static void xsPlayTest(xsMachine* the);
static void xsPrintUsage();

extern void xs_clearTimer(xsMachine* the);
static void xs_gc(xsMachine* the);
static void xs_issueCommand(xsMachine* the);
static void xs_print(xsMachine* the);
static void xs_setImmediate(xsMachine* the);
static void xs_setInterval(xsMachine* the);
static void xs_setTimeout(xsMachine* the);

#define mxSnapshotCallbackCount 7
xsCallback gxSnapshotCallbacks[mxSnapshotCallbackCount] = {
	xs_issueCommand,
	xs_clearTimer,
	xs_print,
	xs_setImmediate,
	xs_gc,
	xs_setInterval,
	xs_setTimeout,
};

static int xsSnapshopRead(void* stream, void* address, size_t size)
{
	return (fread(address, size, 1, stream) == 1) ? 0 : errno;
}

static int xsSnapshopWrite(void* stream, void* address, size_t size)
{
	return (fwrite(address, size, 1, stream) == 1) ? 0 : errno;
}
	
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
	int argr = 0;
	int argw = 0;
	int error = 0;
	int interval = 0;
	int option = 0;
	int freeze = 0;
	xsCreation _creation = {
		16 * 1024 * 1024, 	/* initialChunkSize */
		16 * 1024 * 1024, 	/* incrementalChunkSize */
		1 * 1024 * 1024, 	/* initialHeapCount */
		1 * 1024 * 1024, 	/* incrementalHeapCount */
		4096, 				/* stackCount */
		4096*3, 			/* keyCount */
		1993, 				/* nameModulo */
		127, 				/* symbolModulo */
		256 * 1024,			/* parserBufferSize */
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
		if (!strcmp(argv[argi], "-e"))
			option = 1;
		else if (!strcmp(argv[argi], "-f")) {
			if (argw) {
				xsPrintUsage();
				return 1;
			}
			freeze = 1;
		}
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
		else if (!strcmp(argv[argi], "-p"))
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
			if (freeze) {
				xsPrintUsage();
				return 1;
			}
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
	if (freeze) {
		xsFreezeBuiltIns(machine);
		xsFreezeAgent(machine);
		xsShareMachine(machine);
		xsCheckAliases(machine);
		machine = xsCloneMachine(creation, machine, "xsnap", NULL);
	}
	xsBeginMetering(machine, xsMeteringCallback, interval);
	{
		xsBeginHost(machine);
		{
			xsVars(1);
			if (option == 4) {
				xsPlayTest(the);
			}
			else {
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
		}
		xsEndHost(machine);
		xsRunLoop(machine);
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
	xsEndMetering(machine);
	if (machine->abortStatus)
		error = machine->abortStatus;
	xsDeleteMachine(machine);
	xsTerminateSharedCluster();
	return error;
}

void xsBuildAgent(xsMachine* machine) 
{
	xsBeginHost(machine);
	
	xsResult = xsNewHostFunction(xs_clearTimer, 1);
	xsDefine(xsGlobal, xsID("clearImmediate"), xsResult, xsDontEnum);
	xsResult = xsNewHostFunction(xs_setImmediate, 1);
	xsDefine(xsGlobal, xsID("setImmediate"), xsResult, xsDontEnum);
	
	xsResult = xsNewHostFunction(xs_clearTimer, 1);
	xsDefine(xsGlobal, xsID("clearInterval"), xsResult, xsDontEnum);
	xsResult = xsNewHostFunction(xs_setInterval, 1);
	xsDefine(xsGlobal, xsID("setInterval"), xsResult, xsDontEnum);
	
	xsResult = xsNewHostFunction(xs_clearTimer, 1);
	xsDefine(xsGlobal, xsID("clearTimeout"), xsResult, xsDontEnum);
	xsResult = xsNewHostFunction(xs_setTimeout, 1);
	xsDefine(xsGlobal, xsID("setTimeout"), xsResult, xsDontEnum);
	
	xsResult = xsNewHostFunction(xs_gc, 1);
	xsDefine(xsGlobal, xsID("gc"), xsResult, xsDontEnum);
	xsResult = xsNewHostFunction(xs_print, 1);
	xsDefine(xsGlobal, xsID("print"), xsResult, xsDontEnum);
	
	xsResult = xsNewHostFunction(xs_issueCommand, 1);
	xsDefine(xsGlobal, xsID("issueCommand"), xsResult, xsDontEnum);
	
	xsEndHost(machine);
}

void xsFreezeAgent(xsMachine* machine) 
{
	xsBeginHost(machine);
	xsVars(2);
	xsVar(0) = xsGet(xsGlobal, xsID("Object"));

	xsVar(1) = xsGet(xsGlobal, xsID("clearImmediate"));
	xsCall2(xsVar(0), xsID("freeze"), xsVar(1), xsTrue);
	xsVar(1) = xsGet(xsGlobal, xsID("setImmediate"));
	xsCall2(xsVar(0), xsID("freeze"), xsVar(1), xsTrue);
	
	xsVar(1) = xsGet(xsGlobal, xsID("clearInterval"));
	xsCall2(xsVar(0), xsID("freeze"), xsVar(1), xsTrue);
	xsVar(1) = xsGet(xsGlobal, xsID("setInterval"));
	xsCall2(xsVar(0), xsID("freeze"), xsVar(1), xsTrue);
	
	xsVar(1) = xsGet(xsGlobal, xsID("clearTimeout"));
	xsCall2(xsVar(0), xsID("freeze"), xsVar(1), xsTrue);
	xsVar(1) = xsGet(xsGlobal, xsID("setTimeout"));
	xsCall2(xsVar(0), xsID("freeze"), xsVar(1), xsTrue);
	
	xsVar(1) = xsGet(xsGlobal, xsID("gc"));
	xsCall2(xsVar(0), xsID("freeze"), xsVar(1), xsTrue);
	xsVar(1) = xsGet(xsGlobal, xsID("print"));
	xsCall2(xsVar(0), xsID("freeze"), xsVar(1), xsTrue);
	
	xsVar(1) = xsGet(xsGlobal, xsID("issueCommand"));
	xsCall2(xsVar(0), xsID("freeze"), xsVar(1), xsTrue);
	
	xsEndHost(machine);
}

void xsPlayTest(xsMachine* the)
{
	int index = 0;
	char* extensions[2] = { ".js", ".json" };
	for (;;) {
		int which;
		for (which = 0; which < 2; which++) {
			char path[PATH_MAX];
			struct stat a_stat;
			sprintf(path, "param-%d%s", index, extensions[which]);
			if (stat(path, &a_stat) == 0) {
				if (S_ISREG(a_stat.st_mode)) {
					fprintf(stderr, "### %s\n", path);
					FILE* file = fopen(path, "rb");
					if (file) {
						size_t length;
						fseek(file, 0, SEEK_END);
						length = ftell(file);
						fseek(file, 0, SEEK_SET);
						if (which == 0) {
							xsStringValue string;
							xsResult = xsStringBuffer(NULL, length);
							string = xsToString(xsResult);
							length = fread(string, 1, length, file);
							string[length] = 0;
							fclose(file);
							xsCall1(xsGlobal, xsID("eval"), xsResult);
						}
						else {
							xsResult = xsArrayBuffer(NULL, length);
							length = fread(xsToArrayBuffer(xsResult), 1, length, file);	
							fclose(file);
							xsCall1(xsGlobal, xsID("handleCommand"), xsResult);
						}
					}
					index++;
					break;
				}
			}
		}
		if (which == 2)
			break;
	}
}

void xsPrintUsage()
{
	printf("xsnap [-h] [-e] [-f] [i <interval] [l <limit] [-m] [-r <snapshot>] [-s] [-v] [-w <snapshot>] strings...\n");
	printf("\t-e: eval strings\n");
	printf("\t-f: freeze the XS machine\n");
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
	printf("-f and -w are incompatible\n");
}

void xs_gc(xsMachine* the)
{
	xsCollectGarbage();
}

void xs_issueCommand(xsMachine* the)
{
	static int index = 0;
	char path[PATH_MAX];
	FILE* file;
	size_t length;
	sprintf(path, "reply-%d.json", index);
	fprintf(stderr, "### %s\n", path);
	file = fopen(path, "rb");
	if (file) {
		fseek(file, 0, SEEK_END);
		length = ftell(file);
		fseek(file, 0, SEEK_SET);
		xsResult = xsArrayBuffer(NULL, length);
		length = fread(xsToArrayBuffer(xsResult), 1, length, file);	
		fclose(file);
	}
	index++;
}

void xs_print(xsMachine* the)
{
	xsIntegerValue c = xsToInteger(xsArgc), i;
#ifdef mxMetering
	if (gxMeteringPrint)
		fprintf(stdout, "[%u] ", xsGetCurrentMeter(the));
#endif
	for (i = 0; i < c; i++) {
		if (i)
			fprintf(stdout, " ");
		fprintf(stdout, "%s", xsToString(xsArg(i)));
	}
	fprintf(stdout, "\n");
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

void xs_clearTimer(xsMachine* the)
{
	xsClearTimer();
}





