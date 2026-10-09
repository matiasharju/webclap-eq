// Frequency response of Airwindows Baxandall2, exactly as baxandall2.h computes it.
// Used by the UI (to draw the curve) and by tests/smoke-test.mjs (to check the curve matches the audio).
//
// y = treble*(x - LP1(x)) + bass*LP2(x), with each biquad interleaved over 2 samples: H(z^2)

(function (root) {
	'use strict';

	function lowpass(fNorm, q) {
		const K = Math.tan(Math.PI*fNorm), n = 1/(1 + K/q + K*K), a0 = K*K*n;
		return {b0: a0, b1: 2*a0, b2: a0, a1: 2*(K*K - 1)*n, a2: (1 - K/q + K*K)*n};
	}
	function evaluate(c, w) {
		const c1 = Math.cos(w), s1 = Math.sin(w), c2 = Math.cos(2*w), s2 = Math.sin(2*w);
		const nr = c.b0 + c.b1*c1 + c.b2*c2, ni = -(c.b1*s1 + c.b2*s2);
		const dr = 1 + c.a1*c1 + c.a2*c2, di = -(c.a1*s1 + c.a2*s2);
		const d = dr*dr + di*di;
		return [(nr*dr + ni*di)/d, (ni*dr - nr*di)/d];
	}
	function responseDb(p, sampleRate, freq) {
		const tg = Math.pow(10, p.treble/20), bg = Math.pow(10, p.bass/20);
		const treble = lowpass(Math.min(4410*tg/sampleRate, 0.45), 0.4);
		const bass = lowpass(Math.min(8820/bg/sampleRate, 0.45), 0.2);
		const w = 2*(2*Math.PI*freq/sampleRate);
		const t = evaluate(treble, w), b = evaluate(bass, w);
		const re = tg*(1 - t[0]) + bg*b[0], im = -tg*t[1] + bg*b[1];
		return 10*Math.log10(re*re + im*im);
	}

	const api = {responseDb};
	if (typeof module === 'object' && module.exports) module.exports = api;
	else root.EqResponse = api;
})(this);
