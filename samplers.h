#ifndef _SAMPLERS_H_
#define _SAMPLERS_H_

#include "measure.h"
#include "spgrid.h"

/* Sampler that dumps physics stats. */
Sampler dumpStatsSampler(void);

typedef struct {
	int numBins;
	double maxR;
	double rho; /* Particle density, for normalization. */
} PairCorrelationConfig;
/* A sampler that samples the pair correlation function between the particles. */
Sampler pairCorrelationSampler(PairCorrelationConfig *conf);

/* A trivial sampler that does nothing. Useful for debugging purposes. */
Sampler trivialSampler(void);

#endif

