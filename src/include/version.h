#ifndef RAMPART_VERSION_H

#include "rampart.h"

#define RAMPART_VERSION_H
#define RAMPART_VERSION_MAJOR 0
#define RAMPART_VERSION_MINOR 1
#define RAMPART_VERSION_PATCH 1
#define RAMPART_VERSION_PRERELEASE "-pre"

void duk_rp_push_rampart_version(duk_context *ctx);

#endif

