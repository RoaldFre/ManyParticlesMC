#include "system.h"

typedef struct
{
	double boxSize; /* Particles have diameter == 1. */
} MonteCarloConfig;

Task makeMonteCarloTask(MonteCarloConfig *mcc);


