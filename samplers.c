#include <string.h>
#include <stdio.h>
#include "samplers.h"
#include "spgrid.h"
#include "octave.h"

#if 0
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
#endif


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


/* PAIR CORRELATION SAMPLER */
typedef struct {
	long *bins;
	PairCorrelationConfig conf;
} PairCorrelationData;
static void *pairCorrelationStart(SamplerData *sd, void *conf)
{
	UNUSED(sd);
	assert(conf != NULL);

	PairCorrelationConfig *pcc = (PairCorrelationConfig*) conf;
	PairCorrelationData *pcd = malloc(sizeof(*pcd));
	pcd->conf = *pcc;
	pcd->bins = calloc(pcc->numBins, sizeof(*pcd->bins));

	free(pcc);
	return pcd;
}

static void pairCorrelationHelper(Particle *p1, Particle *p2, void *data)
{
	PairCorrelationData *pcd = (PairCorrelationData*) data;
	double maxR = pcd->conf.maxR;
	int nBins = pcd->conf.numBins;

	double r = nearestImageDistance(p1->pos, p2->pos);
	if (r >= maxR)
		return;

	pcd->bins[(int) (nBins * r / maxR)] += 1;
}
static SamplerSignal pairCorrelationSample(SamplerData *sd, void *data)
{
	UNUSED(sd);
	forEVERYpairD(&pairCorrelationHelper, data);
	return SAMPLER_OK;
}
static void pairCorrelationStop(SamplerData *sd, void *data)
{
	UNUSED(sd);

	PairCorrelationData *pcd = (PairCorrelationData*) data;
	double maxR = pcd->conf.maxR;
	int nBins = pcd->conf.numBins;
	double dr = maxR / nBins;
	double rho = pcd->conf.rho;
	double N = world.numParticles;

	for (int i = 0; i < nBins; i++) {
		double r = (i + 0.5) * dr;

		/* Fraction of particles between r and r+dr (factor 2 
		 * because we only counted distinct pairs): */
		double n = pcd->bins[i] * 2.0 / (N * sd->sample);

		/* We expect to find to find a fraction:
		 *   rho *  2*pi*r  * dr (2D)
		 *   rho * 4*pi*r^2 * dr (3D)
		 * between r and r+dr when everything is uniform, so 
		 * normalize accordingly */
		double normalization = rho * dr * (world.twoDimensional ?
				2*M_PI*r : 4*M_PI*SQUARE(r));

		printf("%e, %e\n", r, n / normalization);
	}

	free(pcd->bins);
	free(pcd);
}
Sampler pairCorrelationSampler(PairCorrelationConfig *conf)
{
	PairCorrelationConfig *pcc = malloc(sizeof(*pcc));
	memcpy(pcc, conf, sizeof(*pcc));
	Sampler sampler = {
			.samplerConf = pcc,
			.start = &pairCorrelationStart,
			.sample = &pairCorrelationSample,
			.stop = &pairCorrelationStop,
			.header = NULL,
	};
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

