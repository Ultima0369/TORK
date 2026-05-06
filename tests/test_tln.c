/*
 * test_tln.c  --  TLN (Three-valued Logic Network) unit tests
 *
 * Build:  make build/test_tln && ./build/test_tln
 *
 * Covers:
 *   - tln_init: zeroed state, no crash
 *   - tln_step + tln_decode_output: hint values in {-1, 0, +1}
 *   - tln_encode_soul: encoding from Soul buffer, NULL safety
 *   - tln_mutate: changes state at p=1.0, no change at p=0.0
 *   - tln_clone / tln_diff: exact copy, difference detection
 *   - tln_save / tln_load: round-trip persistence
 *   - observation mode: is_observing, observe_tick, timeout, reset
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../src/engine/tln.h"
#include "../src/engine/soul_access.h"

/* ── test harness macros (same style as test_core.c) ─────────── */

static int g_pass = 0;
static int g_fail = 0;

#define TEST(name) static void test_##name(void)

#define RUN(name) do { \
    printf("  %-50s", #name); \
    test_##name(); \
    g_pass++; \
    printf("PASS\n"); \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAIL\n    %s != %s  (line %d)\n", #a, #b, __LINE__); \
        g_fail++; return; \
    } \
} while(0)

#define ASSERT_TRUE(x) do { \
    if (!(x)) { \
        printf("FAIL\n    %s  (line %d)\n", #x, __LINE__); \
        g_fail++; return; \
    } \
} while(0)

/* ── helper: create persist/ dir for save/load tests ─────────── */

static void ensure_persist_dir(void)
{
    mkdir("persist", 0755);
}

/* ── helper: valid hint predicate ────────────────────────────── */

static int is_valid_hint(int h)
{
    return h == -1 || h == 0 || h == 1;
}

/* ── helper: valid tln_val_t predicate ───────────────────────── */

static int is_valid_tval(tln_val_t v)
{
    return v >= -1 && v <= 1;
}

/* ══════════════════════════════════════════════════════════════
 *  tln_init tests
 * ══════════════════════════════════════════════════════════════ */

TEST(tln_init_basic)
{
    TernaryNet net;
    /* Fill with garbage to verify init truly zeroes everything */
    memset(&net, 0xFF, sizeof(net));

    tln_init(&net);

    /* All weights zero */
    for (int i = 0; i < TLN_INPUTS * TLN_HIDDEN; i++)
        ASSERT_EQ(net.w_ih[i], 0);
    for (int i = 0; i < TLN_HIDDEN * TLN_HIDDEN; i++)
        ASSERT_EQ(net.w_hh[i], 0);
    for (int i = 0; i < TLN_HIDDEN * TLN_OUTPUTS; i++)
        ASSERT_EQ(net.w_ho[i], 0);

    /* State, output zeroed */
    for (int i = 0; i < TLN_HIDDEN; i++)
        ASSERT_EQ(net.state[i], 0);
    for (int i = 0; i < TLN_OUTPUTS; i++)
        ASSERT_EQ(net.output[i], 0);

    /* Counters zeroed */
    ASSERT_EQ((int)net.ticks, 0);
    ASSERT_EQ((int)net.mutation_count, 0);
    ASSERT_EQ((int)net.observe_ticks, 0);
    ASSERT_EQ((int)net.observe_snapshots, 0);
}

TEST(tln_init_idempotent)
{
    TernaryNet net;
    tln_init(&net);
    tln_init(&net);

    ASSERT_EQ((int)net.ticks, 0);
    ASSERT_EQ((int)net.mutation_count, 0);
}

/* ══════════════════════════════════════════════════════════════
 *  tln_step + tln_decode_output tests
 * ══════════════════════════════════════════════════════════════ */

TEST(tln_step_zero_weights_all_suspend)
{
    /* Zero weights + zero input => all output = 0 => all hints = 0 */
    TernaryNet net;
    tln_init(&net);

    tln_val_t input[TLN_INPUTS] = {0};
    tln_val_t output[TLN_OUTPUTS];

    tln_step(&net, input, output);

    for (int i = 0; i < TLN_OUTPUTS; i++)
        ASSERT_EQ(output[i], 0);

    int ah, mh, eh, enh;
    tln_decode_output(output, &ah, &mh, &eh, &enh);
    ASSERT_EQ(ah, 0);
    ASSERT_EQ(mh, 0);
    ASSERT_EQ(eh, 0);
    ASSERT_EQ(enh, 0);
}

TEST(tln_step_tick_increments)
{
    TernaryNet net;
    tln_init(&net);
    tln_val_t input[TLN_INPUTS] = {0};
    tln_val_t output[TLN_OUTPUTS];

    ASSERT_EQ((int)net.ticks, 0);
    tln_step(&net, input, output);
    ASSERT_EQ((int)net.ticks, 1);
    tln_step(&net, input, output);
    ASSERT_EQ((int)net.ticks, 2);
}

TEST(tln_step_output_always_valid_tval)
{
    /* With random-ish non-zero weights, outputs must remain in {-1,0,+1} */
    TernaryNet net;
    tln_init(&net);

    /* Set some non-trivial weights */
    for (int i = 0; i < TLN_INPUTS * TLN_HIDDEN; i++)
        net.w_ih[i] = (tln_val_t)((i % 7 == 0) ? 1 : (i % 5 == 0) ? -1 : 0);
    for (int i = 0; i < TLN_HIDDEN * TLN_HIDDEN; i++)
        net.w_hh[i] = (tln_val_t)((i % 11 == 0) ? 1 : (i % 3 == 0) ? -1 : 0);
    for (int i = 0; i < TLN_HIDDEN * TLN_OUTPUTS; i++)
        net.w_ho[i] = (tln_val_t)((i % 5 == 0) ? 1 : (i % 7 == 0) ? -1 : 0);

    tln_val_t input[TLN_INPUTS];
    for (int i = 0; i < TLN_INPUTS; i++)
        input[i] = (tln_val_t)((i % 3) - 1);

    tln_val_t output[TLN_OUTPUTS];

    for (int trial = 0; trial < 100; trial++) {
        tln_step(&net, input, output);
        for (int j = 0; j < TLN_OUTPUTS; j++)
            ASSERT_TRUE(is_valid_tval(output[j]));
    }
}

TEST(tln_decode_output_hints_always_valid)
{
    TernaryNet net;
    tln_init(&net);

    /* Set varied weights */
    for (int i = 0; i < TLN_INPUTS * TLN_HIDDEN; i++)
        net.w_ih[i] = (tln_val_t)((i % 3) - 1);
    for (int i = 0; i < TLN_HIDDEN * TLN_OUTPUTS; i++)
        net.w_ho[i] = (tln_val_t)((i % 5) - 2);

    tln_val_t input[TLN_INPUTS];
    for (int i = 0; i < TLN_INPUTS; i++)
        input[i] = (tln_val_t)((i * 7 + 3) % 3 - 1);

    tln_val_t output[TLN_OUTPUTS];

    for (int trial = 0; trial < 50; trial++) {
        tln_step(&net, input, output);
        int ah, mh, eh, enh;
        tln_decode_output(output, &ah, &mh, &eh, &enh);
        ASSERT_TRUE(is_valid_hint(ah));
        ASSERT_TRUE(is_valid_hint(mh));
        ASSERT_TRUE(is_valid_hint(eh));
        ASSERT_TRUE(is_valid_hint(enh));
    }
}

TEST(tln_decode_output_specific_values)
{
    /* Manually set output vector and verify decode logic:
     * action_hint = clamp(output[0] + output[4])
     * modify_hint = clamp(output[1] + output[5])
     * explore_hint = clamp(output[2] + output[6])
     * energy_hint = clamp(output[3] + output[7]) */
    tln_val_t output[TLN_OUTPUTS] = {1, -1, 1, 0, 1, -1, -1, 0};
    int ah, mh, eh, enh;
    tln_decode_output(output, &ah, &mh, &eh, &enh);

    /* output[0]+output[4] = 1+1 = 2 > 0 => +1 */
    ASSERT_EQ(ah, 1);
    /* output[1]+output[5] = -1+(-1) = -2 < 0 => -1 */
    ASSERT_EQ(mh, -1);
    /* output[2]+output[6] = 1+(-1) = 0 => 0 */
    ASSERT_EQ(eh, 0);
    /* output[3]+output[7] = 0+0 = 0 => 0 */
    ASSERT_EQ(enh, 0);
}

TEST(tln_step_state_feedback)
{
    /* Verify that hidden state persists across ticks (self-recurrent) */
    TernaryNet net;
    tln_init(&net);

    /* Set w_hh self-connection for first hidden neuron */
    net.w_hh[0] = 1;  /* state[0] feeds back into hidden[0] */

    tln_val_t input[TLN_INPUTS] = {0};
    tln_val_t output[TLN_OUTPUTS];

    tln_step(&net, input, output);
    /* After first tick: hidden[0] = clamp(0 + w_hh[0]*state_old[0]) = clamp(0+1*0) = 0 */
    ASSERT_EQ(net.state[0], 0);

    /* Now set external input that activates hidden[0] */
    net.w_ih[0] = 1;   /* input[0] -> hidden[0] */
    input[0] = 1;

    tln_step(&net, input, output);
    /* hidden[0] = clamp(w_ih[0]*input[0] + w_hh[0]*state_prev[0])
     *           = clamp(1*1 + 1*0) = 1 */
    ASSERT_EQ(net.state[0], 1);
}

/* ══════════════════════════════════════════════════════════════
 *  tln_encode_soul tests
 * ══════════════════════════════════════════════════════════════ */

TEST(tln_encode_soul_basic)
{
    uint8_t soul_buf[SOUL_SIZE];
    memset(soul_buf, 0, SOUL_SIZE);
    soul_buf[S_MODE] = 1;
    soul_buf[S_CODE_MOD_SUCCESS] = 1;

    tln_val_t input[TLN_INPUTS];
    tln_val_t pattern_out[4] = {1, 0, -1, 1};

    tln_encode_soul(soul_buf, 0, 50, 8, pattern_out, input);

    /* hw_stress=0 => input[0] = 1 (no stress) */
    ASSERT_EQ(input[0], 1);

    /* drive=50 > 30 => input[3] = 1 */
    ASSERT_EQ(input[3], 1);

    /* gen_count=8 > 6 => input[6] = 1 */
    ASSERT_EQ(input[6], 1);

    /* pattern_out passed through */
    ASSERT_EQ(input[8],  1);
    ASSERT_EQ(input[9],  0);
    ASSERT_EQ(input[10], -1);
    ASSERT_EQ(input[11], 1);

    /* S_MODE=1 => input[12] = 1 */
    ASSERT_EQ(input[12], 1);

    /* S_CODE_MOD_SUCCESS=1 => input[13] = 1 */
    ASSERT_EQ(input[13], 1);
}

TEST(tln_encode_soul_null_pattern)
{
    uint8_t soul_buf[SOUL_SIZE];
    memset(soul_buf, 0, SOUL_SIZE);

    tln_val_t input[TLN_INPUTS];
    tln_encode_soul(soul_buf, 0, 0, 0, NULL, input);

    /* pattern_out == NULL => input[8..11] = 0 */
    ASSERT_EQ(input[8],  0);
    ASSERT_EQ(input[9],  0);
    ASSERT_EQ(input[10], 0);
    ASSERT_EQ(input[11], 0);
}

TEST(tln_encode_soul_high_stress)
{
    uint8_t soul_buf[SOUL_SIZE];
    memset(soul_buf, 0, SOUL_SIZE);

    tln_val_t input[TLN_INPUTS];
    tln_encode_soul(soul_buf, 3, 0, 0, NULL, input);

    /* hw_stress=3 => input[0] = -1 (under stress) */
    ASSERT_EQ(input[0], -1);
    /* hw_stress=3 >= 2 => input[1] = -1 */
    ASSERT_EQ(input[1], -1);
    /* hw_stress=3 == 3 => input[2] = -1 */
    ASSERT_EQ(input[2], -1);
}

TEST(tln_encode_soul_negative_drive)
{
    uint8_t soul_buf[SOUL_SIZE];
    memset(soul_buf, 0, SOUL_SIZE);

    tln_val_t input[TLN_INPUTS];
    tln_encode_soul(soul_buf, 0, -50, 0, NULL, input);

    /* drive=-50 < -30 => input[3] = -1 */
    ASSERT_EQ(input[3], -1);
    /* drive=-50 < -60 is false, -50 > 60 is false => input[4] = 0 */
    ASSERT_EQ(input[4], 0);
    /* drive=-50 < 0 => input[5] = -1 */
    ASSERT_EQ(input[5], -1);
}

TEST(tln_encode_soul_mature_generation)
{
    uint8_t soul_buf[SOUL_SIZE];
    memset(soul_buf, 0, SOUL_SIZE);

    tln_val_t input[TLN_INPUTS];
    tln_encode_soul(soul_buf, 0, 0, 12, NULL, input);

    /* gen_count=12 > 6 => input[6] = 1 */
    ASSERT_EQ(input[6], 1);
    /* gen_count=12 > 10 => input[7] = 1 */
    ASSERT_EQ(input[7], 1);
}

/* ══════════════════════════════════════════════════════════════
 *  tln_mutate tests
 * ══════════════════════════════════════════════════════════════ */

TEST(tln_mutate_full_rate)
{
    /* p=1.0 should mutate at least some weights */
    TernaryNet net;
    tln_init(&net);

    int mutated = tln_mutate(&net, 1.0f);
    ASSERT_TRUE(mutated > 0);
    ASSERT_EQ((int)net.mutation_count, mutated);
}

TEST(tln_mutate_zero_rate)
{
    /* p=0.0 should mutate nothing */
    TernaryNet net;
    tln_init(&net);
    /* Set some state so we can verify it is preserved */
    net.w_ih[0] = 1;
    net.w_ih[1] = -1;

    TernaryNet copy;
    tln_clone(&net, &copy);

    int mutated = tln_mutate(&net, 0.0f);
    ASSERT_EQ(mutated, 0);
    ASSERT_EQ(net.w_ih[0], copy.w_ih[0]);
    ASSERT_EQ(net.w_ih[1], copy.w_ih[1]);
}

TEST(tln_mutate_values_in_range)
{
    /* After mutation, all weights must remain in {-1, 0, +1} */
    TernaryNet net;
    tln_init(&net);
    tln_mutate(&net, 1.0f);

    int total = TLN_INPUTS * TLN_HIDDEN + TLN_HIDDEN * TLN_HIDDEN + TLN_HIDDEN * TLN_OUTPUTS;
    const tln_val_t *w = net.w_ih;
    for (int i = 0; i < total; i++)
        ASSERT_TRUE(is_valid_tval(w[i]));
}

/* ══════════════════════════════════════════════════════════════
 *  tln_clone / tln_diff tests
 * ══════════════════════════════════════════════════════════════ */

TEST(tln_clone_exact_copy)
{
    TernaryNet net;
    tln_init(&net);
    for (int i = 0; i < TLN_INPUTS * TLN_HIDDEN; i++)
        net.w_ih[i] = (tln_val_t)((i * 7 + 3) % 3 - 1);
    net.ticks = 42;
    net.mutation_count = 7;

    TernaryNet clone;
    tln_clone(&net, &clone);

    ASSERT_EQ(memcmp(&net, &clone, sizeof(TernaryNet)), 0);
}

TEST(tln_diff_identical)
{
    TernaryNet a, b;
    tln_init(&a);
    tln_clone(&a, &b);
    ASSERT_EQ(tln_diff(&a, &b), 0);
}

TEST(tln_diff_detects_change)
{
    TernaryNet a, b;
    tln_init(&a);
    tln_clone(&a, &b);

    /* Mutate one weight */
    b.w_ih[0] = 1;
    int d = tln_diff(&a, &b);
    ASSERT_TRUE(d > 0);
}

TEST(tln_diff_symmetric)
{
    TernaryNet a, b;
    tln_init(&a);
    tln_init(&b);
    a.w_ih[0] = 1;
    b.w_ih[0] = -1;

    ASSERT_EQ(tln_diff(&a, &b), tln_diff(&b, &a));
}

/* ══════════════════════════════════════════════════════════════
 *  tln_save / tln_load tests
 * ══════════════════════════════════════════════════════════════ */

TEST(tln_save_load_roundtrip)
{
    ensure_persist_dir();
    const char *path = "persist/test_tln_roundtrip.bin";

    TernaryNet original;
    tln_init(&original);
    for (int i = 0; i < TLN_INPUTS * TLN_HIDDEN; i++)
        original.w_ih[i] = (tln_val_t)((i * 13 + 7) % 3 - 1);
    for (int i = 0; i < TLN_HIDDEN * TLN_OUTPUTS; i++)
        original.w_ho[i] = (tln_val_t)((i * 17 + 3) % 3 - 1);
    original.ticks = 99;
    original.mutation_count = 5;

    int rc = tln_save(&original, path);
    ASSERT_EQ(rc, 0);

    TernaryNet loaded;
    tln_init(&loaded);
    rc = tln_load(&loaded, path);
    ASSERT_EQ(rc, 0);

    ASSERT_EQ(memcmp(&original, &loaded, sizeof(TernaryNet)), 0);

    remove(path);
}

TEST(tln_load_missing_file)
{
    TernaryNet net;
    tln_init(&net);
    int rc = tln_load(&net, "persist/__nonexistent_tln__.bin");
    ASSERT_TRUE(rc != 0);
}

TEST(tln_save_load_corrupt_file)
{
    /* Write a file with wrong magic, then try to load it */
    ensure_persist_dir();
    const char *path = "persist/test_tln_corrupt.bin";

    FILE *f = fopen(path, "wb");
    ASSERT_TRUE(f != NULL);
    uint32_t bad_magic = 0xDEADBEEF;
    fwrite(&bad_magic, 4, 1, f);
    fclose(f);

    TernaryNet net;
    tln_init(&net);
    int rc = tln_load(&net, path);
    ASSERT_TRUE(rc != 0);   /* should reject bad magic */

    remove(path);
}

TEST(tln_save_then_load_empty_network)
{
    ensure_persist_dir();
    const char *path = "persist/test_tln_empty.bin";

    TernaryNet net;
    tln_init(&net);

    ASSERT_EQ(tln_save(&net, path), 0);

    TernaryNet loaded;
    tln_init(&loaded);
    ASSERT_EQ(tln_load(&loaded, path), 0);

    /* Both zeroed => identical */
    ASSERT_EQ(tln_diff(&net, &loaded), 0);

    remove(path);
}

/* ══════════════════════════════════════════════════════════════
 *  Observation mode tests
 * ══════════════════════════════════════════════════════════════ */

TEST(tln_is_observing_init)
{
    TernaryNet net;
    tln_init(&net);
    /* Freshly initialized: all output = 0 => is_observing should be true */
    ASSERT_EQ(tln_is_observing(&net), 1);
}

TEST(tln_is_observing_with_nonzero_output)
{
    TernaryNet net;
    tln_init(&net);
    net.output[0] = 1;
    ASSERT_EQ(tln_is_observing(&net), 0);
}

TEST(tln_observe_tick_increments)
{
    TernaryNet net;
    tln_init(&net);

    ASSERT_EQ((int)net.observe_ticks, 0);
    tln_observe_tick(&net);
    ASSERT_EQ((int)net.observe_ticks, 1);
    tln_observe_tick(&net);
    ASSERT_EQ((int)net.observe_ticks, 2);
}

TEST(tln_observe_tick_snapshot_at_100)
{
    TernaryNet net;
    tln_init(&net);

    for (int i = 0; i < 99; i++)
        tln_observe_tick(&net);
    ASSERT_EQ((int)net.observe_snapshots, 0);

    tln_observe_tick(&net);   /* tick 100 */
    ASSERT_EQ((int)net.observe_snapshots, 1);
}

TEST(tln_observe_timed_out)
{
    TernaryNet net;
    tln_init(&net);

    ASSERT_EQ(tln_observe_timed_out(&net), 0);

    net.observe_ticks = TLN_OBSERVE_TIMEOUT;
    ASSERT_EQ(tln_observe_timed_out(&net), 1);
}

TEST(tln_observe_reset)
{
    TernaryNet net;
    tln_init(&net);

    for (int i = 0; i < 50; i++)
        tln_observe_tick(&net);
    ASSERT_TRUE(net.observe_ticks > 0);

    tln_observe_reset(&net);
    ASSERT_EQ((int)net.observe_ticks, 0);
}

TEST(tln_observe_weight_drift)
{
    /* Every 20 ticks, observe_tick increments one w_ih weight
     * Verify that after 20 ticks, at least one weight has been nudged */
    TernaryNet net;
    tln_init(&net);

    /* All w_ih start at 0; after 20 observe ticks, w_ih[1] should be 1 */
    for (int i = 0; i < 20; i++)
        tln_observe_tick(&net);

    /* Index = (observe_ticks/20) % (TLN_INPUTS*TLN_HIDDEN) = 1 % 512 = 1 */
    ASSERT_EQ(net.w_ih[1], 1);
}

/* ══════════════════════════════════════════════════════════════
 *  Stability / edge case tests
 * ══════════════════════════════════════════════════════════════ */

TEST(tln_step_repeated_stability)
{
    /* 200 steps with non-trivial weights, all outputs and hints stay valid */
    TernaryNet net;
    tln_init(&net);
    for (int i = 0; i < TLN_INPUTS * TLN_HIDDEN; i++)
        net.w_ih[i] = (tln_val_t)((i % 3) - 1);
    for (int i = 0; i < TLN_HIDDEN * TLN_OUTPUTS; i++)
        net.w_ho[i] = (tln_val_t)((i % 5) - 2);

    tln_val_t input[TLN_INPUTS];
    for (int i = 0; i < TLN_INPUTS; i++)
        input[i] = (tln_val_t)((i * 7 + 3) % 3 - 1);

    tln_val_t output[TLN_OUTPUTS];

    for (int trial = 0; trial < 200; trial++) {
        tln_step(&net, input, output);
        for (int j = 0; j < TLN_OUTPUTS; j++)
            ASSERT_TRUE(is_valid_tval(output[j]));

        int ah, mh, eh, enh;
        tln_decode_output(output, &ah, &mh, &eh, &enh);
        ASSERT_TRUE(is_valid_hint(ah));
        ASSERT_TRUE(is_valid_hint(mh));
        ASSERT_TRUE(is_valid_hint(eh));
        ASSERT_TRUE(is_valid_hint(enh));
    }
}

TEST(tln_clone_then_mutate_isolation)
{
    /* Mutating a clone should not affect the original */
    TernaryNet a;
    tln_init(&a);
    for (int i = 0; i < TLN_INPUTS * TLN_HIDDEN; i++)
        a.w_ih[i] = (tln_val_t)((i * 3 + 1) % 3 - 1);

    TernaryNet b;
    tln_clone(&a, &b);

    tln_mutate(&b, 1.0f);

    /* a should still match itself (unchanged) */
    TernaryNet a_snapshot;
    tln_clone(&a, &a_snapshot);
    ASSERT_EQ(tln_diff(&a, &a_snapshot), 0);

    /* b should differ from a after full-rate mutation */
    ASSERT_TRUE(tln_diff(&a, &b) > 0);
}

/* ══════════════════════════════════════════════════════════════
 *  Main
 * ══════════════════════════════════════════════════════════════ */

int main(void)
{
    printf("TLN Unit Tests\n");
    printf("==============================================================\n\n");

    printf("[tln_init]\n");
    RUN(tln_init_basic);
    RUN(tln_init_idempotent);

    printf("\n[tln_step + tln_decode_output]\n");
    RUN(tln_step_zero_weights_all_suspend);
    RUN(tln_step_tick_increments);
    RUN(tln_step_output_always_valid_tval);
    RUN(tln_decode_output_hints_always_valid);
    RUN(tln_decode_output_specific_values);
    RUN(tln_step_state_feedback);

    printf("\n[tln_encode_soul]\n");
    RUN(tln_encode_soul_basic);
    RUN(tln_encode_soul_null_pattern);
    RUN(tln_encode_soul_high_stress);
    RUN(tln_encode_soul_negative_drive);
    RUN(tln_encode_soul_mature_generation);

    printf("\n[tln_mutate]\n");
    RUN(tln_mutate_full_rate);
    RUN(tln_mutate_zero_rate);
    RUN(tln_mutate_values_in_range);

    printf("\n[tln_clone / tln_diff]\n");
    RUN(tln_clone_exact_copy);
    RUN(tln_diff_identical);
    RUN(tln_diff_detects_change);
    RUN(tln_diff_symmetric);

    printf("\n[tln_save / tln_load]\n");
    RUN(tln_save_load_roundtrip);
    RUN(tln_load_missing_file);
    RUN(tln_save_load_corrupt_file);
    RUN(tln_save_then_load_empty_network);

    printf("\n[Observation mode]\n");
    RUN(tln_is_observing_init);
    RUN(tln_is_observing_with_nonzero_output);
    RUN(tln_observe_tick_increments);
    RUN(tln_observe_tick_snapshot_at_100);
    RUN(tln_observe_timed_out);
    RUN(tln_observe_reset);
    RUN(tln_observe_weight_drift);

    printf("\n[Stability / edge cases]\n");
    RUN(tln_step_repeated_stability);
    RUN(tln_clone_then_mutate_isolation);

    printf("\n==============================================================\n");
    printf("Results: %d passed, %d failed\n", g_pass, g_fail);

    return g_fail > 0 ? 1 : 0;
}
