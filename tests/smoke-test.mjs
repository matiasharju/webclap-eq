// Minimal headless WCLAP host for automated checks (no audio output).
//
//     node tests/smoke-test.mjs dist/webclap-eq-airwindows.wclap.tar.gz
//
// Loads a bundle like a DAW would, then exercises: entry/factory, parameters, audio processing
// (measures the frequency response), state save/load, and the webview UI messages.

import fs from 'node:fs';
import zlib from 'node:zlib';

const bundlePath = process.argv[2] ?? 'dist/webclap-eq-airwindows.wclap.tar.gz';
let failures = 0;
function check(condition, message) {
	console.log(`  ${condition ? 'ok  ' : 'FAIL'} ${message}`);
	if (!condition) failures++;
	return condition;
}

// ---- unpack .tar.gz ----

function untar(buffer) {
	const files = {};
	for (let offset = 0; offset + 512 <= buffer.length;) {
		const header = buffer.subarray(offset, offset + 512);
		const name = header.subarray(0, 100).toString().replace(/\0.*$/s, '');
		if (!name) break;
		const size = parseInt(header.subarray(124, 136).toString().replace(/\0.*$/s, '').trim() || '0', 8);
		const type = String.fromCharCode(header[156]);
		if (type === '0' || type === '\0') files[name.replace(/^\.\//, '')] = buffer.subarray(offset + 512, offset + 512 + size);
		offset += 512 + Math.ceil(size/512)*512;
	}
	return files;
}
const files = untar(zlib.gunzipSync(fs.readFileSync(bundlePath)));
console.log(`Bundle ${bundlePath}: ${Object.keys(files).join(', ')}`);
if (!files['module.wasm']) throw new Error('no module.wasm in bundle');

// ---- instantiate with shared memory + minimal WASI ----

const module = await WebAssembly.compile(files['module.wasm']);
function memoryImportLimits(bytes) {
	// Read the (min, max) pages of the imported memory from the import section
	let pos = 8;
	const u8 = () => bytes[pos++];
	const uleb = () => {let r = 0, s = 0, b; do {b = u8(); r |= (b & 0x7f) << s; s += 7;} while (b & 0x80); return r >>> 0;};
	const name = () => {const n = uleb(); const s = Buffer.from(bytes.subarray(pos, pos + n)).toString(); pos += n; return s;};
	while (pos < bytes.length) {
		const id = u8(), size = uleb(), end = pos + size;
		if (id === 2) {
			for (let count = uleb(); count > 0; --count) {
				name(); name();
				const kind = u8();
				if (kind === 0) uleb();
				else if (kind === 1) {u8(); const f = u8(); uleb(); if (f & 1) uleb();}
				else if (kind === 2) {const flags = u8(); const min = uleb(); const max = (flags & 1) ? uleb() : undefined; return {min, max, shared: !!(flags & 2)};}
				else if (kind === 3) {u8(); u8();}
			}
		}
		pos = end;
	}
	return null;
}
const limits = memoryImportLimits(files['module.wasm']);
check(limits?.shared, `imports shared memory (min ${limits?.min} pages, max ${limits?.max} pages)`);
const memory = new WebAssembly.Memory({initial: limits.min, maximum: limits.max ?? 16384, shared: limits.shared});

const view = () => new DataView(memory.buffer);
const bytes = () => new Uint8Array(memory.buffer);
let wasiOutput = '';
const wasi = new Proxy({
	fd_write(fd, iovs, iovsLen, nwrittenPtr) {
		let total = 0;
		for (let i = 0; i < iovsLen; ++i) {
			const ptr = view().getUint32(iovs + i*8, true), len = view().getUint32(iovs + i*8 + 4, true);
			wasiOutput += Buffer.from(bytes().slice(ptr, ptr + len)).toString();
			total += len;
		}
		view().setUint32(nwrittenPtr, total, true);
		return 0;
	},
	clock_time_get(id, precision, timePtr) {
		view().setBigUint64(timePtr, BigInt(Math.round(performance.now()*1e6)), true);
		return 0;
	},
	environ_sizes_get(countPtr, sizePtr) {
		view().setUint32(countPtr, 0, true);
		view().setUint32(sizePtr, 0, true);
		return 0;
	},
	fd_prestat_get() {return 8;}, // EBADF: no preopened directories
	proc_exit(code) {throw new Error(`proc_exit(${code})`);},
}, {get: (target, key) => target[key] ?? (() => 52)}); // ENOSYS for everything else

const instance = await WebAssembly.instantiate(module, {env: {memory}, wasi_snapshot_preview1: wasi, wasi: {'thread-spawn': () => -1}});
const exports = instance.exports;
exports._initialize?.();
const table = exports.__indirect_function_table;

// ---- helpers for reading/writing CLAP structs in the plugin's memory ----

const u32 = (ptr) => view().getUint32(ptr, true);
const f64 = (ptr) => view().getFloat64(ptr, true);
const setU32 = (ptr, v) => view().setUint32(ptr, v >>> 0, true);
const setU16 = (ptr, v) => view().setUint16(ptr, v, true);
const setI16 = (ptr, v) => view().setInt16(ptr, v, true);
const setF64 = (ptr, v) => view().setFloat64(ptr, v, true);
const malloc = (size) => {const p = exports.malloc(size); bytes().fill(0, p, p + size); return p;};
const cString = (ptr) => {if (!ptr) return null; const b = bytes(); let end = ptr; while (b[end]) end++; return Buffer.from(b.slice(ptr, end)).toString();};
const allocString = (str) => {const enc = Buffer.from(str + '\0'); const p = malloc(enc.length); bytes().set(enc, p); return p;};
const fn = (index) => {
	if (!index) throw new Error('null function pointer');
	return table.get(index);
};

// WebAssembly tables only hold typed WASM functions, so we generate a tiny module which imports
// our JS functions and re-exports them with the right types.  Signature: return type then args.
function addHostFunctions(signatures, impls) {
	const typeCodes = {I: 0x7f, L: 0x7e, F: 0x7d, D: 0x7c};
	const uleb = (arr, v) => {do {let b = v & 0x7f; v >>>= 7; if (v) b |= 0x80; arr.push(b);} while (v); return arr;};
	const str = (arr, s) => {uleb(arr, s.length); for (const c of s) arr.push(c.charCodeAt(0));};
	const names = Object.keys(signatures);
	const types = [], imports = [], exportsSection = [];
	names.forEach((name, i) => {
		const sig = signatures[name];
		types.push(0x60);
		uleb(types, sig.length - 1);
		for (const c of sig.slice(1)) types.push(typeCodes[c]);
		if (sig[0] === 'v') types.push(0); else types.push(1, typeCodes[sig[0]]);
		str(imports, 'h'); str(imports, name); imports.push(0); uleb(imports, i);
		str(exportsSection, name); exportsSection.push(0); uleb(exportsSection, i);
	});
	const section = (id, count, body) => {const content = uleb([], count).concat(body); return [id, ...uleb([], content.length), ...content];};
	const wasm = new Uint8Array([0, 0x61, 0x73, 0x6d, 1, 0, 0, 0,
		...section(1, names.length, types), ...section(2, names.length, imports), ...section(7, names.length, exportsSection)]);
	const inst = new WebAssembly.Instance(new WebAssembly.Module(wasm), {h: impls});
	const result = {};
	for (const name of names) {
		const index = table.grow(1);
		table.set(index, inst.exports[name]);
		result[name] = index;
	}
	return result;
}

// ---- host ----

const hostLog = {callbacks: 0, flushes: 0, dirty: 0, uiMessages: []};
const hostFns = addHostFunctions({
	getExtension: 'III', requestRestart: 'vI', requestProcess: 'vI', requestCallback: 'vI',
	paramsRescan: 'vII', paramsClear: 'vIII', paramsRequestFlush: 'vI',
	stateMarkDirty: 'vI',
	webviewSend: 'IIII',
	inSize: 'II', inGet: 'III', outTryPush: 'III',
	streamWrite: 'LIIL', streamRead: 'LIIL',
}, {
	getExtension: (host, idPtr) => hostExtensions[cString(idPtr)] ?? 0,
	requestRestart() {}, requestProcess() {},
	requestCallback() {hostLog.callbacks++;},
	paramsRescan() {}, paramsClear() {},
	paramsRequestFlush() {hostLog.flushes++;},
	stateMarkDirty() {hostLog.dirty++;},
	webviewSend(host, ptr, size) {
		hostLog.uiMessages.push(Buffer.from(bytes().slice(ptr, ptr + size)).toString());
		return 1;
	},
	inSize: () => inputEvents.length,
	inGet: (list, i) => inputEvents[i] ?? 0,
	outTryPush(list, eventPtr) {
		const size = u32(eventPtr), type = view().getUint16(eventPtr + 10, true);
		outputEvents.push({type, paramId: u32(eventPtr + 16), value: type === 5 ? f64(eventPtr + 40) : undefined});
		return 1;
	},
	streamWrite(stream, ptr, size) {
		streamBuffer.push(...bytes().slice(ptr, ptr + Number(size)));
		return size;
	},
	streamRead(stream, ptr, size) {
		const n = Math.min(Number(size), streamBuffer.length - streamReadPos);
		bytes().set(streamBuffer.slice(streamReadPos, streamReadPos + n), ptr);
		streamReadPos += n;
		return BigInt(n);
	},
});
let inputEvents = [], outputEvents = [], streamBuffer = [], streamReadPos = 0;

function makeStruct(fnPointers) {
	const p = malloc(fnPointers.length*4);
	fnPointers.forEach((v, i) => setU32(p + i*4, v));
	return p;
}
const hostExtensions = {
	'clap.params': makeStruct([hostFns.paramsRescan, hostFns.paramsClear, hostFns.paramsRequestFlush]),
	'clap.state': makeStruct([hostFns.stateMarkDirty]),
	'clap.webview/3': makeStruct([hostFns.webviewSend]),
};
const host = malloc(48);
setU32(host, 1); setU32(host + 4, 2); setU32(host + 8, 7);
[allocString('smoke-test'), allocString('webclap-eq'), allocString(''), allocString('0.1')].forEach((p, i) => setU32(host + 16 + i*4, p));
[hostFns.getExtension, hostFns.requestRestart, hostFns.requestProcess, hostFns.requestCallback].forEach((p, i) => setU32(host + 32 + i*4, p));

const inEventsStruct = makeStruct([0, hostFns.inSize, hostFns.inGet]);
const outEventsStruct = makeStruct([0, hostFns.outTryPush]);
const ostream = makeStruct([0, hostFns.streamWrite]);
const istream = makeStruct([0, hostFns.streamRead]);

// ---- entry & factory ----

console.log('\nEntry');
const entry = exports.clap_entry.value;
const [eInit, eDeinit, eGetFactory] = [u32(entry + 12), u32(entry + 16), u32(entry + 20)];
check(u32(entry) === 1, `CLAP version ${u32(entry)}.${u32(entry + 4)}.${u32(entry + 8)}`);
check(eInit && eDeinit && eGetFactory, 'init, deinit and get_factory are all present');
check(fn(eInit)(allocString('/')), 'entry.init() succeeds');
const factory = fn(eGetFactory)(allocString('clap.plugin-factory'));
check(factory, 'has a plugin factory');
const pluginCount = fn(u32(factory))(factory);
check(pluginCount > 0, `factory lists ${pluginCount} plugin(s)`);

for (let pluginIndex = 0; pluginIndex < pluginCount; ++pluginIndex) {
	const desc = fn(u32(factory + 4))(factory, pluginIndex);
	const id = cString(u32(desc + 12));
	const features = [];
	for (let f = u32(desc + 44); u32(f); f += 4) features.push(cString(u32(f)));
	console.log(`\nPlugin ${pluginIndex}: "${cString(u32(desc + 16))}" (${id}) [${features.join(', ')}]`);

	const plugin = fn(u32(factory + 8))(factory, host, allocString(id));
	check(plugin, 'created');
	const call = (offset, ...args) => fn(u32(plugin + offset))(plugin, ...args);
	check(call(8), 'plugin.init()');
	const ext = (name) => call(40, allocString(name));

	// parameters
	const params = ext('clap.params');
	const paramList = [];
	if (check(params, 'has clap.params')) {
		const count = fn(u32(params))(plugin);
		const info = malloc(1320);
		for (let i = 0; i < count; ++i) {
			fn(u32(params + 4))(plugin, i, info);
			const p = {
				id: u32(info), stepped: (u32(info + 4) & 1) !== 0, name: cString(info + 12), module: cString(info + 268),
				min: f64(info + 1296), max: f64(info + 1304), def: f64(info + 1312)
			};
			const text = malloc(64);
			fn(u32(params + 12))(plugin, p.id, p.def, text, 64);
			console.log(`    param ${String(p.id).padStart(3)} ${(p.module ? p.module + '/' : '') + p.name}: ${p.min}..${p.max}${p.stepped ? ' (stepped)' : ''}, default ${cString(text)}`);
			paramList.push(p);
		}
	}
	check(ext('clap.audio-ports'), 'has clap.audio-ports');
	const state = ext('clap.state');
	check(state, 'has clap.state');
	const webview = ext('clap.webview/3');
	check(webview, 'has clap.webview/3');
	const gui = ext('clap.gui');
	check(gui && fn(u32(gui))(plugin, allocString('webview'), 0), 'clap.gui supports the "webview" API');

	// Our plugins save "key value" lines in parameter order, which tells us the keys the UI and response.js use
	streamBuffer = [];
	check(fn(u32(state))(plugin, ostream), 'state.save()');
	const initialState = Buffer.from(streamBuffer).toString();
	const ourPlugin = initialState.startsWith('webclap-state ');
	if (ourPlugin) {
		initialState.split('\n').slice(1).filter(Boolean).forEach((line, i) => paramList[i].key = line.split(' ')[0]);
	}
	const firstKnob = paramList.find(p => !p.stepped && p.key);

	// ---- audio ----
	const sampleRate = 48000, block = 512;
	check(call(16, sampleRate, 1, block), 'activate(48000, 1..512)');
	check(call(24), 'start_processing()');

	const bufIn = [malloc(block*4), malloc(block*4)], bufOut = [malloc(block*4), malloc(block*4)];
	const ptrsIn = makeStruct(bufIn), ptrsOut = makeStruct(bufOut);
	const audioIn = malloc(24), audioOut = malloc(24);
	setU32(audioIn, ptrsIn); setU32(audioIn + 8, 2);
	setU32(audioOut, ptrsOut); setU32(audioOut + 8, 2);
	const proc = malloc(40);
	setU32(proc + 16, audioIn); setU32(proc + 20, audioOut);
	setU32(proc + 24, 1); setU32(proc + 28, 1);
	setU32(proc + 32, inEventsStruct); setU32(proc + 36, outEventsStruct);

	const paramEvent = (paramId, value, time = 0) => {
		const e = malloc(48);
		setU32(e, 48); setU32(e + 4, time); setU16(e + 8, 0); setU16(e + 10, 5);
		setU32(e + 16, paramId); view().setInt32(e + 24, -1, true);
		setI16(e + 28, -1); setI16(e + 30, -1); setI16(e + 32, -1);
		setF64(e + 40, value);
		return e;
	};
	let phase = 0, allFinite = true;
	// Steady-state gain of a sine at `freq`, measured over a whole number of periods
	function measureGainDb(freq) {
		const settleBlocks = 20, measureBlocks = 20;
		const inSamples = [], outSamples = [];
		for (let b = 0; b < settleBlocks + measureBlocks; ++b) {
			const f32 = new Float32Array(memory.buffer);
			for (let i = 0; i < block; ++i) {
				const x = 0.25*Math.sin(phase);
				phase += 2*Math.PI*freq/sampleRate;
				f32[bufIn[0]/4 + i] = f32[bufIn[1]/4 + i] = x;
			}
			setU32(proc + 8, block);
			call(36, proc);
			inputEvents = [];
			const out = new Float32Array(memory.buffer);
			for (let i = 0; i < block; ++i) {
				const l = out[bufOut[0]/4 + i], r = out[bufOut[1]/4 + i];
				if (!Number.isFinite(l) || !Number.isFinite(r)) allFinite = false;
				if (b >= settleBlocks) {inSamples.push(out[bufIn[0]/4 + i]); outSamples.push(l);}
			}
		}
		const periods = Math.floor(inSamples.length*freq/sampleRate);
		const length = periods > 0 ? Math.round(periods*sampleRate/freq) : inSamples.length;
		let sumIn = 0, sumOut = 0;
		for (let i = 0; i < length; ++i) {sumIn += inSamples[i]**2; sumOut += outSamples[i]**2;}
		return 10*Math.log10(sumOut/sumIn);
	}
	const setParams = (values) => {
		inputEvents = paramList.filter(p => p.key in values).map(p => paramEvent(p.id, values[p.key]));
	};

	const freqs = [40, 100, 250, 600, 1500, 4000, 9000, 15000];
	const modelSource = files['ui/response.js'];
	if (modelSource && ourPlugin) {
		// The UI's curve must match what the audio actually does
		const model = {exports: {}};
		new Function('module', modelSource.toString())(model);
		let seed = 12345;
		const random = () => {seed = (seed*1664525 + 1013904223) >>> 0; return seed/4294967296;};
		const randomValue = (p) => {
			if (p.stepped) return p.min + Math.floor(random()*(p.max - p.min + 1));
			if (p.min > 0 && p.max/p.min >= 10) return p.min*Math.pow(p.max/p.min, random()); // e.g. frequencies
			return Math.round((p.min + random()*(p.max - p.min))*10)/10;
		};
		const cases = [Object.fromEntries(paramList.map(p => [p.key, p.def]))];
		for (let i = 0; i < 12; ++i) cases.push(Object.fromEntries(paramList.map(p => [p.key, randomValue(p)])));

		let worst = 0;
		cases.forEach((values, caseIndex) => {
			setParams(values);
			const measured = freqs.map(f => measureGainDb(f));
			const expected = freqs.map(f => model.exports.responseDb(values, sampleRate, f));
			const errors = measured.map((m, i) => Math.abs(m - expected[i]) - (expected[i] < -30 ? 1 : 0)); // deep cuts: allow 1 dB more
			const caseWorst = Math.max(...errors);
			worst = Math.max(worst, caseWorst);
			const label = caseIndex ? `random #${caseIndex}` : 'defaults';
			console.log(`    ${label.padEnd(10)} ${measured.map((m, i) => `${freqs[i] >= 1000 ? freqs[i]/1000 + 'k' : freqs[i]}:${m >= 0 ? '+' : ''}${m.toFixed(1)}`).join(' ')}`);
			if (caseWorst > 0.2) console.log(`      expected   ${expected.map(e => e.toFixed(1)).join(' ')}\n      settings   ${JSON.stringify(values)}`);
		});
		check(worst <= 0.2, `measured response matches the UI curve (ui/response.js) in ${cases.length} settings, worst error ${worst.toFixed(3)} dB`);
	} else {
		const gains = freqs.map(f => measureGainDb(f));
		console.log(`    defaults   ${gains.map((g, i) => `${freqs[i]}:${g.toFixed(1)}`).join(' ')}`);
	}
	check(allFinite, 'output is always finite (no NaN/Inf)');

	// ---- state ----
	if (ourPlugin && firstKnob) {
		streamBuffer = [];
		check(fn(u32(state))(plugin, ostream), 'state.save()');
		const savedText = Buffer.from(streamBuffer).toString();
		const savedValue = parseFloat(savedText.split('\n').find(l => l.startsWith(firstKnob.key + ' ')).split(' ')[1]);
		inputEvents = [paramEvent(firstKnob.id, firstKnob.min)];
		fn(u32(params + 20))(plugin, inEventsStruct, outEventsStruct); // flush
		inputEvents = [];
		streamReadPos = 0;
		check(fn(u32(state + 4))(plugin, istream), 'state.load()');
		const value = malloc(8);
		fn(u32(params + 8))(plugin, firstKnob.id, value);
		check(f64(value) === savedValue, `${firstKnob.key} restored to ${savedValue} after load (got ${f64(value)})`);
	}

	// ---- webview ----
	const uriLength = fn(u32(webview))(plugin, 0, 0);
	const uri = malloc(uriLength);
	fn(u32(webview))(plugin, uri, uriLength);
	const startPage = cString(uri);
	if (!startPage?.startsWith('/') || !firstKnob) {
		console.log(`    webview start page ${startPage} is not served by get_resource() - skipping UI checks`);
	} else {
		console.log(`    webview start page: ${startPage}`);
		streamBuffer = [];
		const mime = malloc(64);
		check(fn(u32(webview + 4))(plugin, allocString(startPage), mime, 64, ostream),
			`get_resource(${startPage}) -> ${cString(mime)}, ${streamBuffer.length} bytes`);
		check(!fn(u32(webview + 4))(plugin, allocString('/does-not-exist'), mime, 64, ostream), 'unknown resource is rejected');

		const key = firstKnob.key;
		const uiValue = Math.round((firstKnob.min + 0.75*(firstKnob.max - firstKnob.min))*2)/2;
		const automationValue = Math.round((firstKnob.min + 0.25*(firstKnob.max - firstKnob.min))*2)/2;
		const uiReceive = (text) => {const enc = Buffer.from(text); const p = malloc(enc.length); bytes().set(enc, p); return fn(u32(webview + 8))(plugin, p, enc.length);};
		hostLog.uiMessages = [];
		check(uiReceive('ready'), 'UI "ready" accepted');
		check(hostLog.uiMessages.some(m => m.startsWith('samplerate 48000')), 'UI is told the sample rate');
		check(paramList.every(p => hostLog.uiMessages.some(m => m.startsWith(`param ${p.key} `))), `UI is told all ${paramList.length} parameter values`);
		const flushesBefore = hostLog.flushes;
		uiReceive(`begin ${key}`); uiReceive(`set ${key} ${uiValue}`); uiReceive(`end ${key}`);
		check(hostLog.flushes > flushesBefore, 'UI change requests a parameter flush');

		outputEvents = [];
		fn(u32(params + 20))(plugin, inEventsStruct, outEventsStruct);
		check(outputEvents.map(e => e.type).join(',') === '7,5,8' && outputEvents[1]?.value === uiValue,
			`host receives gesture-begin, value ${uiValue}, gesture-end (got ${JSON.stringify(outputEvents)})`);

		// automation from the host must reach the UI, even if the host never calls on_main_thread()
		inputEvents = [paramEvent(firstKnob.id, automationValue)];
		measureGainDb(1000);
		hostLog.uiMessages = [];
		uiReceive('poll');
		check(hostLog.uiMessages.includes(`param ${key} ${automationValue}`), `automation reaches the UI on "poll" (got ${JSON.stringify(hostLog.uiMessages)})`);
		hostLog.uiMessages = [];
		uiReceive('poll');
		check(hostLog.uiMessages.length === 0, 'nothing is re-sent when nothing changed');
	}

	// ---- shutdown ----
	call(28); // stop_processing
	call(20); // deactivate
	call(12); // destroy
	console.log('  ok   stopped, deactivated, destroyed');
}

fn(eDeinit)();
console.log('  ok   entry.deinit()');
if (wasiOutput) console.log('\nPlugin printed:\n' + wasiOutput);
console.log(failures ? `\n${failures} check(s) FAILED` : '\nAll checks passed');
process.exit(failures ? 1 : 0);
