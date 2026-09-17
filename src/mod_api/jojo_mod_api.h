#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JOJO_NATIVE_MOD_ABI_V1 1u

typedef struct JojoNativeModV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const char* mod_id;
    int (*on_load)(void);
    void (*on_unload)(void);
} JojoNativeModV1;

typedef const JojoNativeModV1* (*JojoGetNativeModV1Fn)(void);

#ifdef __cplusplus
}
#endif
