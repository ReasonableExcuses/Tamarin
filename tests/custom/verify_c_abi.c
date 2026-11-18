// verify_c_abi.c
#include <stdio.h>
#include "test_tamarin_full.h"

int main(void) {
    SimSummary res = run_tamarin_verification(987654321L);
    printf("C_CALLER -> scalar=%ld, parallel=%ld, passed=%d\n",
           res.scalar_checksum, res.parallel_checksum, res.passed);
    return (res.passed == 1) ? 0 : 1;
}