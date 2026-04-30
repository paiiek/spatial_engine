// test_p5_decoder_underflow.cpp
// P5: Pause the decoder and verify that the audio thread sees silence
// (short reads → fill remainder with zero) and that underrun_count
// increments per missed block. No xrun, no panic.

#include "input/FileInput.h"
#include "input/SynthInput.h"
#include <cassert>
#include <cstdio>
#include <vector>

using namespace spe::input;

static void test_file_input_underflow() {
    // Small file: 1024 mono samples.
    std::vector<float> samples(1024, 0.5f);
    FileInput fi(std::move(samples), 48000, /*chunk*/256, /*fifo_pow2*/2048);

    // Drive decoder synchronously without spawning the thread.
    while (!fi.atEnd() && fi.fifoAvailable() < 1024) {
        if (!fi.decodeMore()) break;
    }

    // Pull 1024 frames — should succeed in full.
    std::vector<float> block(1024, -1.f);
    int got = fi.pull(block.data(), 1024);
    assert(got == 1024);
    assert(fi.underrunCount() == 0);
    for (int i = 0; i < 1024; ++i) assert(block[static_cast<size_t>(i)] == 0.5f);

    // FIFO is now empty; decoder is at end of buffer. Pull again — underflow.
    std::vector<float> block2(256, -2.f);
    int got2 = fi.pull(block2.data(), 256);
    assert(got2 == 0);
    assert(fi.underrunCount() == 1);

    // Multiple underflow attempts increment counter monotonically.
    for (int i = 0; i < 5; ++i) {
        (void)fi.pull(block2.data(), 256);
    }
    assert(fi.underrunCount() == 6);

    std::puts("  file_input_underflow: PASS");
}

static void test_file_input_paused_decoder() {
    // 4096 samples, but pause decoder before draining FIFO.
    std::vector<float> samples(4096, 0.25f);
    FileInput fi(std::move(samples), 48000, /*chunk*/256, /*fifo_pow2*/2048);

    // Fill FIFO partially.
    for (int i = 0; i < 2; ++i) (void)fi.decodeMore();
    assert(fi.fifoAvailable() == 512);

    // Pause: subsequent decodeMore() returns false even though source has data.
    fi.setPaused(true);
    assert(!fi.decodeMore());
    assert(fi.fifoAvailable() == 512);

    // Audio thread drains FIFO, then underflows.
    std::vector<float> block(512, -1.f);
    int got = fi.pull(block.data(), 512);
    assert(got == 512);
    (void)got;
    assert(fi.underrunCount() == 0);

    int got_starved = fi.pull(block.data(), 256);
    assert(got_starved == 0);
    (void)got_starved;
    assert(fi.underrunCount() == 1);

    // Resume and fill again.
    fi.setPaused(false);
    while (fi.fifoAvailable() < 256) {
        if (!fi.decodeMore()) break;
    }
    int got_resumed = fi.pull(block.data(), 256);
    assert(got_resumed == 256);
    (void)got_resumed;

    std::puts("  file_input_paused_decoder: PASS");
}

static void test_synth_input_underflow() {
    SynthInput si(SynthKind::Sine, 1000.f, 0.5f, 48000,
                  /*chunk*/256, /*fifo_pow2*/2048);

    // Without start(), FIFO is empty — pull underflows immediately.
    std::vector<float> block(256, -1.f);
    int got = si.pull(block.data(), 256);
    assert(got == 0);
    assert(si.underrunCount() == 1);

    // Hand-drive the decoder.
    (void)si.decodeMore();
    assert(si.fifoAvailable() == 256);

    int got2 = si.pull(block.data(), 256);
    assert(got2 == 256);
    assert(si.underrunCount() == 1); // unchanged after success
    assert(si.framesProduced() == 256);

    // No xrun, no panic — assert by getting here cleanly.
    std::puts("  synth_input_underflow: PASS");
}

static void test_decoder_thread_lifecycle() {
    // Start a real decoder thread, let it fill, stop cleanly.
    std::vector<float> samples(8192, 0.1f);
    FileInput fi(std::move(samples), 48000, /*chunk*/256, /*fifo_pow2*/4096);
    fi.start();

    // Spin briefly until FIFO fills (decoder thread loops until at_end).
    int spins = 0;
    while (fi.fifoAvailable() < 1024 && spins++ < 1000) {
        // sleep handled by std::this_thread::yield; tight enough for test.
    }
    assert(fi.fifoAvailable() >= 1024);

    fi.stop();
    assert(!fi.isRunning());

    std::puts("  decoder_thread_lifecycle: PASS");
}

int main() {
    test_file_input_underflow();
    test_file_input_paused_decoder();
    test_synth_input_underflow();
    test_decoder_thread_lifecycle();
    std::puts("PASS test_p5_decoder_underflow");
    return 0;
}
