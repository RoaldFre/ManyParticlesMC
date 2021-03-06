#include "spgrid.h"
#include "math.h"
#include <stdio.h>
#include <stdlib.h>

/* Return false if the given expression is false. */
#define QUICK_BAIL(expr) if (!expr) return false;

struct box
{
	Particle *p;	/* (Linked list of) particles in this box */
	int n;		/* number of particles in this box */

	/* Circular linked list of boxes that contain particles. Both are 
	 * NULL if this box has no particles, and hence is not part of the 
	 * list. */
	struct box *prevOccupied;
	struct box *nextOccupied;

	/* TODO be more smart so we don't need this */
	struct box *prevX;
	struct box *nextX;
	struct box *prevY;
	struct box *nextY;
	struct box *prevZ;
	struct box *nextZ;
};
typedef struct box Box;


static void addToBox(Particle *p, Box *b);
static void removeFromBox(Particle *p, Box *b);
static Box *boxFromIndex(int ix, int iy, int iz);
static Box *boxFromParticle(const Particle *p);
static Box *boxFromNonPeriodicIndex(int ix, int iy, int iz);


/* Globals */
static Box *grid; /* All boxes in the spgrid. */
static Box *occupiedBoxes; /* First element in linked list of boxes that 
			      contain particles, or NULL if all boxes are 
			      empty. */
static double boxSize = 0; /* Linear length of one box. */
static int nbx = 0; /* Number of Boxes in x dimension. */
static int nby = 0; /* Number of Boxes in y dimension. */
static int nbz = 0; /* Number of Boxes in z dimension. */
static Vec3 gridSize; /* [nbx, nby, nbz] * boxSize -- cached for performance */
static int gridNumParticles = 0; /* Total number of particles in the grid. For 
				consistency checking only! */

/* Box to signal a 'non existing box'. */
static Box nullBox = {
	.p = NULL, .n = 0, .prevOccupied = NULL, .nextOccupied = NULL,
	.prevX = &nullBox, .nextX = &nullBox,
	.prevY = &nullBox, .nextY = &nullBox,
	.prevZ = &nullBox, .nextZ = &nullBox,
};

/* Add the (newly) occupied box to the list. It cannot already be part of 
 * the list. */
static void addOccupiedBox(Box *box)
{
	assert(box != NULL);
	assert(box->n > 0);
	assert(box->p != NULL);
	assert(box->nextOccupied == NULL);
	assert(box->prevOccupied == NULL);

	if (occupiedBoxes == NULL) {
		/* Start a new list */
		box->nextOccupied = box;
		box->prevOccupied = box;
		occupiedBoxes = box;
	} else {
		box->nextOccupied = occupiedBoxes;
		box->prevOccupied = occupiedBoxes->prevOccupied;
		box->prevOccupied->nextOccupied = box;
		box->nextOccupied->prevOccupied = box;
	}
}
/* Remove the empty box from the list */
static void removeNonOccupiedBox(Box *emptyBox)
{
	assert(emptyBox != NULL);
	assert(occupiedBoxes != NULL);
	assert(emptyBox->n == 0);
	assert(emptyBox->p == NULL);

	if (emptyBox->nextOccupied == emptyBox) {
		/* List contains only emptyBox itself */
		assert(emptyBox->prevOccupied == emptyBox);
		assert(emptyBox == occupiedBoxes);

		occupiedBoxes = NULL;
	} else {
		/* List contains at least one other box */
		assert(emptyBox->prevOccupied->nextOccupied == emptyBox);
		assert(emptyBox->nextOccupied->prevOccupied == emptyBox);

		emptyBox->prevOccupied->nextOccupied = emptyBox->nextOccupied;
		emptyBox->nextOccupied->prevOccupied = emptyBox->prevOccupied;

		/* If the list uses emptyBox as head, pick another head */
		if (occupiedBoxes == emptyBox)
			occupiedBoxes = emptyBox->nextOccupied;
	}

	emptyBox->prevOccupied = NULL;
	emptyBox->nextOccupied = NULL;
}


bool allocGrid(int nx, int ny, int nz, double boxLength)
{
	assert(grid == NULL && nbx == 0 && nby == 0 && nbz == 0);
	grid = calloc(nx * ny * nz, sizeof(*grid));
	if (grid == NULL)
		return false;
	gridSize = scale((Vec3) {nx, ny, nz}, boxLength);
	nbx = nx;
	nby = ny;
	nbz = nz;
	boxSize = boxLength;

	if (nx*ny*nz*boxLength == 0)
		die("Allocating grid with 0 boxes in a dimension, or zero "
				"box size!\n");

	/* Set the prev/nextXYZ pointers
	 * When there are 2 or less boxes in a given dimension, then set 
	 * the 'next' pointer for that dimension to the nullBox to avoid 
	 * double passes! (Because for 2 boxes in a dimension, both prev 
	 * and next would point to the same neighbouring box!)
	 * For only one box in a given dimension, both next and prev is set 
	 * to the nullbox. */
	for (int ix = 0; ix < nbx; ix++)
	for (int iy = 0; iy < nby; iy++)
	for (int iz = 0; iz < nbz; iz++) {
		Box *box = boxFromIndex(ix, iy, iz);

		box->nextX = boxFromNonPeriodicIndex(ix+1, iy,   iz  );
		box->prevX = boxFromNonPeriodicIndex(ix-1, iy,   iz  );
		box->nextY = boxFromNonPeriodicIndex(ix,   iy+1, iz  );
		box->prevY = boxFromNonPeriodicIndex(ix,   iy-1, iz  );
		box->nextZ = boxFromNonPeriodicIndex(ix,   iy,   iz+1);
		box->prevZ = boxFromNonPeriodicIndex(ix,   iy,   iz-1);

		if (nbx < 3) box->nextX = &nullBox;
		if (nby < 3) box->nextY = &nullBox;
		if (nbz < 3) box->nextZ = &nullBox;

		if (nbx < 2) box->prevX = &nullBox;
		if (nby < 2) box->prevY = &nullBox;
		if (nbz < 2) box->prevZ = &nullBox;
	}

	assert(spgridSanityCheck(true));
	return true;
}

void freeGrid()
{
	if (grid == NULL) {
		assert(nbx == 0 && nby == 0 && nbz == 0);
		assert(occupiedBoxes == NULL);
		return;
	}

	for (int i = 0; i < nbx*nby*nbz; i++) {
		Box *box = &grid[i];
		Particle *p = box->p;

		if (p == NULL)
			continue; /* Empty box */

		int n = box->n; /* Save original number, it will decrease 
				   during the loop as we remove particles! */
		for (int j = 0; j < n; j++) {
			Particle *next = p->next;
			removeFromBox(p, box);
			gridNumParticles--;
			p = next;
		}
		assert(box->n == 0  &&  box->p == NULL);
	}
	assert(spgridSanityCheck(true));
	assert(gridNumParticles == 0);

	occupiedBoxes = NULL;

	nbx = nby = nbz = 0;
	free(grid);
}

void addToGrid(Particle *p) {
	p->pos = periodic(gridSize, p->pos);
	Box *box = boxFromParticle(p);
	addToBox(p, box);
	gridNumParticles++;

	assert(spgridSanityCheck(false));
}

static void periodicPosition(Particle *p)
{
	/* closePeriodic should suffice. When debugging, it can be useful 
	 * to use periodic instead if we hang on closePeriodic [but that's 
	 * a bad sign anyway!]. */
	p->pos = closePeriodic(gridSize, p->pos);
	//p->pos = periodic(gridSize, p->pos);
}

void reboxParticle(Particle *p)
{
	periodicPosition(p);

	Box *correctBox = boxFromParticle(p);
	if (correctBox == p->myBox)
		return;

	removeFromBox(p, p->myBox);
	addToBox(p, correctBox);
}
void reboxParticles(void)
{
	assert(spgridSanityCheck(false));

	forEveryParticle(&reboxParticle);

	assert(spgridSanityCheck(true));
}

/* Precondition: particle must be within the grid. */
static Box *boxFromParticle(const Particle *p)
{
	/* shift coordinates from [-gs/2 to gs/2] to [0 to gs], where gs = 
	 * gridSize */
	Vec3 shifted = add(p->pos, scale(gridSize, 1/2.0));

	assert(p != NULL);
	assert(!isnan(p->pos.x) && !isnan(p->pos.y) && !isnan(p->pos.z));
	assert(0 <= shifted.x  &&  shifted.x < gridSize.x);
	assert(0 <= shifted.y  &&  shifted.y < gridSize.y);
	assert(0 <= shifted.z  &&  shifted.z < gridSize.z);

	int ix = shifted.x / boxSize;
	int iy = shifted.y / boxSize;
	int iz = shifted.z / boxSize;

	return boxFromIndex(ix, iy, iz);
}
/* Particle may be outside the grid */
static Box *boxFromNonPeriodicParticle(const Particle *p)
{
	assert(p != NULL);
	assert(!isnan(p->pos.x) && !isnan(p->pos.y) && !isnan(p->pos.z));

	Vec3 shifted = add(p->pos, scale(gridSize, 1/2.0));

	int ix = shifted.x / boxSize;
	int iy = shifted.y / boxSize;
	int iz = shifted.z / boxSize;

	return boxFromNonPeriodicIndex(ix, iy, iz);
}

static Box *boxFromNonPeriodicIndex(int ix, int iy, int iz)
{
	ix = ix % nbx;
	if (UNLIKELY(ix < 0)) ix += nbx;

	iy = iy % nby;
	if (UNLIKELY(iy < 0)) iy += nby;

	iz = iz % nbz;
	if (UNLIKELY(iz < 0)) iz += nbz;

	return boxFromIndex(ix, iy, iz);
}

static Box *boxFromIndex(int ix, int iy, int iz)
{
	assert(0 <= ix && ix < nbx);
	assert(0 <= iy && iy < nby);
	assert(0 <= iz && iz < nbz);

	return grid + ix*nby*nbz + iy*nbz + iz;
}

static void removeFromBox(Particle *p, Box *b)
{
	assert(p != NULL && b != NULL);
	assert(p->myBox == b);
	assert(b->n >= 0);

	b->n--;

	if (b->n == 0) {
		assert(p->prev == p);
		assert(p->next == p);
		b->p = NULL;
		removeNonOccupiedBox(b);
	} else {
		assert(p->prev->next == p);
		assert(p->next->prev == p);
		p->prev->next = p->next;
		p->next->prev = p->prev;

		if (b->p == p)
			b->p = p->next;
	}

	p->prev = NULL;
	p->next = NULL;
	p->myBox = NULL;
}

static void addToBox(Particle *p, Box *b)
{
	assert(p->prev == NULL);
	assert(p->next == NULL);
	assert(p->myBox == NULL);

	b->n++;

	if (b->p == NULL) {
		assert(b->n == 1);
		b->p = p;
		p->prev = p;
		p->next = p;
		addOccupiedBox(b);
	} else {
		assert(b->n > 1);
		p->next = b->p;
		p->prev = b->p->prev;
		p->prev->next = p;
		p->next->prev = p;
	}
	p->myBox = b;
}







/* ITERATION OVER ALL NEIGBOURS OF A SINGLE PARTICLE */

/* Loop between given particle (element of 'box') and particles of adjacent 
 * boxes of box.  */
static bool forEveryNeighbourInBox(Particle *p, Box *neighbour,
		bool (*f)(Particle *p1, Particle *p2, void *data), void *data)
{
	assert(p->myBox != neighbour);

	if (neighbour == &nullBox)
		return true;
		/* Check for nullBox in case there were less than 3 boxes 
		 * in that dimension. */

	/* Match up all particles from box and neighbour. */
	int n = neighbour->n;
	Particle *p2 = neighbour->p;
	for (int i = 0; i < n; i++) {
		QUICK_BAIL(f(p, p2, data));
		p2 = p2->next;
	}
	assert(p2 == neighbour->p);

	return true;
}

static bool forEveryNeighbourBox(Particle *p,
		bool (*f)(Particle *p1, Particle *p2, void *d), void *d)
{
	//TODO: be more smart/elegant
	Box *b = p->myBox;

	/* x-1 */
	QUICK_BAIL(forEveryNeighbourInBox(p, b->prevX->prevY->nextZ, f, d));
	QUICK_BAIL(forEveryNeighbourInBox(p, b->prevX->prevY,        f, d));
	QUICK_BAIL(forEveryNeighbourInBox(p, b->prevX->prevY->prevZ, f, d));

	QUICK_BAIL(forEveryNeighbourInBox(p, b->prevX->nextZ,        f, d));
	QUICK_BAIL(forEveryNeighbourInBox(p, b->prevX,               f, d));
	QUICK_BAIL(forEveryNeighbourInBox(p, b->prevX->prevZ,        f, d));

	QUICK_BAIL(forEveryNeighbourInBox(p, b->prevX->nextY->nextZ, f, d));
	QUICK_BAIL(forEveryNeighbourInBox(p, b->prevX->nextY,        f, d));
	QUICK_BAIL(forEveryNeighbourInBox(p, b->prevX->nextY->prevZ, f, d));


	/* x */
	QUICK_BAIL(forEveryNeighbourInBox(p, b->prevY->nextZ, f, d));
	QUICK_BAIL(forEveryNeighbourInBox(p, b->prevY,        f, d));
	QUICK_BAIL(forEveryNeighbourInBox(p, b->prevY->prevZ, f, d));

	QUICK_BAIL(forEveryNeighbourInBox(p, b->nextZ,        f, d));
	QUICK_BAIL(forEveryNeighbourInBox(p, b->prevZ,        f, d));

	QUICK_BAIL(forEveryNeighbourInBox(p, b->nextY->nextZ, f, d));
	QUICK_BAIL(forEveryNeighbourInBox(p, b->nextY,        f, d));
	QUICK_BAIL(forEveryNeighbourInBox(p, b->nextY->prevZ, f, d));


	/* x+1 */
	QUICK_BAIL(forEveryNeighbourInBox(p, b->nextX->prevY->nextZ, f, d));
	QUICK_BAIL(forEveryNeighbourInBox(p, b->nextX->prevY,        f, d));
	QUICK_BAIL(forEveryNeighbourInBox(p, b->nextX->prevY->prevZ, f, d));

	QUICK_BAIL(forEveryNeighbourInBox(p, b->nextX->nextZ,        f, d));
	QUICK_BAIL(forEveryNeighbourInBox(p, b->nextX,               f, d));
	QUICK_BAIL(forEveryNeighbourInBox(p, b->nextX->prevZ,        f, d));

	QUICK_BAIL(forEveryNeighbourInBox(p, b->nextX->nextY->nextZ, f, d));
	QUICK_BAIL(forEveryNeighbourInBox(p, b->nextX->nextY,        f, d));
	QUICK_BAIL(forEveryNeighbourInBox(p, b->nextX->nextY->prevZ, f, d));

	return true;
}


bool forEveryNeighbourOfD(Particle *p,
		bool (*f)(Particle *p1, Particle *p2, void *data),
		void *data)
{
	Box *box = boxFromParticle(p);

	/* Every neighbour within the same box */
	int n = box->n;
	Particle *p2 = p->next;
	for (int i = 0; i < n - 1; i++) {
		QUICK_BAIL(f(p, p2, data));
		p2 = p2->next;
	}
	assert(p2 == p);
	
	/* Every neighbour in neighbouring boxes */
	return forEveryNeighbourBox(p, f, data);
}

static bool neighbourWrapper(Particle *p1, Particle *p2, void *data)
{
	bool (**f)(Particle *p1, Particle *p2) =
			(bool (**)(Particle *p1, Particle *p2)) data;
	return (*f)(p1, p2);
}
bool forEveryNeighbourOf(Particle *p, bool (*f)(Particle *p1, Particle *p2))
{
	return forEveryNeighbourOfD(p, &neighbourWrapper, (void*) &f);
}



/* ITERATION OVER ALL PAIRS */

/* Loop between particles of adjacent boxes of box.  We need a total 
 * ordering on the boxes so we don't check the same box twice. We use the 
 * pointer value for this.
 * However, due to periodic boundary conditions, this ONLY works when there 
 * are AT LEAST 3 boxes in each dimension!  */
static void visitNeighbours(Box *box, Box *neighbour,
		void (*f)(Particle *p1, Particle *p2, void *data), void *data)
{
	if (neighbour <= box || neighbour == &nullBox)
		return;
		/* if neighbour == box: it's our own box!
		 * else: only check boxes that have a 
		 * strictly larger pointer value to avoid 
		 * double work.
		 * Check for nullBox in case there were less than 3 boxes 
		 * in that dimension. */

	if (neighbour->n == 0)
		return;

	/* Match up all particles from box and neighbour. */
	int n1 = box->n;
	int n2 = neighbour->n;
	Particle *p1 = box->p;
	for (int i = 0; i < n1; i++) {
		Particle *p2 = neighbour->p;
		for (int j = 0; j < n2; j++) {
			f(p1, p2, data);
			p2 = p2->next;
		}
		assert(p2 == neighbour->p);
		p1 = p1->next;
	}
	assert(p1 == box->p);
}

static void visitNeighboursOf(Box *box,
		void (*f)(Particle *p1, Particle *p2, void *data), void *data)
{
	//TODO: be more smart/elegant

	/* x-1 */
	visitNeighbours(box, box->prevX->prevY->nextZ, f, data);
	visitNeighbours(box, box->prevX->prevY,        f, data);
	visitNeighbours(box, box->prevX->prevY->prevZ, f, data);

	visitNeighbours(box, box->prevX->nextZ,        f, data);
	visitNeighbours(box, box->prevX,               f, data);
	visitNeighbours(box, box->prevX->prevZ,        f, data);

	visitNeighbours(box, box->prevX->nextY->nextZ, f, data);
	visitNeighbours(box, box->prevX->nextY,        f, data);
	visitNeighbours(box, box->prevX->nextY->prevZ, f, data);


	/* x */
	visitNeighbours(box, box->prevY->nextZ, f, data);
	visitNeighbours(box, box->prevY,        f, data);
	visitNeighbours(box, box->prevY->prevZ, f, data);

	visitNeighbours(box, box->nextZ,        f, data);
	visitNeighbours(box, box->prevZ,        f, data);

	visitNeighbours(box, box->nextY->nextZ, f, data);
	visitNeighbours(box, box->nextY,        f, data);
	visitNeighbours(box, box->nextY->prevZ, f, data);


	/* x+1 */
	visitNeighbours(box, box->nextX->prevY->nextZ, f, data);
	visitNeighbours(box, box->nextX->prevY,        f, data);
	visitNeighbours(box, box->nextX->prevY->prevZ, f, data);

	visitNeighbours(box, box->nextX->nextZ,        f, data);
	visitNeighbours(box, box->nextX,               f, data);
	visitNeighbours(box, box->nextX->prevZ,        f, data);

	visitNeighbours(box, box->nextX->nextY->nextZ, f, data);
	visitNeighbours(box, box->nextX->nextY,        f, data);
	visitNeighbours(box, box->nextX->nextY->prevZ, f, data);
}

void forEveryPairD(void (*f)(Particle *p1, Particle *p2, void *data),
		void *data)
{
	/* Loop over all occupied boxes */
	if (occupiedBoxes == NULL)
		return;

	Box *box = occupiedBoxes;
	do {
		/* Loop over all i'th particles 'p' from the box 'box' and 
		 * match them with the j'th particle in the same box */
		Particle *p = box->p;
		int n = box->n;
		for (int i = 0; i < n; i++) {
			Particle *p2 = p->next;
			for (int j = i + 1; j < n; j++) {
				f(p, p2, data);
				p2 = p2->next;
			}

			p = p->next;
		}
		assert(p == box->p); /* We went 'full circle' */

		visitNeighboursOf(box, f, data);

		box = box->nextOccupied;
	} while (box != occupiedBoxes);
}

static void pairWrapper(Particle *p1, Particle *p2, void *data)
{
	void (**f)(Particle *p1, Particle *p2) =
			(void (**)(Particle *p1, Particle *p2)) data;
	(*f)(p1, p2);
}
void forEveryPair(void (*f)(Particle *p1, Particle *p2))
{
	/* I *hope* the compiler can optimize this deep chain of (function) 
	 * pointer magic. TODO: Check this! */
	forEveryPairD(&pairWrapper, (void*) &f);
}




/* PERIODIC VECTOR FUNCTIONS */
Vec3 nearestImageVector(Vec3 v1, Vec3 v2)
{
	return fastPeriodic(gridSize, sub(v2, v1));
}

double nearestImageDistance(Vec3 v1, Vec3 v2)
{
	return length(nearestImageVector(v1, v2));
}
double nearestImageDistance2(Vec3 v1, Vec3 v2)
{
	return length2(nearestImageVector(v1, v2));
}
Vec3 nearestImageUnitVector(Vec3 v1, Vec3 v2)
{
	return normalize(nearestImageVector(v1, v2));
}










/* TEST ROUTINES */

typedef struct
{
	int count;
	bool error;
} ForEveryCheckData;
static void forEveryPairCheckHelper(Particle *p1, Particle *p2, void *data)
{
	ForEveryCheckData *fecd = (ForEveryCheckData*) data;
	fecd->count++;
	if (p1 == p2) {
		fecd->error = true;
		fprintf(stderr, "forEveryPair gave illegal pair with "
				"particle %p\n", (void*)p1);
	}
}
bool forEveryPairCheck(void)
{
	ForEveryCheckData data;
	data.count = 0;
	data.error = false;

	forEveryPairD(&forEveryPairCheckHelper, &data);

	int correctCount = 0;
	for (int ix = 0; ix < nbx; ix++)
	for (int iy = 0; iy < nby; iy++)
	for (int iz = 0; iz < nbz; iz++) {
		Box *box = boxFromIndex(ix, iy, iz);
		/* Pairs in this box */
		int n1 = box->n;
		correctCount += n1 * (n1 - 1) / 2;

		/* Loop over pair with adjacent boxes to the box 
		 * of p. We need a total ordering on the boxes so 
		 * we don't check the same box twice. We use the 
		 * pointer value for this.
		 * However, due to periodic boundary conditions, 
		 * this ONLY works when there are AT LEAST 3 boxes 
		 * in each dimension! Hence the conditional di{x,y,z}'s 
		 * below. */
		for (int dix = (nbx>=3 ? -1 : 0); dix <= (nbx>=2 ? 1 : 0); dix++)
		for (int diy = (nby>=3 ? -1 : 0); diy <= (nby>=2 ? 1 : 0); diy++)
		for (int diz = (nbz>=3 ? -1 : 0); diz <= (nbz>=2 ? 1 : 0); diz++) {
			Box *b = boxFromNonPeriodicIndex(
					ix+dix, iy+diy, iz+diz);
			if (b <= box)
				continue;
				/* if b == box: it's our own box!
				 * else: only check boxes that have 
				 * a strictly larger pointer value 
				 * to avoid double counting. */
			int n2 = b->n;
			correctCount += n1 * n2;
		}
	}

	if (data.count != correctCount) {
		fprintf(stderr, "forEveryPair ran over %d pair(s), but should "
				"be %d\n", data.count, correctCount);
		fprintf(stderr, "number of particles in grid: %d\n", 
				gridNumParticles);
		return false;
	}

	return !data.error;
}

static bool forEveryNeighbourOfCheckHelper(Particle *p1, Particle *p2,
								void *data)
{
	ForEveryCheckData *fecd = (ForEveryCheckData*) data;
	fecd->count++;
	if (p1 == p2) {
		fecd->error = true;
		fprintf(stderr, "forEveryNeighbourOf gave illegal pair with "
				"particle %p\n", (void*)p1);
	}
	return true;
}

static bool forEveryNeighbourOfCheck(void)
{
	bool OK = true;

	for (int ix = 0; ix < nbx; ix++)
	for (int iy = 0; iy < nby; iy++)
	for (int iz = 0; iz < nbz; iz++) {

		Box *box = boxFromIndex(ix, iy, iz);
		int particlesInAdjacentBoxes = 0;

		/* Count all particles in adjacent boxes */
		for (int dix = (nbx>=3 ? -1 : 0); dix <= (nbx>=2 ? 1 : 0); dix++)
		for (int diy = (nby>=3 ? -1 : 0); diy <= (nby>=2 ? 1 : 0); diy++)
		for (int diz = (nbz>=3 ? -1 : 0); diz <= (nbz>=2 ? 1 : 0); diz++) {
			Box *b = boxFromNonPeriodicIndex(
					ix+dix, iy+diy, iz+diz);
			if (b == box)
				continue;

			particlesInAdjacentBoxes += b->n;
		}

		int correctNeighbours = particlesInAdjacentBoxes 
						+ MAX(0, box->n - 1);

		/* Loop over all particles in this box and check that their 
		 * number of neigbours check out. */
		int n = box->n;
		Particle *p = box->p;
		for (int i = 0; i < n; i++) {
			ForEveryCheckData data;
			data.count = 0;
			data.error = false;

			forEveryNeighbourOfD(p,
					&forEveryNeighbourOfCheckHelper,
					&data);

			if (data.count != correctNeighbours) {
				fprintf(stderr, "forEveryNeighbourOf ran "
						"over %d neighbour(s), but "
						"should be %d (p %p, b %p)\n",
						data.count, correctNeighbours,
						(void*) p, (void*) box);
				OK = false;
			}

			p = p->next;
		}
	}

	return OK;
}
	



bool spgridSanityCheck(bool checkCorrectBox)
{
	int nParts1 = 0;
	int nParts2 = 0;
	bool OK = true;

	/* BOXES */

	/* Check linked list consistency of boxes */
	for (int i = 0; i < nbx*nby*nbz; i++) {
		Box *box = &grid[i];
		Particle *p = box->p;

		if (p == NULL)
			continue; /* Empty box */

		int n = box->n; 
		for (int j = 0; j < n; j++) {
			if (p->next->prev != p || p->prev->next != p) {
				fprintf(stderr, "%p is in a broken list\n",
						(const void *) p);
				OK = false;
			}
			p = p->next;
		}
	}

	/* 1) Check if each particle is in the box it should be in, given 
	 * its coordinates.
	 * 2) Count the number of particles and check them with
	 * the total we got when initially adding the particles. */
	for (int i = 0; i < nbx*nby*nbz; i++)
	{
		Box *b = &grid[i];
		const Particle *p, *first;

		if (b->p == NULL) {
			if (b->n != 0) {
				fprintf(stderr, "Box %d: found zero particles, "
						"expected %d\n", i, b->n);
				OK = false;
			}
			continue;
		}

		first = b->p;
		p = first;
		int j = 0;
		do {
			Box *correctBox = boxFromNonPeriodicParticle(p);
			if (checkCorrectBox && correctBox != b) {
				int c = (correctBox - grid)/sizeof(*correctBox);
				fprintf(stderr, "Particle is in box %d, "
					"should be in %ld\n", i, (correctBox -
					grid)/sizeof(*correctBox));
				fprintf(stderr, "numBox per dim: %d %d %d\n",
								nbx, nby, nbz);
				fprintf(stderr, "Pos:\t");
				fprintVector(stderr, p->pos);
				fprintf(stderr, "\n");
				fprintf(stderr, "Actual box coords:  %d %d %d\n",
						i/nby/nbz, (i/nbz)%nby, i%nbx);
				fprintf(stderr, "Correct box coords: %d %d %d\n",
						c/nby/nbz, (c/nbz)%nby, c%nbx);
				OK = false;
			}
			j++;
			nParts1++;
			p = p->next;
		} while (p != first);

		if (j != b->n) {
			fprintf(stderr, "Box %d: found %d particles, "
					"expected %d\n", i, j, b->n);
			OK = false;
		}
		nParts2 += b->n;
	}

	if (nParts1 != gridNumParticles)
	{
		fprintf(stderr, "1: Found a total of %d particles, "
			"should be %d\n", nParts1, gridNumParticles);
		OK = false;
	}

	if (nParts2 != gridNumParticles)
	{
		fprintf(stderr, "2: Found a total of %d particles, "
			"should be %d\n", nParts2, gridNumParticles);
		OK = false;
	}



	/* OCCUPIED BOXES */

	/* Check linked list consistency of occupiedboxes and count them */
	int numOccupiedBoxes = 0;
	if (occupiedBoxes != NULL) {
		Box *box = occupiedBoxes;
		do {
			if (box->nextOccupied->prevOccupied != box
					|| box->prevOccupied->nextOccupied != box) {
				fprintf(stderr, "numOccupiedBoxes is broken at %p\n",
						(const void *) box);
				OK = false;
			}
			numOccupiedBoxes++;
			if (numOccupiedBoxes > nbx*nby*nbz) {
				fprintf(stderr, "numOccupiedBoxes contains more boxes "
						"than there are boxes in the "
						"grid!\n");
				OK = false;
				break; /* Avoid possible infinite loops */
			}
			box = box->nextOccupied;
		} while (box != occupiedBoxes);
	}

	/* Check whether all occupied boxes (n > 0) are in the list, and 
	 * that we don't have any extra in the list (count them) */
	int correctNumOccupiedBoxes = 0;
	for (int ix = 0; ix < nbx; ix++)
	for (int iy = 0; iy < nby; iy++)
	for (int iz = 0; iz < nbz; iz++) {
		Box *box = boxFromIndex(ix, iy, iz);

		if (box->n == 0) {
			/* empty box */
			if (box->nextOccupied != NULL
					|| box->prevOccupied != NULL) {
				fprintf(stderr, "box %p has no particles "
						"but appears to be in the "
						"occupiedBoxes list!\n",
						(void *) box);
				OK = false;
			}
		} else {
			/* occupied box */
			correctNumOccupiedBoxes++;

			if (box->nextOccupied == NULL
					|| box->prevOccupied == NULL) {
				fprintf(stderr, "box %p has particles but "
						"doesn't appear to be in the "
						"occupiedBoxes list!\n",
						(void *) box);
				OK = false;
			}
		}
	}

	if (numOccupiedBoxes != correctNumOccupiedBoxes) {
		fprintf(stderr, "Found %d boxes in the numOccupiedBoxes "
				"list, but counted %d occupied boxes!\n", 
				numOccupiedBoxes, correctNumOccupiedBoxes);
		OK = false;
	}



	/* PAIRS AND NEIGHBOURS */

	OK = forEveryPairCheck() && OK;
	OK = forEveryNeighbourOfCheck() && OK;

	return OK;
}
