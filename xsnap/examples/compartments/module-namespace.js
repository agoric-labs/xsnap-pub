const modules = {
	a: new StaticModuleRecord({ source:`
		export const foo = "foo";
	`}),
};
const c1 = new Compartment({}, {},
	{
		resolveHook(specifier, refererSpecifier) {
			return specifier;
		},
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
