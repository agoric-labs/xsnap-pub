# xsbug protocol in node.js

This snippet shows how to use the xsbug protocol to profile and instrument the xsnap worker in node.js

To test, build the xsnap worker then execute

	npm install --save saxophone
	node main.js
	
in this directory. A few seconds later the console displays

	Metering: 178698743 times
	Chunk used: 16795680 / 33554432 bytes
	Slot used: 8514336 / 12582912 bytes
	Stack used: 6880 / 131072 bytes
	Garbage collections: 12 times
	Keys used: 483 keys
	Modules loaded: 0 modules
	Parser used: 8418919 bytes
	Floating Point: 6139586 operations
	  1467 ms   1467 ms (anonymous-540) (test.js:8)
	   119 ms   1771 ms test (test.js:1)
	    79 ms     79 ms foo (test.js:4)
	    73 ms     83 ms Array.prototype.push
	    31 ms     31 ms (gc)
	     2 ms   1479 ms Array.prototype.sort

The snippet also writes a `test.cpuprofile` file that can be viewed in **Google Chrome DevTools**, **Visual Studio Code**, and **xsbug**. See [XS Profiler](https://github.com/Moddable-OpenSource/moddable/blob/public/documentation/xs/XS%20Profiler.md) for details.

## main.js

The script spawns the xsnap worker process with four pipes:

- 3: xsnap output
- 4: xsnap input
- 5: xsbug output
- 6: xsbug input

The xsbug pipes are used for the xsbug protocol thru an xsbug `Machine` object.

	const machine = new Machine(xsbugInput, xsbugOutput);
	machine.on('instruments', (instruments) => {
		machine.reportInstruments(instruments);
	});
	machine.on('profile', (profile) => {
		profile.reportHits();
		fs.writeFileSync("./test.cpuprofile", profile.toString());
	});
	machine.doStartProfiling();

The xsnap pipes are used to communicate with xsnap as usual. The script reads the `test.js` file, sends an `e{...}` command to xsnap and waits for the result.

	xsnapInput.on('data', (data) => {
		machine.doStopProfiling();
	});
	const buffer = fs.readFileSync("./test.js");
	writeNetString(xsnapOutput, `e{${buffer.toString()}}`);
	
## xsbug-machine.js

This module defines the xsbug `Machine` class.

The xsbug protocol uses XML so the module needs a SAX parser. A few have been tried but [Saxophone](https://github.com/matteodelabre/saxophone) was the only one that worked well. 

The xsbug `Machine` class defines a bunch of commands and internal events. These are all the commands and events that **xsbug** uses.

Currently there are two external events: `'instruments'` and `'profile'`.

## xsbug-profile.js

This module defines the xsbug `Profile` class.

The xsbug `Machine` object creates an xsbug `Profile` object to accumulate profile records and samples.

Once the xsbug `Machine` object triggered the `'profile'` event, the xsbug `Profile` object can be transformed into a `JSON` string to be saved into a `.cpuprofile` file.

The xsbug `Profile` object can also report a summary of the profile hits to the console.

## ../sources

`xsnapPlatform.c` has been modified to use pipes instead of sockets for the xsbug protocol.
	
### select	
	
Because there are two read pipes, the xsnap worker cannot block while waiting for one of them to be readable. So `xsnap-worker.c` has been modified to use `select` in its main loop.

		fd_set rfds;
		char done = 0;
		while (!done) {
			FD_ZERO(&rfds);
			FD_SET(3, &rfds);
			FD_SET(5, &rfds);
			if (select(6, &rfds, NULL, NULL, NULL) >= 0) {
				if (FD_ISSET(5, &rfds))
					xsRunDebugger(machine);
				if (!FD_ISSET(3, &rfds))
					continue;
			}
			else {
				fprintf(stderr, "select failed\n");
				error = E_IO_ERROR;
				break;
			}
			//...
			
### metering instrument	

XS allows hosts to add instruments. As an example, `xsnap-worker.c` has been modified to add a metering instrument.

	#if mxInstrument
	#define xsnapInstrumentCount 1
	static xsStringValue xsnapInstrumentNames[xsnapInstrumentCount] = {
		"Metering",
	};
	static xsStringValue xsnapInstrumentUnits[xsnapInstrumentCount] = {
		" times",
	};
	static xsIntegerValue xsnapInstrumentValues[xsnapInstrumentCount] = {
		0,
	};
	#endif
	
Instruments needs to be described once, typically after creating the XS machine.

	#if mxInstrument
	xsDescribeInstrumentation(machine, xsnapInstrumentCount, xsnapInstrumentNames, xsnapInstrumentUnits);
	#endif
	
Then instruments can be sampled at will.

	#if mxInstrument
	xsnapInstrumentValues[0] = (xsIntegerValue)meterIndex;
	xsSampleInstrumentation(machine, xsnapInstrumentCount, xsnapInstrumentValues);
	#endif

Every time instruments are sampled, the xsbug `Machine` object triggers the `'instruments'` event. 



