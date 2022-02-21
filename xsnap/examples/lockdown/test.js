"use strict";
let x = 0;
function f() {
	eval("x = 1; y = 2");
	return x;
}
lockdown();
harden(f);
print(purify(f));
f();
print(x, y);

const dv = DataView(new ArrayBuffer(10));
harden(dv, "purify");
harden(dv.buffer, "purify");
