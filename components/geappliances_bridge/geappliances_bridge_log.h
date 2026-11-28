#pragma once

#ifdef __cplusplus
#define GEA_MAYBE_UNUSED [[maybe_unused]]
#else
#define GEA_MAYBE_UNUSED __attribute__((unused))
#endif

#define GEA_TAG(name) GEA_MAYBE_UNUSED static const char* const name
