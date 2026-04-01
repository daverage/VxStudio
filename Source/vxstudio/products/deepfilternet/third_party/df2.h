#ifndef DF2_H
#define DF2_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

typedef struct DF2State DF2State;

DF2State* df2_create(
    const char* path,
    float atten_lim,
    const char* log_level
);

float df2_process_frame(
    DF2State* st,
    float* input,
    float* output
);

size_t df2_get_frame_length(DF2State* st);

void df2_free(DF2State* st);

void df2_set_atten_lim(DF2State* st, float lim_db);

#ifdef __cplusplus
}
#endif

#endif
