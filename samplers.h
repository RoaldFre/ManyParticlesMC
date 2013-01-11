#ifndef _SAMPLERS_H_
#define _SAMPLERS_H_

#include "measure.h"
#include "spgrid.h"

/* Sampler that dumps physics stats. */
Sampler dumpStatsSampler(void);

/* A trivial sampler that does nothing. Useful for debugging purposes. */
Sampler trivialSampler(void);

#endif

