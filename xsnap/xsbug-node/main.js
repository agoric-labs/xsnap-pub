const fs = require("fs");
const os = require('node:os');
const { spawn } = require('node:child_process');
const { Machine } = require('./xsbug-machine.js');

const encoder = new TextEncoder();
const decoder = new TextDecoder();

function writeNetString(output, string) {
	const message = encoder.encode(string);
	const messageLength = message.length;
	const prefix = encoder.encode(messageLength.toString() + ':');
	const prefixLength = prefix.length;
    const data = new Uint8Array(prefixLength + messageLength + 1);
    data.set(prefix, 0);
    data.set(message, prefixLength);
    data[prefixLength + messageLength] = ','.charCodeAt(0);
//  console.log(`>>> ${messageLength}:${string}`);
	output.write(data);
}

const platforms = {
	linux: 'lin',
	darwin: 'mac',
};
const xsnapProcess = spawn(`../build/bin/${platforms[os.platform()]}/debug/xsnap-worker`, [], {
	stdio: ['ignore', 'inherit', 'inherit', 'pipe', 'pipe', 'pipe', 'pipe'],
});
xsnapProcess.once('exit', (code, signal) => {
	console.log(`exit ${code} ${signal}`);
});
const xsnapOutput = xsnapProcess.stdio[3];
const xsnapInput = xsnapProcess.stdio[4];
const xsbugOutput = xsnapProcess.stdio[5];
const xsbugInput = xsnapProcess.stdio[6];

const machine = new Machine(xsbugInput, xsbugOutput);
machine.on('instruments', (instruments) => {
	machine.reportInstruments(instruments);
});
machine.on('profile', (profile) => {
	profile.reportHits();
	fs.writeFileSync("./test.cpuprofile", profile.toString());
});
machine.doStartProfiling();

xsnapInput.on('data', (data) => {
// 	console.log(`<<< ${data}`);
	machine.doStopProfiling();
});
const buffer = fs.readFileSync("./test.js");
writeNetString(xsnapOutput, `e{${buffer.toString()}}`);
