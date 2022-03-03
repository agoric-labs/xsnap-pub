# xsnap
Revised: January 4, 2022

Warning: These notes are preliminary. Omissions and errors are likely. If you encounter problems, please ask for assistance.

## About

`xsnap` is a custom XS runtime that read and write XS snapshots. See [XS snapshots](./documentation/XS Snapshots.md) for details and the C programming interface. `xsnap` can create a machine from scratch or from a snapshot.

`xsnap` also uses the metering version of XS. There are options to constraint how much computation a machine can do. See [XS metering](./documentation/XS Metering.md)

## Additions

Besides ECMAScript built-ins and intrinsics, `xsnap` provides additional features.

- `gc()`
- `print(x)`

### Immutability

- `harden(it)`
- `lockdown()`

### Metering

- `currentMeterLimit()`
- `resetMeter(newMeterLimit, newMeterIndex)`

### Web API

- `TextDecoder`
- `TextEncoder`
- `clearImmediate(id)`
- `clearInterval(id)`
- `clearTimeout(id)`
- `setImmediate(callback)`
- `setInterval(callback, delay)`
- `setTimeout(callback, delay)`
- `performance.now()`

## Build

### Linux 

	cd ./makefiles/lin
	make

The debug version is built in `$MODDABLE/build/bin/lin/debug`
The release version is built in `$MODDABLE/build/bin/lin/release `

### macOS 

	cd ./xs/makefiles/mac
	make
	
The debug version is built in `$MODDABLE/build/bin/mac/debug`
The release version is built in `$MODDABLE/build/bin/mac/release `
	
### Windows 

	cd .\xs\makefiles\win
	build
	
The debug version is built in `$MODDABLE/build/bin/win/debug`
The release version is built in `$MODDABLE/build/bin/win/release `

## Usage

	xsnap [-h] [-v]
			[-d <snapshot>] [-r <snapshot>] [-w <snapshot>] 
			[-i <interval>] [-l <limit>] [-p]
			[-e] [-m] [-s] strings...

- `-h`: print this help message
- `-v`: print XS version
- `-d <snapshot>`: dump snapshot to stderr 
- `-r <snapshot>`: read snapshot to create the XS machine 
- `-w <snapshot>`: write snapshot of the XS machine at exit
- `-i <interval>`: metering interval (defaults to 1) 
- `-l <limit>`: metering limit (defaults to none) 
- `-p`: prefix `print` output with metering index
- `-e`: eval `strings`
- `-m`: `strings` are paths to modules
- `-s`: `strings` are paths to scripts

Without `-e`, `-m`, `-s`, if the extension is `.mjs`, strings are paths to modules, else strings are paths to scripts.

## Examples

Add the debug or release directory here above to your path. 

### helloworld

	cd ./examples/helloworld
	xsnap before.js -w snapshot.xsm
	xsnap -r snapshot.xsm after.js
	
The simplest example to see if the build was successful...

### values

	cd ./examples/values
	xsnap before.js -w snapshot.xsm
	xsnap -r snapshot.xsm after.js

Just to test how various JavaScript values survive the snapshot.
	
### generators

	cd ./examples/generators
	xsnap before.js -w snapshot.xsm
	xsnap -r snapshot.xsm after.js

Generators can be iterated before writing and after reading the snapshot.

### promises

	cd ./examples/promises
	xsnap before.js -w snapshot.xsm
	xsnap -r snapshot.xsm after.js

Promises can be part of a snapshot. And `then` can be used before writing and after reading the snapshot

### proxy

	cd ./examples/proxy
	xsnap before.js -w snapshot.xsm
	xsnap -r snapshot.xsm after.js

Just to test how a `Proxy` instance, its target and its handler survive the snapshot.

### weaks

	cd ./examples/weaks
	xsnap before.js -w snapshot.xsm
	xsnap -r snapshot.xsm after.js

`WeakMap`, `WeakSet`, `WeakRef` and `FinalizationRegistry` instances can be defined before writing the snapshot and used after reading the snapshot.

### modules

Use the `-m` option for modules 

	cd ./examples/modules
	xsnap -m before.js -w snapshot.xsm
	xsnap -r snapshot.xsm -m after.js

Modules imported before writing the snapshot are available after reading the snapshot.

### metering

Use the `-l` option to limit the number of byte codes that can be executed. 

	cd ./examples/metering
	xsnap test.js -l 10000
	
The test prints numbers and exits when too many byte codes have been executed.

	...
	473
	474
	too much computation

Use the `-i` option to change how often XS asks the runtime if the limit has been reached.

	xsnap test.js -l 10000 -i 1000

There is a performance gain but a precision lost.

	...
	476
	477
	too much computation

### metering-built-ins

Use the `-p` option to prefix `print` output with the metering index. 

	cd ./examples/metering-built-ins
	xsnap test.js -p

The tests builds, sorts and reverses an array of 100 random numbers. Observe the metering index around `sort` and `reverse`.

	...
	[3935] 99 0.4153946155753893
	[3957] sort
	[11266] reverse
	[16260] 0 0.000007826369259425611
	...


