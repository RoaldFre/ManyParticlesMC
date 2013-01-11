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
#include "measure.h"
#include "samplers.h"

/* Defaults */
#define DEF_MEASURE_FILE 		"data"
#define DEF_RENDER_FRAMERATE 		30.0
#define DEF_PAIR_CORRELATION_BINS 	1000
#define DEF_DELTA		 	1
#define RADIUS		 		0.5
#define DISK_AREA 			(M_PI * SQUARE(RADIUS))
#define SPHERE_VOLUME 			(4.0/3.0*M_PI * CUBE(RADIUS))


/* Static global configuration variables */

static RenderConf renderConf = {
	.framerate = DEF_RENDER_FRAMERATE,
	.radius    = RADIUS,
};
static MonteCarloConfig monteCarloConfig = {
	.boxSize = 1, /* Particles have diameter 1 */
	.delta = DEF_DELTA,
};
static MeasurementConf measConf = {
	.measureTime = -1, /* Go on indefinitely. */
	.measureInterval = -1, /* Don't measure by default. */
	.measureWait = -1,
	.measureFile = DEF_MEASURE_FILE,
	.verbose = true,
	.renderStrBufSize = 0, /* Disable rendering of strings. */
	.renderStrX = 20,
	.renderStrY = 20,
	.measureHeader = NULL,
};
static bool render;
static bool twoDimensional = false;
static double packingDensity;
static int numParticles;
static int numBoxes = -1; /* guard */
static int pairCorrelationBins = DEF_PAIR_CORRELATION_BINS;

static void printUsage(void)
{
	printf("Usage: main <num particles> <packing density> [flags]\n");
	printf("\n");
	printf("Flags:\n");
	printf(" -2        2D instead of 3D\n");
	printf(" -d <flt>  Delta to use in Monte Carlo algorithm\n");
	printf("             default: %f\n", monteCarloConfig.delta);
	printf(" -I <num>  sample Interval\n");
	printf("             default: don't sample\n");
	printf(" -P <num>  measurement Period\n");
	printf("             default: sample indefinitely\n");
	printf(" -B <num>  number of Bins for the pair correlation\n");
	printf("             default: %d\n", pairCorrelationBins);
	printf(" -b <num>  number of Boxes per dimension\n");
	printf(" -r        Render\n");
	printf(" -f <flt>  desired Framerate when rendering.\n");
	printf("             default: %f)\n", DEF_RENDER_FRAMERATE);
	printf("\n");
}

static void parseArguments(int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, ":2d:I:P:rf:B:b:")) != -1)
	{
		switch (c)
		{
		case '2':
			twoDimensional = true;
			break;
		case 'd':
			monteCarloConfig.delta = atof(optarg);
			if (monteCarloConfig.delta <= 0)
				die("Invalid Monte Carlo delta %s\n", optarg);
			break;
		case 'I':
			measConf.measureInterval = atoi(optarg);
			if (measConf.measureInterval <= 0)
				die("Invalid measurement interval %s\n", optarg);
			break;
		case 'P':
			measConf.measureTime = atoi(optarg);
			if (measConf.measureTime < 0)
				die("Invalid measurement time %s\n", optarg);
			break;
		case 'f':
			renderConf.framerate = atof(optarg);
			if (renderConf.framerate < 0)
				die("Invalid framerate %s\n", optarg);
			break;
		case 'r':
			render = true;
			break;
		case 'B':
			pairCorrelationBins = atoi(optarg);
			if (pairCorrelationBins <= 0)
				die("Invalid number of pair correlation bins %s\n", optarg);
			break;
		case 'b':
			numBoxes = atoi(optarg);
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
	
	if (numBoxes > 0) {
		/* explicit number of boxes requested. */
		monteCarloConfig.boxSize = worldSize / numBoxes;
		if (monteCarloConfig.boxSize < 2*RADIUS)
			die("Resulting boxsize %f less than particle "
					"dameter %f!\n",
					monteCarloConfig.boxSize, 2*RADIUS);
	}

	allocWorld(numParticles, worldSize, twoDimensional);

	/* Render task */
	Task renderTask = makeRenderTask(&renderConf);

	/* Monte Carlo task */
	Task monteCarloTask = makeMonteCarloTask(&monteCarloConfig);

	/* Measurement task */
	PairCorrelationConfig pairCorrelationConf = {
		.numBins = pairCorrelationBins,
		.maxR = worldSize / 2,
		.rho = numParticles / (twoDimensional ? SQUARE(worldSize)
		                                      : CUBE(worldSize)),
	};
	Measurement measurement;
	measurement.measConf = measConf;
	measurement.sampler = pairCorrelationSampler(&pairCorrelationConf);
	Task measTask = measurementTask(&measurement);

	/* Combined task */
	Task *tasks[3];
	tasks[0] = (render ? &renderTask : NULL);
	tasks[1] = &monteCarloTask;
	tasks[2] = &measTask;
	Task task = sequence(tasks, 3);

	bool everythingOK = run(&task);

	if (!everythingOK)
		return 1;
	return 0;
}

