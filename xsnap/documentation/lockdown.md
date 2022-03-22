# Lockdown

Revised: March 1, 2022

Warning: These notes are preliminary. Omissions and errors are likely. If you encounter problems, please ask for assistance.

## lockdown()

The `lockdown` function freezes all JS built-ins.

- Once the `lockdown` function has been called, the `harden` function can be called.
- Snapshots preserve the state of the machine, pre and post lockdown.  

### Surgery

Besides freezing all JS built-ins, the `lockdown` function also modifies some built-ins as specified by Secure ECMAScript.

- The `constructor` property of the prototypes of function, generator and compartment instances are replaced by a function that always throws.

In compartments created after the execution of the `lockdown` function:

- The `Date` constructor is replaced by its secure version, which throws when called without arguments.
- The `Date.now` and `Math.random` functions are replaced by their secure versions, which always throw.
- The `Function` constructor, the `Compartment` constructor and the `eval` function created for the compartment are frozen.

Here is a script to test such modifications. Firsly the script defines a string to evaluate in different contexts:

	const tests = `
		let result = "";
		function test(f) {
			try {
				f();
				result += 'Y';
			}
			catch {
				result += 'N';
			}
		}
		test(()=>new test.constructor(""));
		test(()=>new Date());
		test(()=>Date.now());
		test(()=>Math.random());
		print(result);
	`

Before lockdown:

	eval(tests); // YYYY
	
	const before = new Compartment({ print });
	before.evaluate(tests); // YYYY

After lockdown:

	lockdown();
	
	eval(tests); // NYYY
	
	const after = new Compartment({ print });
	after.evaluate(tests); // NNNN

## harden(o)

The `harden` function freezes the passed object, then hardens its prototypes and all its properties.

- The `harden` function throws if the `lockdown` function has not been called.
- The `harden` function does nothing if the passed object is no object.
- In order to respect ordinary and exotic behaviors, the `harden` function uses standard traps like `[[GetPrototypeOf]]`, `[[OwnPropertyKeys]]` and `[[GetOwnProperty]]`. Proxies can execute some JS code while hardening like while freezing.
- If the `harden` function fails, for instance if a proxy throws an exception, no traversed objects are hardened. But objects that have been frozen before the exception remain frozen.
- The `harden` function returns the passed object.
- XS uses an internal flag to mark hardened instances. It is the same flag that is used to mark instances in the read-only heap prepared by the XS linker for embedded devices.

### Traversal Order

The `harden` function traverses objects breadth first. The prototype of an object, if any, is treated like the first property of the object.

Here is a script to test the traversal order. Firstly the script defines a function that creates a proxy to observe `Object.freeze` thru the `[[PreventExtensions]]` trap: 

	let order = "";
	const handler = {
	  preventExtensions(target) {
	    order += target.name;
	    return Reflect.preventExtensions(target);
	  }
	}
	function createObservable(proto, name) {
	  const target = Object.create(proto);
	  target.name = name;
	  return new Proxy(target, handler);
	}

Then the script uses such function to build this hierarchy:
	
![](./hardenit.png)
	
	const a = createObservable(null, "a");
	a.e = createObservable(null, "e");
	a.n = createObservable(null, "n");
	const h = createObservable(a, "h");
	h.r = createObservable(null, "r");
	h.r.i = createObservable(null, "i");
	h.r.t = createObservable(null, "t");
	h.d = createObservable(null, "d");
	
When hardening `h`, the script prints `hardenit`!
	
	lockdown();
	harden(h);
	print(order);

### override mistake

I did nothing to workaround the so-called "override mistake”:

	lockdown();
	const x = harden({foo: 88});
	const y = Object.create(x);
	y.foo = 99; // fail

In fact I tend to agree with Allen Wirfs-Brock: [it is not a mistake](https://web.archive.org/web/20141230041441/http://wiki.ecmascript.org/doku.php?id=strawman:fixing_override_mistake).

Even if you use JS as a protoype based language, it is always possible to override:

	Object.defineProperty(y, "foo", { value:99 });

And once you use JS as a class based language, there are no problems:

	lockdown();
	class C {
		constructor() {
		}
		test() {
			print("C");
		}
	}
	class D extends harden(C) {
		constructor() {
			super();
		}
		test() {
			print("D");
		}
	}
	const d = new D;
	d.test();

To be discussed, of course!

## petrify(o)

The experimental `petrify` function make private fields and internal properties of the passed object immutable.

Here are the objects modified by the `petrify` function:

| Instances of | Internal Property |
|----|----|
| `ArrayBuffer` | `[[ArrayBufferData]]` |
| `Date` | `[[DateValue]]` |
| `Map` | `[[MapData]]` |
| `Set` | `[[SetData]]` |
| `WeakMap` | `[[WeakMapData]]` |
| `WeakSet` | `[[WeakSetData]]` |

The behavior of the `petrify` function is expected to evolve.

### Private Fields

With `Object.freeze` private fields remain mutable. With `petrify` private fields become immutable

	class C {
		#f
		constructor(f) {
			this.#f = f;
		}
		get f() {
			return this.#f;
		}
		set f(it) {
			this.#f = it;
		}
	}
	const c	= new C(0);
	Object.freeze(c);
	c.f = 1;
	print(c.f); // 1
	petrify(c);
	try { c.f = 2; } catch { }
	print(c.f); // 1

### Internal Properties

With `Object.freeze` dates remain mutable. With `petrify` dates become immutable

	const d = new Date(1993);
	Object.freeze(d);
	d.setFullYear(2022);
	print(d.getFullYear()); // 2022
	petrify(d);
	try { d.setFullYear(1961); } catch { }
	print(d.getFullYear()); // 2022

## mutabilities(o)

The experimental `mutabilities` function checks the immutability of the passed object.

- The `mutabilities` function checks the passed object, then checks its prototypes and all its properties.
- The `mutabilities` function traverses objects breadth first. The prototype of an object, if any, is treated like the first property of the object.
- The `mutabilities` function does not use standard internal methods so JS code is never executed while checking.
- The `mutabilities` function returns a sorted array of strings: the list of all mutable property paths, or the empty array if all properties are immutable.

The behavior of the `mutabilities` function is expected to evolve.

### Properties

The `mutabilities` function checks properties in objects:

	lockdown();
	const o = {
		foo:88,
	};
	print(mutabilities(o));

prints:

	.foo

But:

	lockdown();
	const p = harden({
		foo:88,
	});
	print(mutabilities(p));

prints nothing since the `p` is hardened.

### Closures

The `mutabilities` function checks closures in functions environments:

	lockdown();
	let v = 0;
	const o = {
		oops(i) {
			return v + i;
		},
	};
	harden(o);
	print(mutabilities(o));

prints:
	
	.oops[[Environment]].v

since `v` is variable.

But:

	lockdown();
	const c = 0;
	const p = {
		oops(i) {
			return c + i;
		},
	};
	harden(p);
	print(mutabilities(p));

prints nothing since `c` is constant.

### Modules

The `mutabilities` function checks module namespaces:

	lockdown();
	const foo = { source:`
		let foo = 0;
		export default harden(function() {
			return foo++;
		})
		export const bar = {};
	`};
	const c = new Compartment({ harden }, { foo });
	const ns = await c.import("foo");
	print(mutabilities(ns).join("\n"));

prints:

	.bar[[Extensible]]
	.default[[Environment]].foo

since the `bar` export is extensible and the `default` export is a function that uses a variable.

### Globals

The `mutabilities` function checks globals in functions byte codes:

	lockdown();
	globalThis.x = 0;
	y = 0;
	var z = 0;
	function f() {
		return x + y + z;
	}
	print(mutabilities(harden(f)).join("\n"));

prints:

	[[GlobalEnvironment]].x
	[[GlobalEnvironment]].y
	[[GlobalEnvironment]].z
	
But:

	lockdown();
	Object.defineProperty(globalThis, "ok",
				{ value:0, configurable:false, writable:false });
	function g() {
		return ok;
	}
	print(mutabilities(harden(g)));
	
prints nothing since `ok` is immutable.	
	
### Private Fields and Internal Properties

The `mutabilities` function also reports mutable private fields and internal propertes, which are currently not frozen by the `harden` function. 

	class C {
		#f
		constructor(f) {
			this.#f = f;
		}
	}
	const values = {
		c: new C(0),
		date: new Date('1993-3-28'),
		dataView: new DataView(new ArrayBuffer(10)),
		map: new Map([["a", 0],["b", 1],["c", 2],["d", 2]]),
		set: new Set([0, 1, 2, 3]),
		typedArray: new Uint8Array([0, 1, 2, 3]),
		weakMap: new WeakMap(),
		weakSet: new WeakSet(),
	};
	harden(values);
	print(mutabilities(values).join("\n"));

prints

	.c.#f
	.date[[DateValue]]
	.dataView[[ViewedArrayBuffer]][[ArrayBufferData]]
	.map[[MapData]]
	.set[[SetData]]
	.typedArray[[ViewedArrayBuffer]][[ArrayBufferData]]
	.weakMap[[WeakMapData]]
	.weakSet[[WeakSetData]]

