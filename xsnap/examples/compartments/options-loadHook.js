const c = new Compartment({}, {}, {
	async loadHook(specifier) {
		return new StaticModuleRecord({ source:`export default "${specifier}"` });
	},
	resolveHook(specifier) {
		return specifier;
	}
})
const nsa = await c.import("a");
print(nsa.default); // a
const nsb = await c.import("b");
print(nsb.default); // b
