# Static Module Records

Revised: January 6, 2022

Warning: These notes are preliminary. Omissions and errors are likely. If you encounter problems, please ask for assistance.

## About

A static module record prepares a module descriptor to be loaded and initialized as a new module into a compartment.

> Compartments can use module descriptors directly instead of indirectly thru static module records.

A static module record also provides access to the module bindings.

## The StaticModuleRecord Constructor

### StaticModuleRecord(descriptor)

Returns a static module record object. Currently XS supports two kinds of module descriptor.

### descriptor.source

The `source` module descriptor constructs a static module record from a string that is parsed and compiled as a module. 

	const smr = new StaticModuleRecord({ source: `
		import x from "mod";
		export let y = x;
	` });

Obviously, such module descriptor requires a JavaScript parser at runtime.

### descriptor.bindings & descriptor.initialize

The `bindings` & `initialize` module descriptor constructs a static module record from module bindings and a function to initialize the module.

	const smr = new StaticModuleRecord({ 
		bindings:[
			{ import: "x", from: "mod" },
			{ export: "y" },
		], 
		initialize($, meta) {
			$.y = $.x;
		} 
	});
	
The module bindings allow the same expressiveness as import and export declarations without requiring a JavaScript parser at runtime. The `StaticModuleRecord` constructor checks the declarations and can throw a `SyntaxError`.

Once the module is loaded and linked, the compartment calls the `initialize` function with two arguments:

- `$`: the [module environment record](https://tc39.es/ecma262/#sec-module-environment-records),
- `meta`: the module `import.meta`.

> Separating exports and imports to call `initialize` would be artificial. No such separation exists during the execution of a module body. So XS would have to create new objects and new properties while the module environment record is already available. 

The module environment record is sealed: 

- no properties can be created or deleted,
- export properties are writable,
- import properties are read-only,
- there are no reexports properties.

Like a module body, the `initialize` function can be asynchronous.

	const smr = new StaticModuleRecord({ 
		bindings:[
			{ import: "x", from: "mod" },
			{ export: "y" },
			{ export: "z", from: "mod" },
		], 
		async initialize($) {
			try {
				$.z = 0;
			}
			catch {
				// not extensible
			}
			try {
				delete $.y;
			}
			catch {
				// not allowed
			}
			try {
				$.x = 0;
			}
			catch {
				// constant
			}
		} 
	});


## Properties of the StaticModuleRecord Prototype

### get StaticModuleRecord.prototype.bindings

Returns the module bindings.

XS stores module bindings into a private compressed form. To avoid the multiplication of getters going thru the same process, there is only  one `bindings` getter that decompresses and publishes the module bindings into an array.

There are many forms of module bindings. See the
[imports](https://tc39.es/ecma262/#table-import-forms-mapping-to-importentry-records) and [exports](https://tc39.es/ecma262/#table-export-forms-mapping-to-exportentry-records) tables. If bindings would be returned as strings, the process would have to create strings and some strings would require to be parsed again.

So instead of strings, bindings are JSON-like objects with `export`, `import`, `as`, `from` properties, the values of the properties are strings that already exist.

For instance:

	const smr = new StaticModuleRecord({ source:`
		export var v0;	
		export default 0
		export { v0 as w0 };	
	
		import v1 from "mod";
		import * as ns1 from "mod";	
		import { x1 } from "mod";	
		import { v1 as w1 } from "mod";	
	
		export { x2 } from "mod";
		export { v2 as w2 } from "mod";
		export * from "mod";
		export * as ns2 from "mod";
	`});
	
	print(JSON.stringify(smr.bindings, null, "\t"));

Prints to the console:
	
	[
		{
			"export": "v0"
		},
		{
			"export": "default"
		},
		{
			"export": "v0",
			"as": "w0"
		},
		{
			"import": "default",
			"as": "v1",
			"from": "mod"
		},
		{
			"import": "*",
			"as": "ns1",
			"from": "mod"
		},
		{
			"import": "x1",
			"from": "mod"
		},
		{
			"import": "v1",
			"as": "w1",
			"from": "mod"
		},
		{
			"export": "x2",
			"from": "mod"
		},
		{
			"export": "v2",
			"as": "w2",
			"from": "mod"
		},
		{
			"export": "*",
			"from": "mod"
		},
		{
			"export": "*",
			"as": "ns2",
			"from": "mod"
		}
	]


### StaticModuleRecord.prototype.[@@toStringTag]

The initial value of this property is the `"StaticModuleRecord"` string.
