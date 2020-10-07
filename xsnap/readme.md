# xsnap
Revised: September 30, 2020

Warning: These notes are preliminary. Omissions and errors are likely. If you encounter problems, please ask for assistance.

## About

`xsnap` is a custom XS runtime that read and write XS snapshots. See [XS snapshots](./documentation/XS Snapshots.md) for details and the C programming interface.

`xsnap` can create a machine from scratch or from a snapshot. In both cases, the machine can be frozen prior to scripts or modules execution.

When a machine is frozen, all intrinsics become immutable. Similarly to the [XS linker](https://github.com/Moddable-OpenSource/moddable/blob/public/documentation/xs/XS%20linker%20warnings.md), `xsnap` reports warning about mutable closures or objects.
`xsnap` cannot write a snapshot if the machine has been frozen.

The intent is to evolve xsnap by adding or removing features to fit Agoric's project.

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

	xsnap [-h] [-e] [-f] [-m] [-r <snapshot>] [-s] [-v] [-w <snapshot>] strings...

- `-h`: print this help message
- `-e`: eval `strings`
- `-f`: freeze the XS machine
- `-m`: `strings` are paths to modules
- `-r <snapshot>`: read snapshot to create the XS machine 
- `-s`: `strings` are paths to scripts
- `-v`: print XS version
- `-w <snapshot>`: write snapshot of the XS machine at exit

The `-f` and `-w` options are incompatible.

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

### compartments

Use the `-f` option to freeze the machine in order to use compartments. 

	cd ./examples/compartments
	xsnap before.js -w snapshot.xsm
	xsnap -r snapshot.xsm -f after.js

`xsnap` warns about mutable closures and objects.

	### warning() l: no const
	0 0 0 0
	undefined undefined undefined 1
