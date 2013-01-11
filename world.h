#ifndef _WORLD_H_
#define _WORLD_H_
#include "system.h"
#include "spgridBootstrap.h"

typedef struct particle
{
	Vec3 pos; /* Position */
	struct particle *prev, *next; /* Previous/Next particle in box */
	struct box *myBox; /* The space patition box that I am in */
} Particle;

typedef struct world
{
	int numParticles;
	Particle *particles;
	double worldSize; /* Length of the world along one dimension. */
	bool twoDimensional;
} World;

extern World world;

bool allocWorld(int numParticles, double worldSize, bool twoDimensional);
void freeWorld(void);

void forEveryParticle(void (*f)(Particle *p));
void forEveryParticleD(void (*f)(Particle *p, void *data), void *data);

#endif
