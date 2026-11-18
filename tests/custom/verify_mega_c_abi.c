// verify_mega_c_abi.c
// Compiled with Clang/MSVC to verify Tamarin's generated .h header,
// @c_layout struct offsets, and @export function calling convention.

#include "test_tamarin_mega.h"
#include <stdio.h>


int main(void) {
  MegaReport r = run_mega_verification(135792468L);

  printf("C_ABI_HOST -> s1=%ld (pass=%d), s2=%ld (pass=%d), s3=%ld (pass=%d), "
         "s4=%ld (pass=%d), all=%d\n",
         r.stage1_ternary_actual, r.stage1_pass, r.stage2_soa_actual,
         r.stage2_pass, r.stage3_simd_actual, r.stage3_pass,
         r.stage4_parallel_actual, r.stage4_pass, r.all_passed);

  if (r.all_passed == 1 && r.stage1_ternary_actual == 17944L &&
      r.stage2_soa_actual == 346321L && r.stage3_simd_actual == 1215968L &&
      r.stage4_parallel_actual == 26453504L) {
    printf("C ABI ROUND-TRIP: PASS\n");
    return 0;
  }

  printf("C ABI ROUND-TRIP: FAIL (Struct layout or calling convention "
         "mismatch!)\n");
  return 1;
}