// Frequency response of the Console EQ, using exactly the same filter designs as console-eq.h.
// Used by the UI (to draw the curve) and by tests/smoke-test.mjs (to check the curve matches the audio).
//
// `params` uses the parameter keys: {hpfOn, hpfFreq, ..., output}

(function (root) {
	'use strict';

	const shelfQ = Math.SQRT1_2, outerBellQ = 0.8, hpfQ = 1, lpfQ = Math.SQRT1_2;

	function prewarp(freq, sampleRate) {
		return Math.tan(Math.PI*Math.min(freq, 0.45*sampleRate)/sampleRate);
	}
	function bell(freq, gainDb, q, sr) {
		const A = Math.pow(10, gainDb/40), k = 1/(q*A);
		return {g: prewarp(freq, sr), k, m0: 1, m1: k*(A*A - 1), m2: 0};
	}
	function lowShelf(freq, gainDb, q, sr) {
		const A = Math.pow(10, gainDb/40), k = 1/q;
		return {g: prewarp(freq, sr)/Math.sqrt(A), k, m0: 1, m1: k*(A - 1), m2: A*A - 1};
	}
	function highShelf(freq, gainDb, q, sr) {
		const A = Math.pow(10, gainDb/40), k = 1/q;
		return {g: prewarp(freq, sr)*Math.sqrt(A), k, m0: A*A, m1: k*(1 - A)*A, m2: 1 - A*A};
	}
	function lowpass(freq, q, sr) {
		return {g: prewarp(freq, sr), k: 1/q, m0: 0, m1: 0, m2: 1};
	}
	function highpass(freq, q, sr) {
		return {g: prewarp(freq, sr), k: 1/q, m0: 1, m1: -1/q, m2: -1};
	}
	function proportionalQFactor(gainDb) {
		return 0.5 + Math.abs(gainDb)/15;
	}

	// Complex numbers as [re, im]
	const mul = (a, b) => [a[0]*b[0] - a[1]*b[1], a[0]*b[1] + a[1]*b[0]];
	const lerp = (a, b, t) => [a[0] + (b[0] - a[0])*t, a[1] + (b[1] - a[1])*t];
	const ONE = [1, 0];
	const toDb = (c) => 10*Math.log10(c[0]*c[0] + c[1]*c[1] + 1e-30);

	// Trapezoidal SVF = bilinear transform: s = j*tan(w/2)/g
	function svf(c, t) {
		const x = t/c.g;
		const dr = 1 - x*x, di = c.k*x, d = dr*dr + di*di;
		const lpRe = dr/d, lpIm = -di/d; // lowpass = 1/(s^2 + k s + 1)
		const bpRe = -x*lpIm, bpIm = x*lpRe; // bandpass = s * lowpass
		return [c.m0 + c.m1*bpRe + c.m2*lpRe, c.m1*bpIm + c.m2*lpIm];
	}
	function onePoleHighpass(freq, sr, t) {
		const x = t/prewarp(freq, sr); // s/(s + 1)
		return [x*x/(1 + x*x), x/(1 + x*x)];
	}

	// Complex response of each section at `freq`
	function sections(p, sr, freq) {
		const t = Math.tan(Math.PI*Math.min(freq, 0.4999*sr)/sr);
		const bellQ = (q, gainDb) => q*(p.qMode >= 0.5 ? proportionalQFactor(gainDb) : 1);

		const hp = mul(onePoleHighpass(p.hpfFreq, sr, t), svf(highpass(p.hpfFreq, hpfQ, sr), t));
		const lp = svf(lowpass(p.lpfFreq, lpfQ, sr), t);
		const lf = p.lfBell >= 0.5
			? svf(bell(p.lfFreq, p.lfGain, bellQ(outerBellQ, p.lfGain), sr), t)
			: svf(lowShelf(p.lfFreq, p.lfGain, shelfQ, sr), t);
		const lmf = svf(bell(p.lmfFreq, p.lmfGain, bellQ(p.lmfQ, p.lmfGain), sr), t);
		const hmf = svf(bell(p.hmfFreq, p.hmfGain, bellQ(p.hmfQ, p.hmfGain), sr), t);
		const hf = p.hfBell >= 0.5
			? svf(bell(p.hfFreq, p.hfGain, bellQ(outerBellQ, p.hfGain), sr), t)
			: svf(highShelf(p.hfFreq, p.hfGain, shelfQ, sr), t);
		return {
			hpf: p.hpfOn >= 0.5 ? hp : ONE,
			lpf: p.lpfOn >= 0.5 ? lp : ONE,
			lf, lmf, hmf, hf
		};
	}

	// Total response in dB (what the plugin does to a sine wave at `freq`)
	function responseDb(p, sampleRate, freq) {
		const s = sections(p, sampleRate, freq);
		const eq = lerp(ONE, mul(mul(s.lf, s.lmf), mul(s.hmf, s.hf)), p.eqIn >= 0.5 ? 1 : 0);
		return toDb(mul(mul(s.hpf, s.lpf), eq)) + p.output;
	}

	// Each section on its own, in dB (for drawing the individual bands)
	function sectionsDb(p, sampleRate, freq) {
		const s = sections(p, sampleRate, freq);
		return {
			filters: toDb(mul(s.hpf, s.lpf)),
			lf: toDb(s.lf), lmf: toDb(s.lmf), hmf: toDb(s.hmf), hf: toDb(s.hf)
		};
	}

	const api = {responseDb, sectionsDb};
	if (typeof module === 'object' && module.exports) module.exports = api;
	else root.EqResponse = api;
})(this);
