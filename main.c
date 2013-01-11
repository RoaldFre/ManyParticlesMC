#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <string.h>
#include <errno.h>
#include "system.h"
#include "world.h"
#include "render.h"
#include "math.h"
#include "monteCarlo.h"

/* Defaults */
#define DEF_RENDER_FRAMERATE 		30.0
#define RADIUS		 		0.5
#define DISK_AREA 			(M_PI * SQUARE(RADIUS))
#define SPHERE_VOLUME 			(4/3*M_PI * CUBE(RADIUS))


/* Static global configuration variables */

static RenderConf renderConf = {
	.framerate = DEF_RENDER_FRAMERATE,
	.radius    = RADIUS,
};
static MonteCarloConfig monteCarloConfig = {
	.boxSize = 1, /* Particles have diameter 1 */
};
static bool render;
static bool twoDimensional = false;
static double packingDensity;
static int numParticles;

static void printUsage(void)
{
	printf("Usage: main <num particles> <packing density> [flags]\n");
	printf("\n");
	printf("Flags:\n");
	printf(" -2        2D instead of 3D\n");
	printf(" -r        Render\n");
	printf(" -f <flt>  desired Framerate when rendering.\n");
	printf("             default: %f)\n", DEF_RENDER_FRAMERATE);
}

static void parseArguments(int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, ":2rf:")) != -1)
	{
		switch (c)
		{
		case '2':
			twoDimensional = true;
			break;
		case 'f':
			renderConf.framerate = atof(optarg);
			if (renderConf.framerate < 0)
				die("Invalid framerate %s\n", optarg);
			break;
		case 'r':
			render = true;
			break;
		case 'h':
			printUsage();
			exit(0);
			break;
		case ':':
			printUsage();
			die("Option -%c requires an argument\n", optopt);
			break;
		case '?':
			printUsage();
			die("Option -%c not recognized\n", optopt);
			break;
		default:
			die("Error parsing options!");
			break;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 2) {
		printUsage();
		die("\nNot enough required arguments!\n");
	}

	errno = 0;
	numParticles = strtol(argv[0], NULL, 10);
	packingDensity = strtod(argv[1], NULL);
	if (errno != 0 || numParticles < 0 || packingDensity < 0) {
		printUsage();
		die("\nError parsing the required options, or they don't "
				"make sense!\n");
	}

	if (argc > 2) {
		printUsage();
		die("\nFound unrecognised garbage at the command line!\n");
	}
}

int main(int argc, char **argv)
{
	seedRandom();

	parseArguments(argc, argv);

	double worldSize;
	if (twoDimensional) {
		double area = numParticles * DISK_AREA / packingDensity;
		worldSize = sqrt(area);
	} else {
		double volume = numParticles * SPHERE_VOLUME / packingDensity;
		worldSize = cbrt(volume);
	}

	allocWorld(numParticles, worldSize, twoDimensional);

	/* Render task */
	Task renderTask = makeRenderTask(&renderConf);

	/* Monte Carlo task */
	Task monteCarloTask = makeMonteCarloTask(&monteCarloConfig);

	/* Combined task */
	Task *tasks[2];
	tasks[0] = (render ? &renderTask : NULL);
	tasks[1] = &monteCarloTask;
	Task task = sequence(tasks, 2);

	bool everythingOK = run(&task);

	if (!everythingOK)
		return 1;
	return 0;
}

