#pragma once

// NDEBUG strips <cassert>, which would turn every check into a no-op and let
// failures pass silently in Release CI builds (the exact fake-green class of
// bug that once masked PicsTokenTests). This macro stays active in Release and
// prints the exact failing expression before aborting.
#define OMNI_CHECK(cond)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::fprintf(stderr, "[FAIL] %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond);                              \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (0)
