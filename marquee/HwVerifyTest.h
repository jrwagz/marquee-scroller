#ifndef HW_VERIFY_TEST_H
#define HW_VERIFY_TEST_H

// On-device ECDSA-P256 verify test for jrwagz/marquee-scroller#100.
//
// Compiled in only when -DWAGFAM_HW_VERIFY_TEST=1 is passed at build time;
// otherwise the entire body of HwVerifyTest.cpp is empty and this header is
// inert.  When compiled in, marquee.ino's setup() calls runHwVerifyTest()
// before any networking starts, the fixture exercises the production
// verifyConfigUpdateSignature() path against 8 hardcoded cases (1 positive,
// 7 negative), prints PASS/FAIL per case + a summary line over Serial, then
// halts.
//
// See docs/HARDWARE_TEST_ECDSA_VERIFY.md for the build/flash/capture procedure.
// Fixtures are produced by scripts/sign_test_payloads.py — same crypto
// library and wire format as jrwagz/wagfam-server's signing path.

#ifdef WAGFAM_HW_VERIFY_TEST
void runHwVerifyTest();
#endif

#endif
