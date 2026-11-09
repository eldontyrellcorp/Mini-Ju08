// Fixed-point engine (firmware/src/ju_dsp.c) against the float engine of jupiter8_esp32, on the 64 factory patches.
// Same notes, same starting phases, ANALOG TOLERANCE off (the two random generators differ). Reports, per patch, the level
// of each and the waveform error between them; fails when a patch is silent, clips, or differs by more than the limits.
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include "dsp/engine.h"
#include "fx_host.h"
extern "C" {
#include "ju_params.h"
void ju_init(void);
void ju_prepare_block(void);
void ju_render(int32_t*, int32_t*);
void ju_finish(const int32_t*, const int32_t*, int32_t*);
void ju_note_on(uint8_t, uint8_t);
void ju_note_off(uint8_t, uint8_t);
void ju_set_phase_seed(void);
extern ju_patch_t ju_patch[2];
extern uint8_t ju_stereo_layers;
extern uint8_t ju_global[G_COUNT];
}
#include "ju_presets.h"

static const int kRate = 44100;
struct Result { double rmsRef, rmsFx, snr, peak; };

// scen 0: dry voices, whole keyboard; 1: arpeggiator (up, 2 octaves); 2: DUAL with two patches, Lower left / Upper right
static Result run(int idx, std::vector<float>* dump, int scen = 0) {
    j8::Patch bank[64];
    j8::applyFactory(bank, 64);
    auto* ref = new j8::Engine();
    ref->init((float)kRate);
    hostFxInit(*ref, (float)kRate);
    ref->global[j8::G_KEYMODE] = j8::KM_WHOLE;
    ref->global[j8::G_VINTAGE] = 0;
    ref->patch[0] = bank[idx];
    if (scen == 2) { ref->patch[1] = bank[(idx + 9) % 64]; j8::fxDefaults(ref->patch[1].fx); ref->global[j8::G_KEYMODE] = j8::KM_DUAL; ref->stereoLayers = true; }
    if (scen == 1) { ref->global[j8::G_ARP_ON] = 1; ref->global[j8::G_ARP_RATE] = 150; ref->global[j8::G_ARP_RANGE] = 1; }
    j8::fxDefaults(ref->patch[0].fx);   // the fixed engine has no effects yet: compare the dry voices
    ju_init();
    ju_global[G_KEYMODE] = KM_WHOLE;
    ju_global[G_VINTAGE] = 0;
    ju_patch[0] = JU_PRESETS[idx];
    if (scen == 2) { ju_patch[1] = JU_PRESETS[(idx + 9) % 64]; ju_global[G_KEYMODE] = KM_DUAL; ju_stereo_layers = 1; }
    if (scen == 1) { ju_global[G_ARP_ON] = 1; ju_global[G_ARP_RATE] = 150; ju_global[G_ARP_RANGE] = 1; }
    ju_set_phase_seed();

    const int notes[4] = {45 + (idx / 8) * 3, 52 + (idx / 8) * 3, 57 + (idx / 8) * 3, 60 + (idx / 8) * 3};
    const int blocks = kRate * 3 / j8::kBlock;
    double eRef = 0, eFx = 0, eDiff = 0, peak = 0;
    for (int b = 0; b < blocks; b++) {
        if (b == 4) for (int n : notes) { ref->pushEvent(j8::Event{j8::Event::NoteOn, (uint8_t)n, 0}); ju_note_on(0xFF, (uint8_t)n); }
        if (b == blocks * 2 / 3) for (int n : notes) { ref->pushEvent(j8::Event{j8::Event::NoteOff, (uint8_t)n, 0}); ju_note_off(0xFF, (uint8_t)n); }
        float u[32] = {0}, l[32] = {0}, L[32], R[32];
        ref->prepareBlock(); ref->render(0, j8::kNumVoices, u, l); ref->fxWorker(); ref->finish(u, l, L, R);
        int32_t fu[32] = {0}, fl[32] = {0}, out[64];
        ju_prepare_block(); ju_render(fu, fl); ju_finish(fu, fl, out);
        for (int k = 0; k < 32; k++) {
            double r = L[k], x = out[2 * k] / 32768.0, r2 = R[k], x2 = out[2 * k + 1] / 32768.0;
            eRef += r * r + r2 * r2; eFx += x * x + x2 * x2; eDiff += (r - x) * (r - x) + (r2 - x2) * (r2 - x2);
            if (fabs(x) > peak) peak = fabs(x);
            if (fabs(x2) > peak) peak = fabs(x2);
            if (dump) { dump->push_back((float)r); dump->push_back((float)x); }
        }
    }
    delete ref;
    double n = (double)blocks * 64;
    return {sqrt(eRef / n), sqrt(eFx / n), eDiff > 0 ? 10 * log10(eRef / eDiff) : 99.0, peak};
}

int main(int argc, char** argv) {
    int fails = 0;
    double minSnr = 99;
    printf("%-3s %-12s %9s %9s %8s %8s\n", "#", "patch", "rms ref", "rms fix", "dB diff", "SNR dB");
    for (int i = 0; i < 64; i++) {
        Result r = run(i, nullptr);
        double dbd = (r.rmsRef > 1e-9 && r.rmsFx > 1e-9) ? 20 * log10(r.rmsFx / r.rmsRef) : -99;
        bool bad = r.rmsRef > 1e-4 && (r.rmsFx < 1e-5 || fabs(dbd) > 3.0 || r.peak >= 0.9999);
        printf("%d%d  %-12s %9.5f %9.5f %+8.2f %8.1f %s\n", i / 8 + 1, i % 8 + 1, JU_PRESETS[i].name, r.rmsRef, r.rmsFx, dbd, r.snr, bad ? "  <-- FAIL" : "");
        if (bad) fails++;
        if (r.snr < minSnr) minSnr = r.snr;
    }
    printf("worst SNR %.1f dB, %d failing\n", minSnr, fails);
    const int arpIdx[3] = {20, 8 * 5 + 2, 8 * 3 + 0};   // pad, lead, brass
    for (int scen = 1; scen <= 2; scen++)
        for (int i : arpIdx) {
            Result r = run(i, nullptr, scen);
            double dbd = (r.rmsRef > 1e-9 && r.rmsFx > 1e-9) ? 20 * log10(r.rmsFx / r.rmsRef) : -99;
            bool bad = r.rmsRef > 1e-4 && (r.rmsFx < 1e-5 || fabs(dbd) > 3.0 || r.snr < 8.0);
            printf("%s  %-12s %9.5f %9.5f %+8.2f %8.1f %s\n", scen == 1 ? "arp  " : "stereo", JU_PRESETS[i].name, r.rmsRef, r.rmsFx, dbd, r.snr, bad ? "  <-- FAIL" : "");
            if (bad) fails++;
        }
    if (argc > 1) {   // dump one patch: ref and fixed interleaved, float32
        std::vector<float> d; run(atoi(argv[1]), &d);
        FILE* f = fopen("ju_dump.f32", "wb"); fwrite(d.data(), 4, d.size(), f); fclose(f);
    }
    return fails ? 1 : 0;
}
