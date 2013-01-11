#include "system.h"

typedef struct
{
	double boxSize; /* Particles have diameter == 1. */
	double delta; /* Max extend of the random position shift. */
	int histBins; /* Number of bins in the distance histogram */
	const char *filename; /* Filename to dump histogram to, or NULL if 
				 you don't want to measure it */
} MonteCarloConfig;

Task makeMonteCarloTask(MonteCarloConfig *mcc);


