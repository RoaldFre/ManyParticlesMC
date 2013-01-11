#include "world.h"

World world;

bool allocWorld(int numParticles, double worldSize, bool twoDimensional)
{
	assert(world.particles == NULL);
	world.particles = calloc(numParticles, sizeof(*world.particles));
	if (world.particles == NULL)
		return false;
	world.numParticles = numParticles;
	world.worldSize = worldSize;
	world.twoDimensional = twoDimensional;
	return true;
}

void freeWorld(void)
{
	free(world.particles);
}



/* loop over every particle in the world */
void forEveryParticle(void (*f)(Particle *p))
{
	for (int p = 0; p < world.numParticles; p++)
		f(&world.particles[p]);
}
/* loop over every particle in the world, pass [D]ata to the function */
void forEveryParticleD(void (*f)(Particle *p, void *data), void *data)
{
	for (int p = 0; p < world.numParticles; p++)
		f(&world.particles[p], data);
}

