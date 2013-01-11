#include "monteCarlo.h"
#include "world.h"
#include "spgrid.h"
#include <string.h>

#define DIAMETER 1 /* Particles have diameter 1 */

static bool collidesHelper(Particle *p1, Particle *p2)
{
	/* Returns TRUE if there is NO collision! */
	return nearestImageDistance2(p1->pos, p2->pos) >= SQUARE(DIAMETER);
}
static bool collides(Particle *p)
{
	return !forEveryNeighbourOf(p, &collidesHelper);
}

static void fillWorld(void)
{
	double ws = world.worldSize;

	for (int i = 0; i < world.numParticles; i++) {
		Particle *p = &world.particles[i];
		p->pos = (Vec3) {0, 0, 0};
		addToGrid(p);
		do {
			p->pos.x = ws * (rand01() - 1/2.0);
			p->pos.y = ws * (rand01() - 1/2.0);
			if (!world.twoDimensional)
				p->pos.z = ws * (rand01() - 1/2.0);

			reboxParticle(p);
		} while (collides(p));
	}
}


static void *monteCarloTaskStart(void *initialData)
{
	assert(initialData != NULL);
	MonteCarloConfig *mcc = (MonteCarloConfig*) initialData;

	int nb = floor(world.worldSize / mcc->boxSize);

	if (nb < 0)
		die("World so small (or boxSize so big) that I can't fit a "
				"single box in there!\n");

	/* adjust boxsize to get the correct world size! */
	double trueBoxSize = world.worldSize / nb;
	printf("Requested boxsize %f, actual box size %f\n",
						mcc->boxSize, trueBoxSize);
	if (world.twoDimensional) {
		printf("Allocating grid for 2D world, %d boxes/dim.\n", nb);
		allocGrid(nb, nb, 1, trueBoxSize);
	} else {
		printf("Allocating grid for 3D world, %d boxes/dim.\n", nb);
		allocGrid(nb, nb, nb, trueBoxSize);
	}
	
	fillWorld();

	return mcc;
}

/* Perform a Monte Carlo sweep */
static TaskSignal monteCarloTaskTick(void *state)
{
	assert(state != NULL);
	MonteCarloConfig *mcc = (MonteCarloConfig*) state;

	for (int i = 0; i < world.numParticles; i++) {
		Particle *p = &world.particles[randIndex(world.numParticles)];
		Vec3 oldPos = p->pos;

		p->pos.x += mcc->delta * (rand01() - 1/2.0);
		p->pos.y += mcc->delta * (rand01() - 1/2.0);
		if (!world.twoDimensional)
			p->pos.z += mcc->delta * (rand01() - 1/2.0);

		reboxParticle(p);

		if (collides(p)) {
			/* Back to old position! */
			p->pos = oldPos;
			reboxParticle(p);
		}
	}

	return TASK_OK;
}

static void monteCarloTaskStop(void *state)
{
	freeGrid();
	free(state);
}


Task makeMonteCarloTask(MonteCarloConfig *mcc)
{
	if (mcc->boxSize <= 0)
		die("Box size is zero (or negative)!\n");

	if (mcc->delta <= 0)
		die("MC delta is zero (or negative)!\n");

	MonteCarloConfig *mccCopy = malloc(sizeof(*mccCopy));
	memcpy(mccCopy, mcc, sizeof(*mccCopy));

	Task ret = {
		.initialData = mccCopy,
		.start = &monteCarloTaskStart,
		.tick  = &monteCarloTaskTick,
		.stop  = &monteCarloTaskStop,
	};
	return ret;
}



