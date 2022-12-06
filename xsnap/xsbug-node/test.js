function test(n) {
	const result = [];
	while (n > 0) {
		const foo = x => x ** x;
		result.push(foo(n));
		n--;
	}
	result.sort((a,b) => a - b);
	return result
}
test(1024*256);
//# sourceURL=test.js
