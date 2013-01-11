#include "system.h"

typedef struct
{
	double boxSize; /* Particles have diameter == 1. */
	double delta; /* Max extend of the random position shift. */
} MonteCarloConfig;

Task makeMonteCarloTask(MonteCarloConfig *mcc);


