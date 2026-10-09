#pragma once

// British-console-style channel EQ: HPF + LPF, then LF / LMF / HMF / HF bands.
//
// All filters are Andy Simper's linear trapezoidal state-variable filters
// (https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf), which stay stable and click-free
// when their settings move quickly (e.g. automation).
//
// The UI draws its curve with the same formulas (ui/response.js), so keep the two in sync.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace console_eq {

constexpr double pi = 3.14159265358979323846;

// ---- filter designs (mirrored in ui/response.js) ----

struct Coeffs {
	double g, k, m0, m1, m2; // output = m0*input + m1*bandpass + m2*lowpass
};

inline double prewarp(double freq, double sampleRate) {
	return std::tan(pi*std::min(freq, 0.45*sampleRate)/sampleRate);
}
inline Coeffs bell(double freq, double gainDb, double q, double sampleRate) {
	double A = std::pow(10.0, gainDb/40), k = 1/(q*A);
	return {prewarp(freq, sampleRate), k, 1, k*(A*A - 1), 0};
}
inline Coeffs lowShelf(double freq, double gainDb, double q, double sampleRate) {
	double A = std::pow(10.0, gainDb/40), k = 1/q;
	return {prewarp(freq, sampleRate)/std::sqrt(A), k, 1, k*(A - 1), A*A - 1};
}
inline Coeffs highShelf(double freq, double gainDb, double q, double sampleRate) {
	double A = std::pow(10.0, gainDb/40), k = 1/q;
	return {prewarp(freq, sampleRate)*std::sqrt(A), k, A*A, k*(1 - A)*A, 1 - A*A};
}
inline Coeffs lowpass(double freq, double q, double sampleRate) {
	return {prewarp(freq, sampleRate), 1/q, 0, 0, 1};
}
inline Coeffs highpass(double freq, double q, double sampleRate) {
	return {prewarp(freq, sampleRate), 1/q, 1, -1/q, -1};
}

// Shelves are fixed at Q 0.707 (no overshoot), the LF/HF bells at Q 0.8
constexpr double shelfQ = 0.7071067811865476, outerBellQ = 0.8;
// HPF: 3rd-order Butterworth (one-pole + 2-pole with Q 1) = 18 dB/oct.  LPF: 2-pole Butterworth = 12 dB/oct.
constexpr double hpfQ = 1.0, lpfQ = 0.7071067811865476;

// "Proportional Q": gentle boosts/cuts are broad, big ones get narrower (like many classic console EQs)
inline double proportionalQFactor(double gainDb) {
	return 0.5 + std::abs(gainDb)/15;
}

// ---- filter implementations ----

struct Svf {
	double a1 = 1, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0;
	double ic1[2] = {0, 0}, ic2[2] = {0, 0};

	void set(const Coeffs &c) {
		a1 = 1/(1 + c.g*(c.g + c.k));
		a2 = c.g*a1;
		a3 = c.g*a2;
		m0 = c.m0;
		m1 = c.m1;
		m2 = c.m2;
	}
	double process(int channel, double v0) {
		double &s1 = ic1[channel], &s2 = ic2[channel];
		double v3 = v0 - s2;
		double v1 = a1*s1 + a2*v3;
		double v2 = s2 + a2*s1 + a3*v3;
		s1 = 2*v1 - s1;
		s2 = 2*v2 - s2;
		return m0*v0 + m1*v1 + m2*v2;
	}
	void reset() {
		ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0;
	}
	void flushDenormals() {
		for (double *s : {&ic1[0], &ic1[1], &ic2[0], &ic2[1]}) {
			if (std::abs(*s) < 1e-20) *s = 0;
		}
	}
};

struct OnePoleHighpass {
	double G = 0;
	double state[2] = {0, 0};

	void set(double freq, double sampleRate) {
		double g = prewarp(freq, sampleRate);
		G = g/(1 + g);
	}
	double process(int channel, double x) {
		double &s = state[channel];
		double v = (x - s)*G;
		double lp = v + s;
		s = lp + v;
		return x - lp;
	}
	void reset() {
		state[0] = state[1] = 0;
	}
	void flushDenormals() {
		for (double &s : state) {
			if (std::abs(s) < 1e-20) s = 0;
		}
	}
};

// ---- the EQ ----

struct Settings {
	bool hpfOn = false, lpfOn = false;
	double hpfFreq = 80, lpfFreq = 12000;
	double hfGain = 0, hfFreq = 8000;
	bool hfBell = false;
	double hmfGain = 0, hmfFreq = 2500, hmfQ = 1;
	double lmfGain = 0, lmfFreq = 600, lmfQ = 1;
	double lfGain = 0, lfFreq = 100;
	bool lfBell = false;
	bool eqIn = true, proportionalQ = false;
	double outputDb = 0;
};

class ConsoleEq {
public:
	void prepare(double newSampleRate) {
		sampleRate = newSampleRate;
		// ~15ms smoothing, updated every `chunk` samples
		smoothingCoeff = 1 - std::exp(-double(chunk)/(0.015*sampleRate));
	}

	void reset(const Settings &settings) {
		for (auto *f : {&hpf2, &lpf, &lfShelf, &lfBell, &lmf, &hmf, &hfShelf, &hfBell}) f->reset();
		hpf1.reset();
		targets(settings, current); // no gliding after a reset
		updateFilters();
	}

	void process(const Settings &settings, const float *inL, const float *inR, float *outL, float *outR, uint32_t frames) {
		double target[count];
		targets(settings, target);

		const float *in[2] = {inL, inR};
		float *out[2] = {outL, outR};
		for (uint32_t start = 0; start < frames; start += chunk) {
			uint32_t end = std::min(frames, start + chunk);
			for (int i = 0; i < count; ++i) current[i] += (target[i] - current[i])*smoothingCoeff;
			updateFilters();

			const double hpfMix = current[sHpfMix], lpfMix = current[sLpfMix];
			const double lfBellMix = current[sLfBellMix], hfBellMix = current[sHfBellMix], eqMix = current[sEqMix];
			const double outputGain = std::pow(10.0, current[sOutputDb]/20);
			for (int c = 0; c < 2; ++c) {
				for (uint32_t i = start; i < end; ++i) {
					double x = in[c][i];

					double hp = hpf2.process(c, hpf1.process(c, x));
					x += hpfMix*(hp - x);
					double lp = lpf.process(c, x);
					x += lpfMix*(lp - x);

					double dry = x;
					double shelf = lfShelf.process(c, x), bellOut = lfBell.process(c, x);
					x = shelf + lfBellMix*(bellOut - shelf);
					x = lmf.process(c, x);
					x = hmf.process(c, x);
					shelf = hfShelf.process(c, x);
					bellOut = hfBell.process(c, x);
					x = shelf + hfBellMix*(bellOut - shelf);
					x = dry + eqMix*(x - dry);

					out[c][i] = float(x*outputGain);
				}
			}
		}
		for (auto *f : {&hpf2, &lpf, &lfShelf, &lfBell, &lmf, &hmf, &hfShelf, &hfBell}) f->flushDenormals();
		hpf1.flushDenormals();
	}

private:
	static constexpr uint32_t chunk = 16;
	double sampleRate = 48000, smoothingCoeff = 1;

	// Smoothed values: frequencies and Qs as log2 (so they glide evenly), gains in dB, switches as 0-1 crossfades
	enum {sHpfMix, sHpfFreq, sLpfMix, sLpfFreq,
		sHfGain, sHfFreq, sHfBellMix, sHmfGain, sHmfFreq, sHmfQ, sLmfGain, sLmfFreq, sLmfQ, sLfGain, sLfFreq, sLfBellMix,
		sEqMix, sProportional, sOutputDb, count};
	double current[count] = {};

	OnePoleHighpass hpf1;
	Svf hpf2, lpf, lfShelf, lfBell, lmf, hmf, hfShelf, hfBell;

	static void targets(const Settings &s, double *t) {
		t[sHpfMix] = s.hpfOn;
		t[sHpfFreq] = std::log2(s.hpfFreq);
		t[sLpfMix] = s.lpfOn;
		t[sLpfFreq] = std::log2(s.lpfFreq);
		t[sHfGain] = s.hfGain;
		t[sHfFreq] = std::log2(s.hfFreq);
		t[sHfBellMix] = s.hfBell;
		t[sHmfGain] = s.hmfGain;
		t[sHmfFreq] = std::log2(s.hmfFreq);
		t[sHmfQ] = std::log2(s.hmfQ);
		t[sLmfGain] = s.lmfGain;
		t[sLmfFreq] = std::log2(s.lmfFreq);
		t[sLmfQ] = std::log2(s.lmfQ);
		t[sLfGain] = s.lfGain;
		t[sLfFreq] = std::log2(s.lfFreq);
		t[sLfBellMix] = s.lfBell;
		t[sEqMix] = s.eqIn;
		t[sProportional] = s.proportionalQ;
		t[sOutputDb] = s.outputDb;
	}

	double bellQ(double q, double gainDb) const {
		double factor = proportionalQFactor(gainDb);
		return q*(1 + current[sProportional]*(factor - 1));
	}

	void updateFilters() {
		const double *c = current;
		const double sr = sampleRate;
		hpf1.set(std::exp2(c[sHpfFreq]), sr);
		hpf2.set(highpass(std::exp2(c[sHpfFreq]), hpfQ, sr));
		lpf.set(lowpass(std::exp2(c[sLpfFreq]), lpfQ, sr));
		lfShelf.set(lowShelf(std::exp2(c[sLfFreq]), c[sLfGain], shelfQ, sr));
		lfBell.set(bell(std::exp2(c[sLfFreq]), c[sLfGain], bellQ(outerBellQ, c[sLfGain]), sr));
		lmf.set(bell(std::exp2(c[sLmfFreq]), c[sLmfGain], bellQ(std::exp2(c[sLmfQ]), c[sLmfGain]), sr));
		hmf.set(bell(std::exp2(c[sHmfFreq]), c[sHmfGain], bellQ(std::exp2(c[sHmfQ]), c[sHmfGain]), sr));
		hfShelf.set(highShelf(std::exp2(c[sHfFreq]), c[sHfGain], shelfQ, sr));
		hfBell.set(bell(std::exp2(c[sHfFreq]), c[sHfGain], bellQ(outerBellQ, c[sHfGain]), sr));
	}
};

} // namespace
