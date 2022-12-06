# Compartments

Revised: February 5, 2022

Warning: These notes are preliminary. Omissions and errors are likely. If you encounter problems, please ask for assistance.

## About

In XS, the real host is the application that creates an XS machine to evaluate scripts and import modules.

Compartments are lightweight virtual hosts that evaluate scripts and import modules separately from the real host and from other compartments. 

Compartments have their own `globalThis` object, their own global lexical scope and their own imported modules. 

By default:

- the `globalThis` object contains only the built-ins,
- the global lexical scope is empty,
- no modules are imported.

Compartments run in the same XS machine:

- Except for `Compartment`, `Function` and `eval`, built-ins are shared. 
- Other objects can be shared thru the **endowments**. 
- Module namespaces can be shared thru the **module map**.

For the sake of security, it is the responsibility of a real host that creates compartments to ensure that shared features are immutable.

### Attenuation

A parent compartment can create child compartments.

	const parent = new Compartment();
	parent.evaluate(`
		const child = new Compartment();
	`);

A compartment can only provide to its child compartments the features provided by its parent compartment (and new features based on the features provided by its parent compartment).

### Module Descriptor

Comparments can load and initialize modules from module descriptors. Like property descriptors, module descriptors are ordinary objects with various forms. 

Static module records are compiled module descriptors and can be used instead of module descriptors. See [StaticModuleRecord](./StaticModuleRecord.md) for details.

## The Compartment Constructor

### Compartment([ endowments, [ moduleMap, [ options ]]])

Returns a compartment object. 

### endowments

The `endowments` parameter is an object that adds properties to the `globalThis` object of the compartment. 

The `Compartment` constructor copies the `endowments` properties using the same behavior as `Object.assign`.

	let getterCount = 0;
	let setterCount = 0;
	const endowments = {
		foo: 0,
		get bar() {
			getterCount++;
			return this.foo;
		},
		set bar(it) {
			setterCount++;
			this.foo = it;
		},
		shared: {
			foo: 0,
			get bar() {
				return this.foo;
			},
			set bar(it) {
				this.foo = it;
			},
		}
	};
	
	const c1 = new Compartment(endowments, {}, {});
	c1.evaluate(`
		foo++;
		bar++;
		shared.foo++;
		shared.bar++;
		globalThis.which = 1;
	`);
	
	const c2 = new Compartment(endowments, {}, {});
	c2.evaluate(`
		foo++;
		bar++;
		shared.foo++;
		shared.bar++;
		globalThis.which = 2;
	`);
	
	print(getterCount); // 2;
	print(setterCount); // 0;
	print(endowments.foo); // 0;
	print(endowments.bar); // 0;
	print(endowments.shared.foo); // 4;
	print(endowments.shared.bar); // 4;
	print(endowments.which); // undefined;
	print(c1.globalThis.foo); // 1;
	print(c1.globalThis.bar); // 1;
	print(c1.globalThis.which); // 1;
	print(c2.globalThis.foo); // 1;
	print(c2.globalThis.bar); // 1;
	print(c2.globalThis.which); // 2;

### moduleMap

The `moduleMap` parameter is an object that initializes the **module map** of the compartment:

- property names are module specifiers,
- property values are module namespaces, module descriptors or module specifiers. 

The `Compartment` constructor copies the `moduleMap` properties using the same behavior as `Object.assign`.

A compartment cannot access module records or module specifiers provided by the module map. A compartment can only access module namespaces that will be loaded and initialized based on such module records or module specifiers.

#### module namespaces

When a module map property value is a module namespace, the module namespace is shared among compartments.

	import * as foo from "./moduleMap-fixture.js";
	
	let getterCount = 0;
	let setterCount = 0;
	const moduleMap = {
		foo,
		get bar() {
			getterCount++;
			return this.foo;
		},
		set bar(it) {
			setterCount++;
			this.foo = it;
		},
	};
	
	const c1 = new Compartment({}, moduleMap, {});
	const fooNS1 = await c1.import("foo");
	const barNS1 = await c1.import("bar");
	
	const c2 = new Compartment({}, moduleMap, {});
	const fooNS2 = await c2.import("foo");
	const barNS2 = await c2.import("bar");
	
	print(getterCount); // 2;
	print(setterCount); // 0;
	print(fooNS1.default()); // 0;
	print(barNS1.default()); // 1;
	print(fooNS2.default()); // 2;
	print(barNS2.default()); // 3;

#### module descriptors

When a module map property value is a module descriptor, the module descriptor is used to load and initialize a new module into the compartment.

	const foo = { source:`
		let foo = 0;
		export default function() {
			return foo++;
		}
	`};
	
	let getterCount = 0;
	let setterCount = 0;
	const moduleMap = {
		foo,
		get bar() {
			getterCount++;
			return this.foo;
		},
		set bar(it) {
			setterCount++;
			this.foo = it;
		},
	};
	
	const c1 = new Compartment({}, moduleMap, {});
	const fooNS1 = await c1.import("foo");
	const barNS1 = await c1.import("bar");
	
	const c2 = new Compartment({}, moduleMap, {});
	const fooNS2 = await c2.import("foo");
	const barNS2 = await c2.import("bar");
	
	print(getterCount); // 2;
	print(setterCount); // 0;
	print(fooNS1.default()); // 0;
	print(barNS1.default()); // 0;
	print(fooNS2.default()); // 0;
	print(barNS2.default()); // 0;

#### module specifiers

When a module map property value is a module specifier, the module specifier is used by the parent compartment to load and initialize a new module into the compartment.

	const uri = import.meta.uri;
	const foo = uri.slice(0, uri.lastIndexOf("/") + 1) + "moduleMap-fixture.js";
	
	let getterCount = 0;
	let setterCount = 0;
	const moduleMap = {
		foo,
		get bar() {
			getterCount++;
			return this.foo;
		},
		set bar(it) {
			setterCount++;
			this.foo = it;
		},
	};
	
	const c1 = new Compartment({}, moduleMap, {});
	const fooNS1 = await c1.import("foo");
	const barNS1 = await c1.import("bar");
	
	const c2 = new Compartment({}, moduleMap, {});
	const fooNS2 = await c2.import("foo");
	const barNS2 = await c2.import("bar");
	
	print(getterCount); // 2;
	print(setterCount); // 0;
	print(fooNS1.default()); // 0;
	print(barNS1.default()); // 0;
	print(fooNS2.default()); // 0;
	print(barNS2.default()); // 0;


### options.globalLexicals

The `globalLexicals` option is an object that initializes the global lexical scope of the compartment:

- property names become variable or constant names,
- property values become variable or constant values. 

If the property is writable, a variable (`let`) is created, else a constant (`const`) is created.

	let getterCount = 0;
	let setterCount = 0;
	const globalLexicals = {
		foo: 0,
		get bar() {
			getterCount++;
			return this.foo;
		},
		set bar(it) {
			setterCount++;
			this.foo = it;
		},
		shared: {
			foo: 0,
			get bar() {
				return this.foo;
			},
			set bar(it) {
				this.foo = it;
			},
		}
	};
	
	const c1 = new Compartment({ print }, {}, { globalLexicals});
	c1.evaluate(`
		foo++;
		bar++;
		shared.foo++;
		shared.bar++;
	`);
	
	const c2 = new Compartment({ print }, {}, { globalLexicals });
	c2.evaluate(`
		foo++;
		bar++;
		shared.foo++;
		shared.bar++;
	`);
	
	print(getterCount); // 2;
	print(setterCount); // 0;
	print(globalLexicals.foo); // 0;
	print(globalLexicals.bar); // 0;
	print(globalLexicals.shared.foo); // 4;
	print(globalLexicals.shared.bar); // 4;
	c1.evaluate(`
		print(foo); // 1;
		print(bar); // 1;
	`);
	print(c1.globalThis.foo); // undefined;
	print(c1.globalThis.bar); // undefined;
	c2.evaluate(`
		print(foo); // 1;
		print(bar); // 1;
	`);
	print(c2.globalThis.foo); // undefined;
	print(c2.globalThis.bar); // undefined;

### options.moduleMapHook(specifier)

The `moduleMapHook` option is a function that takes a module specifier and returns a module namespace, a module descriptor, a module specifier or `undefined`.

The `moduleMapHook` function is only called directly or indirectly by `Compartment.prototype.import` or `Compartment.prototype.importNow` if the `moduleMap` has no property for the specifier.

The default `moduleMapHook` function returns `undefined`.

#### module namespaces

When the `moduleMapHook` function returns a module namespace, the module namespace is shared among compartments.

	import * as foo from "./moduleMap-fixture.js";
	
	function moduleMapHook(specifier) {
		if ((specifier == "foo") || (specifier == "bar"))
			return foo;
	}
	
	const c1 = new Compartment({}, {}, { moduleMapHook });
	const fooNS1 = await c1.import("foo");
	const barNS1 = await c1.import("bar");
	
	const c2 = new Compartment({}, {}, { moduleMapHook });
	const fooNS2 = await c2.import("foo");
	const barNS2 = await c2.import("bar");
	
	print(fooNS1.default()); // 0;
	print(barNS1.default()); // 1;
	print(fooNS2.default()); // 2;
	print(barNS2.default()); // 3;

#### module descriptors

When the `moduleMapHook` function returns a module descriptor, the module descriptor is used to load and initialize a new module into the compartment.

	const foo = { source:`
		let foo = 0;
		export default function() {
			return foo++;
		}
	`};
	
	function moduleMapHook(specifier) {
		if ((specifier == "foo") || (specifier == "bar"))
			return foo;
	}
	
	const c1 = new Compartment({}, {}, { moduleMapHook });
	const fooNS1 = await c1.import("foo");
	const barNS1 = await c1.import("bar");
	
	const c2 = new Compartment({}, {}, { moduleMapHook });
	const fooNS2 = await c2.import("foo");
	const barNS2 = await c2.import("bar");
	
	print(fooNS1.default()); // 0;
	print(barNS1.default()); // 0;
	print(fooNS2.default()); // 0;
	print(barNS2.default()); // 0;

#### module specifiers

When the `moduleMapHook` function returns a module specifier, the module specifier is used by the parent compartment to load and initialize a new module into the compartment.

	const uri = import.meta.uri;
	const foo = uri.slice(0, uri.lastIndexOf("/") + 1) + "moduleMap-fixture.js";
	
	function moduleMapHook(specifier) {
		if ((specifier == "foo") || (specifier == "bar"))
			return foo;
	}
	
	const c1 = new Compartment({}, {}, { moduleMapHook });
	const fooNS1 = await c1.import("foo");
	const barNS1 = await c1.import("bar");
	
	const c2 = new Compartment({}, {}, { moduleMapHook });
	const fooNS2 = await c2.import("foo");
	const barNS2 = await c2.import("bar");
	
	print(fooNS1.default()); // 0;
	print(barNS1.default()); // 0;
	print(fooNS2.default()); // 0;
	print(barNS2.default()); // 0;

### options.loadHook(specifier)

The `loadHook` option is an asynchronous function that takes a module specifier and returns a promise to a module descriptor.

The `loadHook` function is only called directly or indirectly by `Compartment.prototype.import` if the `moduleMap` has no property for the specifier and if the `moduleMapHook` returns `undefined` for the specifier.

The `loadHook` function is useful if the module descriptor is unavailable when constructing the compartment, or to create a module descriptor dynamically. 

	const c = new Compartment({}, {}, {
		async loadHook(specifier) {
			return { source:`export default import.meta.uri`, meta: { uri: specifier } };
		}
	})
	const nsa = await c.import("a");
	print(nsa.default); // a
	const nsb = await c.import("b");
	print(nsb.default); // b

The default `loadHook` function throws a `TypeError`.

### options.loadNowHook(specifier)

The `loadNowHook` option is a function that takes a module specifier and returns a module descriptor.

The `loadNowHook` function is only called directly or indirectly by `Compartment.prototype.importNow` if the `moduleMap` has no property for the specifier and if the `moduleMapHook` returns `undefined` for the specifier.

The `loadNowHook ` function is useful if the module descriptor is unavailable when constructing the compartment, or to create a module descriptor dynamically. 

	const c = new Compartment({}, {}, {
		loadNowHook(specifier) {
			return { source:`export default import.meta.uri`, meta: { uri: specifier } };
		}
	})
	const nsa = c.importNow("a");
	print(nsa.default); // a
	const nsb = c.importNow("b");
	print(nsb.default); // b

The default `loadNowHook` function throws a `TypeError`.

### options.resolveHook(importSpecifier, referrerSpecifier)

The `resolveHook` option is a function that takes an import specifier and a referrer specifier and returns a module specifier.

Typically the `resolveHook` function combine a relative path and an absolute path or uri into an absolute path or uri. But the `resolveHook` function can build arbitrary specifiers.

	const c = new Compartment({}, {
		a: { source:`
			import b from "b";
			export default "a" + b;
		`},
		b_a: { source:`
			import c from "c";
			export default "b" + c;
		`},
		c_b_a: { source:`
			export default "c";
		`},
	}, {
		resolveHook(importSpecifier, referrerSpecifier) {
			return importSpecifier + "_" + referrerSpecifier;
		}
	})
	const nsa = await c.import("a");
	print(nsa.default); // abc

The default `resolveHook` function calls the `resolveHook` function of the parent compartment.

## Properties of the Compartment Prototype

### get Compartment.prototype.globalThis

Returns the `globalThis` object of the compartment.

Except for `Compartment`, `Function` and `eval`, built-ins are shared.

	const c = new Compartment();
	const globals = c.globalThis;
	const names = Object.getOwnPropertyNames(globals);
	const exceptions = [ "Compartment", "Function", "eval", "NaN", "global", "globalThis" ];
	for (let name of names) {
		const actual = globalThis[name] === globals[name];
		const expected = (exceptions.indexOf(name) >= 0) ? false : true;
		if (actual != expected)
			print(name);
	}

### Compartment.prototype.evaluate(script)

Evaluates the script in the compartment and returns its completion value.

Scripts are evaluated in strict mode, with the global lexical scope of the compartment, and with `this` being the `globalThis` object of the compartment.

	const c = new Compartment();
	let result;
	result = c.evaluate(`
		this.foo = 0;
		let bar = 0;
		bar = foo++
	`);
	print(result); // 0
	result = c.evaluate(`
		bar = foo++
	`);
	print(result); // 1
	print(c.globalThis.foo); // 2

### Compartment.prototype.import(specifier)

Asynchronously loads and initializes a module into the compartment and returns a promise to its namespace.

The specifier is used to find a module:

- in the modules already imported by the compartment,
- else in the compartment module map,
- else by the compartment `moduleMapHook`,
- else by the compartment `loadHook`.

If necessary, the compartment loads and initialize the module. The `import` declarations or calls are resolved by the compartment `resolveHook` then follow the same process.. Eventually the promise is fulfilled.

	const c = new Compartment({}, {
		"/a": { source:`
			let a = 0;
			export default function() {
				return a++;
			}
		`},
		"/b": { source:`
			const nsa = await import("./a");
			export default function() {
				const a = nsa.default();
				return a * a;
			}
		`},
	});
	const nsa = await c.import("/a");
	const nsb = await c.import("/b");
	print(nsa.default()); // 0;
	print(nsb.default()); // 1;
	print(nsa.default()); // 2;

### Compartment.prototype.importNow(specifier)

Synchronously loads and initializes a module into the compartment and returns its namespace.

The specifier is used to find a module:

- in the modules already imported by the compartment,
- else in the compartment module map,
- else by the compartment `moduleMapHook`,
- else by the compartment `loadNowHook`.

If necessary, the compartment loads and initialize the module. The `import` declarations are resolved by the compartment `resolveHook` then follow the same process.

- The motivation behind `importNow` is to avoid unnecessary delays and to allow applications to get rid of the promise machinery. Such applications support neither `async` nor `await` nor the `import` call. The implementation of `importNow` must not depend on the availability of promises.

- Applications that support promises, `async`, `await` and the `import` call can still use `importNow`. That is expected to be rare but, then, `importNow` throws when initializing a module with top level `await`.

### Compartment.prototype.module(specifier)

Returns a module namespace without importing the module:

- Before the module is loaded and initialized, getting properties from the returned module namespace throws a `ReferenceError`.

- After the module is loaded and initialized, directly by `Compartment.prototype.import` or `Compartment.prototype.importNow` or indirectly by an `import` declaration or call, the returned module namespace behaves normally.

The `module` method allows to share module namespaces across compartments without loading and initializing modules.

	const c1 = new Compartment({}, {
		a: {source:`
			let x = 0;
			export default function() { return x++; };
		`}
	});
	const nsa1 = c1.module("a");
	const c2 = new Compartment({}, {
		a: nsa1
	});
	const nsa2 = await c2.import("a");
	print(nsa1.default()); // 0
	print(nsa2.default()); // 1
	print(nsa1 === nsa2); // true

### Compartment.prototype.[@@toStringTag]

The initial value of this property is the `"Compartment"` string.

## Module Namespace Exotic Objects

The `Compartment.prototype.module` method give scripts access to module namespace exotic objects from uninitialized modules.

In the [specification](https://tc39.es/ecma262/#sec-module-namespace-exotic-objects), `O.[[Exports]]` has to be replaced by an abstract operation that throws a `ReferenceError` if the module is uninitialized, else returns `O.[[Exports]]`.

The change affects `[[GetOwnProperty]]`, `[[DefineOwnProperty]]`,  `[[HasProperty]]`, `[[Get]]`, `[[Delete]]` and `[[OwnPropertyKeys]]`. 

But the change does not affect `[[GetPrototypeOf]]`, `[[SetPrototypeOf]]`, `[[IsExtensible]]`,  `[[PreventExtensions]]` and `[[Set]]` which do not depend on the state of the module.

	const modules = {
		a: { source:`
			export const foo = "foo";
		`},
	};
	const c1 = new Compartment({}, {},
		{
			async loadHook(specifier) {
				return modules[specifier];
			},
		},
	);
	const nsa1 = c1.module("a");
	try { 
		print(nsa1.foo);
	}
	catch(e) {
		print(e); // ReferenceError: print: module not initialized yet
	}
	try {
		print(nsa1.bar);
	}
	catch(e) {
		print(e); // ReferenceError: print: module not initialized yet
	}
	const nsa2 = await c1.import("a");
	print(nsa2.foo); // foo
	print(nsa2.bar); // undefined
	print(nsa1 === nsa2); // true
