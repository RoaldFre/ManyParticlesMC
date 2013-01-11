#include "measure.h"
#include "render.h"
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>

/* STUFF FOR REDIRECTING STDOUT. POSIX-ONLY. */

typedef struct {
	int newfd; /* fd we want to switch to */
} StreamState;

static StreamState makeStreamState(const char *filename)
{
	StreamState s;
	s.newfd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (s.newfd < 0) {
		perror("Error opening file");
		assert(false);
	}
	return s;
}

/* Switch the current stdout to the stream given in s.
 * At the end of this function: s->fd is an fd to stdout as it was when 
 * calling this fuction; s->pos is the position in that stream an that 
 * time.
 * If s is NULL, or s.newfd < 0: do noting. */
static void switchStdout(StreamState *s)
{
	if (s == NULL  ||  s->newfd < 0)
		return;
	
	fflush(stdout);
	int oldStdout = dup(1);
	dup2(s->newfd, 1);
	close(s->newfd);
	s->newfd = oldStdout;
}

/* Flushes the new stream in s and closes it. */
static void stopRedirection(StreamState *s)
{
	if (s == NULL  ||  s->newfd < 0)
		return;
	close(s->newfd);
	s->newfd = 0;
}




/* SOME WRAPPERS FOR SAMPLERS: */

typedef struct measTaskState
{
	Sampler sampler;
	void *samplerState;
	SamplerData samplerData;
	enum {RELAXING, SAMPLING} measStatus;
	MeasurementConf measConf;
	double intervalTime; /* Time since last sample (or start). */
	StreamState streamState; /* For stdout redirection */
} MeasTaskState;

/* Returns the state pointer that gets returned from sampler.start(), or 
 * NULL if sampler.start == NULL */
static void *samplerStart(MeasTaskState *measState)
{
	Sampler *sampler = &measState->sampler;
	StreamState *streamState = &measState->streamState;
	void *ret;

	switchStdout(streamState); /* Switch stdout to file */
	if (measState->measConf.measureHeader != NULL)
		printf("%s", measState->measConf.measureHeader);

	if (sampler->header != NULL)
		printf("%s", sampler->header);

	if (sampler->start == NULL)
		ret = NULL;
	else
		ret = sampler->start(&measState->samplerData, 
					sampler->samplerConf);

	switchStdout(streamState); /* Switch stdout back */

	return ret;
}

/* Sample and return sampler.sample(), or do nothing if sampler.sample == 
 * NULL and return SAMPLER_OK. Note that this would be a pretty useless sampler 
 * in the latter case... */
static SamplerSignal samplerSample(MeasTaskState *measState)
{
	Sampler *sampler = &measState->sampler;
	StreamState *streamState = &measState->streamState;

	if (sampler->sample == NULL)
		return SAMPLER_OK;

	switchStdout(streamState); /* Switch stdout to file */
	SamplerSignal ret = sampler->sample(&measState->samplerData, 
				measState->samplerState);
	switchStdout(streamState); /* Switch stdout back */

	return ret;
}

/* Stop the sampler if we were currently sampling, do nothing otherwise or 
 * if sampler.stop == NULL */
static void samplerStop(MeasTaskState *measState)
{
	Sampler *sampler = &measState->sampler;
	StreamState *streamState = &measState->streamState;

	if (sampler->stop == NULL  ||  measState->measStatus != SAMPLING)
		return;

	switchStdout(streamState); /* Switch stdout to file */
	sampler->stop(&measState->samplerData, measState->samplerState);
	switchStdout(streamState); /* Switch stdout back */
}






/* MEASUREMENT TASK STUFF */

typedef struct {
	Measurement meas;
	char *strBuf; /* The buffer for the rendering string if applicable */
} MeasInitialData;

static void *measStart(void *initialData)
{
	MeasInitialData *mid = (MeasInitialData*) initialData;
	Measurement *meas = &mid->meas;

	if (meas->measConf.measureInterval <= 0) {
		/* Nothing to do */
		free(mid->strBuf);
		free(mid);
		return NULL;
	}

	assert(meas != NULL);
	MeasTaskState *state = malloc(sizeof(*state));

	if (meas->measConf.measureFile != NULL) {
		state->streamState = makeStreamState(
				meas->measConf.measureFile);
	} else {
		state->streamState.newfd = -1;
	}

	state->intervalTime = (meas->measConf.measureWait > 0 ?
			0 : meas->measConf.measureInterval);
			/* TODO (So we start sampling immediately) */
	state->sampler = meas->sampler; /* struct copy */
	state->measConf = meas->measConf; /* struct copy */
	state->measStatus = (meas->measConf.measureWait > 0 ?
				RELAXING : SAMPLING);
	state->samplerData.sample = 0;
	state->samplerData.strBufSize = meas->measConf.renderStrBufSize;
	state->samplerData.string = mid->strBuf;
	state->samplerData.sampleInterval = meas->measConf.measureInterval;

	/* If we don't wait to relax: start sampler now */
	if (state->measStatus == SAMPLING)
		state->samplerState = samplerStart(state);

	free(initialData);
	return state;
}

static TaskSignal measTick(void *state)
{
	if (state == NULL)
		return TASK_OK;

	MeasTaskState *measState = (MeasTaskState*) state;
	MeasurementConf *measConf = &measState->measConf;
	long measWait     = measConf->measureWait;
	long measInterval = measConf->measureInterval;
	long measTime     = measConf->measureTime;
	long endTime      = measTime + measWait;
	bool verbose      = measConf->verbose;
	long time         = getIteration();

	SamplerSignal samplerSignal = SAMPLER_OK;

	if (measInterval < 0)
		return TASK_OK;


	switch (measState->measStatus) {
	case RELAXING:
		if (verbose && (time % MAX(measWait/100, 1)) == 0 ) {
			printf("\rRelax time %ld of %ld", time, measWait);
			fflush(stdout);
		}
		if (time >= measWait) {
			measState->intervalTime = time - measWait;
			if (verbose)
				printf("\nStarting measurement.\n");

			/* Start the sampler */
			measState->samplerState = samplerStart(measState);

			measState->measStatus = SAMPLING;
			/* bit of a hack to start sampling immediately: */
			measState->intervalTime = measInterval;
		}
		break;
	case SAMPLING:
		measState->intervalTime++;

		if (measState->intervalTime < measInterval)
			break;

		if (verbose) {
			if (measTime > 0)
				printf("\rSampling at iteration %ld of %ld", 
						time, endTime);
			else
				printf("\rSampling at iteration %ld", time);
			fflush(stdout);
		}

		measState->intervalTime -= measInterval;
		samplerSignal = samplerSample(measState);
		measState->samplerData.sample++;
		
		if (measTime < 0)
			break; /* Go on indefinitely, don't print anything */

		fflush(stdout);
		if (time >= endTime) {
			if (verbose)
				printf("\nFinished sampling period!\n");
			return TASK_STOP;
		}
		break;
	default:
		fprintf(stderr, "Unknown measurement status!\n");
		assert(false);
	}

	switch(samplerSignal) {
		case SAMPLER_OK:
			return TASK_OK;
		case SAMPLER_STOP:
			if (verbose)
				printf("\nSampler requested polite quit.\n");
			return TASK_STOP;
		case SAMPLER_ERROR:
			if (verbose)
				printf("\nSampler encountered error!\n");
			return TASK_ERROR;
		default:
			fprintf(stderr, "Received unknown sampler signal!");
			assert(false);
			return TASK_ERROR;
	}
}

static void measStop(void *state)
{
	if (state == NULL)
		return;

	MeasTaskState *measState = (MeasTaskState*) state;
	samplerStop(measState);

	stopRedirection(&measState->streamState);

	free(measState->samplerData.string);
	free(measState);
}


Task measurementTask(Measurement *measurement)
{
	/* We must make a copy because given pointer is not guaranteed to 
	 * remain valid. */
	MeasInitialData *mid = malloc(sizeof(*mid));
	memcpy(&mid->meas, measurement, sizeof(*measurement));
	mid->strBuf = NULL;

	/* Make task */
	Task task;
	task.initialData = mid;
	task.start = &measStart;
	task.tick  = &measTick;
	task.stop  = &measStop;

	int strSize = measurement->measConf.renderStrBufSize;
	if (strSize <= 0)
		return task; /* No string stuff necessary */

	/* Also create a render task to render the string */
	char *string = malloc(strSize);
	string[0] = '\0';
	mid->strBuf = string;
	RenderStringConfig rsc;
	rsc.string = string;
	rsc.x = measurement->measConf.renderStrX;
	rsc.y = measurement->measConf.renderStrY;

	registerString(&rsc);

	return task;
}

