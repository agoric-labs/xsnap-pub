# xsnap-worker

`sources/xsnap-worker.c` contains a variant of `xsnap` which accepts execution commands over a file descriptor. It is designed to function as a "JS coprocessor", driven by a parent process (which can be written in any language). The parent does a fork+exec of `xsnap-worker`, then writes netstring-formatted commands to the child. The child executes those commands (evaluating JS or delivering the command to a handler function), possibly emitting one or more requests to the parent during execution, then finally finishes the command and writing a status to the parent (including metering information).

By default, the process starts from an empty JS environment (not even SES or `lockdown`). If the child is started with a `-r SNAPSHOTFILENAME` argument, it will start from a previously-written heap snapshot instead.

The launch arguments are:

* `-h`: print this help message
* `-i <interval>`: set the metering interval (TODO: what does this mean?)
* `-l <limit>`: limit each delivery to `<limit>` computrons
* `-p`: print the current meter count before every `print()`
* `-r <snapshot filename>`: launch from a snapshot file, instead of an empty environment
* `-s SIZE`: set `parserBufferSize`, in kiB (1024 bytes)
* `-v`: print the `xsnap` version and exit with rc 0
* All `argv` strings that do not start with a hyphen are ignored. This allows the parent to include dummy no-op arguments to e.g. label the worker process with a vat ID and name, so admins can use `ps` to distinguish between workers being run for different purposes.

Once started, the process listens on file descriptor 3, and will write to file descriptor 4. The process will perform a blocking read on fd3 until a complete netstring is received. The first character of the body of this netstring indicates what command to execute, with the remainder of the body as the command's payload. The commands are:

* `R` (isReady): writes `netstring(".")` (the body is a single period) to fd4, to acknowledge that the worker is running
* `e` (evaluate): evaluate the body in the top-level JS environment
* `?` (command): feed the body (as a JS `String`) to the registered `handleCommand(body)` handler
  * for both `e` and `?`, execution continues until both the `setImmediate` and the ready-promise-callback queues are empty (the vat is "quiescent")
  * if evaluation/`handleCommand()` throws an error (and the evaluated code does not catch it), the worker writes `!${toString(err)}` to fd4
  * if successful, the result should be an ArrayBuffer, or an object with a `.result` property that is an ArrayBuffer
    * anything else will yield an empty response string
  * the worker writes a netstring with the following body to fd4:
    * `.${meterObj}\1${result}`
    * the `.` prefix indicates success
    * "meterObj" is a JSON-parseable record, with string keys and numeric values, currently containing `{ currentHeapCount, compute, allocate }`
      * `currentHeapCount` is the number of bytes allocated by the JS engine (`fxGetCurrentHeapCount()`), it never goes down
      * `compute` is the number of computrons used during the evaluation/`handleCommand()` (`meterIndex)`
      * `allocate` is `the->allocatedSpace`
    * the meterObj is separated from the result by a 0x01 byte (i.e. U+0001 if the body is parsed as UTF-8)
    * the `result` field is the ArrayBuffer
* `s` (run script): the body is treated as the filename of a program to run (`xsRunProgramFile`)
* `m` (load module): the body is treated as the name of a module to load (`xsRunModuleFile`). The module must already be defined, perhaps pre-compiled into the `xsnap` executable.
  * for both `s` and `m`, an error writes a terse `!` to fd4, and success writes `.${meterObj}\1` (the same success response as for `e`/`?` but with an empty message: just the metering data)
* `w`: the body is treated as a filename. A GC collection is triggered, and then the JS heap is written to the given filename. The response is `!` or `.${meterObj}\1` as with `s`/`m`
* all other command characters cause the worker to exit

If at any point the computation exceeds one of the following limits, the process will exit with a non-zero (and non-negative) exit code:

* `E_NOT_ENOUGH_MEMORY` (11): when memory allocation uses more than `allocationLimit` bytes (hard-coded to 2GiB in `xsnapPlatform.c`/`fxCreateMachinePlatform()`)
* `E_STACK_OVERFLOW` (12): when the stack exceeds the configured limit (hard-coded in `xsnap-worker.c` as `stackCount` to 4096)
* `E_NO_MORE_KEYS` (16): when the number of "keys" (unique property names) exceeds the limit (hard-coded in `xsnap-worker.c` as `keyCount` to 32000)
* `E_TOO_MUCH_COMPUTATION` (17): when the computation exceeds the `-l` computron limit
