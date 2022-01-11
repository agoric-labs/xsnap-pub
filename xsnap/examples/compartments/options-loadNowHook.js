const c = new Compartment({}, {}, {
	loadNowHook(specifier) {
		return new StaticModuleRecord({ source:`export default "${specifier}"` });
	},
	resolveHook(specifier) {
		return specifier;
	}
})
const nsa = c.importNow("a");
print(nsa.default); // a
const nsb = c.importNow("b");
print(nsb.default); // b
