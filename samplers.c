#include <string.h>
#include <stdio.h>
#include "samplers.h"
#include "spgrid.h"
#include "octave.h"

/* Simple sampler start that just passes the configuration data as the 
 * state pointer. */
static void *passConf(SamplerData *sd, void *conf)
{
	UNUSED(sd);
	return conf;
}
/* Simple sampler stop that just frees the state. */
static void freeState(SamplerData *sd, void *state)
{
	UNUSED(sd);
	free(state);
}


/* STATS / VERBOSE */

static SamplerSignal dumpStatsSample(SamplerData *sd, void *data)
{
	UNUSED(sd);
	UNUSED(data);
	//dumpStats();
	return SAMPLER_OK;
}
Sampler dumpStatsSampler(void)
{
	Sampler sampler;
	sampler.samplerConf = NULL;
	sampler.start  = NULL;
	sampler.sample = &dumpStatsSample;
	sampler.stop   = NULL;
	sampler.header = NULL;
	return sampler;
}


/* TRIVIAL SAMPLER */

Sampler trivialSampler(void) {
	Sampler sampler = {
			.samplerConf = NULL,
			.start = NULL,
			.sample = NULL,
			.stop = NULL,
			.header = NULL,
	};
	return sampler;
}

