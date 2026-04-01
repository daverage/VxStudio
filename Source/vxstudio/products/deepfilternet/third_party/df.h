#ifndef DF_H
#define DF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

typedef struct DFState DFState;

DFState* df_create(
    const char* path,
    float atten_lim,   
    const char* log_level
);

float df_process_frame(
    DFState* st,
    float* input,
    float* output
);

size_t df_get_frame_length(DFState* st);

void df_free(DFState* st);

void df_set_atten_lim(DFState* st, float lim_db);

#ifdef __cplusplus
}
#endif

#endif