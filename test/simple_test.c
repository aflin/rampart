#include "runner.h"
void success()
{
    assert(0 == 0);
}

void fail()
{
    assert(0 == 1);
}

void test()
{
    success();
    // fail();
}