#ifndef FUNCHOOK_CONFIG_H
#define FUNCHOOK_CONFIG_H

#define DISASM_CAPSTONE 1

#if defined(__linux__)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#define GNU_SPECIFIC_STRERROR_R 1
#endif

#endif // FUNCHOOK_CONFIG_H
