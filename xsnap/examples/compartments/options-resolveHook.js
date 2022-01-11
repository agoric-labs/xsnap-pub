const c = new Compartment({}, {
	a$: new StaticModuleRecord({ source:`
		import b from "b";
		export default "a" + b;
	`}),
	b_a$: new StaticModuleRecord({ source:`
		import c from "c";
		export default "b" + c;
	`}),
	c_b_a$: new StaticModuleRecord({ source:`
		export default "c";
	`}),
}, {
	resolveHook(specifier, referrerSpecifier) {
		return referrerSpecifier ? specifier + "_" + referrerSpecifier : specifier + "$";
	}
})
const nsa = await c.import("a");
print(nsa.default); // abc
