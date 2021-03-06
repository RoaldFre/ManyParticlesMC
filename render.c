#include "render.h"

#ifdef NO_RENDER
/* =========== BUILD WITHOUT RENDERING =========== */

Task makeRenderTask(RenderConf *rc)
{
	UNUSED(rc);

	Task ret;
	ret.initialData = NULL;
	ret.start = NULL;
	ret.tick = NULL;
	ret.stop = NULL;

	return ret;
}
void registerString(RenderStringConfig *rsc)
{
	UNUSED(rsc);
}

#else //NO_RENDER
/* ============ BUILD WITH RENDERING ============ */

#include <stdio.h>
#include <SDL/SDL.h>
#include <GL/gl.h>
#include <math.h>
#include "world.h"
#include "system.h"
#include "task.h"
#include "font.h"
#include "mathlib/mathlib.h"

#define SCREEN_W 1000
#define SCREEN_H 1000

#define SPHERE_SLICES 10

typedef struct {
	GLfloat x, y, z;
} Vertex3;

const GLfloat light_pos[]  = {2.0, 1.0, 2.0, 0.0};
const GLfloat light_diff[] = {1.0, 1.0, 1.0, 0.0};
const GLfloat light_spec[] = {1.0, 0.0, 0.0, 0.0};
const GLfloat light_ambi[] = {0.8, 0.8, 0.8, 0.0};

const GLfloat red[]   = {1.0, 0.0, 0.0, 0.0};
const GLfloat green[] = {0.0, 1.0, 0.0, 0.0};
const GLfloat blue[]  = {0.0, 0.0, 1.0, 0.0};
const GLfloat gray[]  = {0.2, 0.2, 0.2, 0.0};

static int numVertices;
static int numIndices;
static Vertex3 *sphereVertex;
static GLushort *sphereIndex;

static Font *font;
static SDL_Surface *surface;
#define FPS_STRING_CHARS 32
static char fps_string[FPS_STRING_CHARS];
static Quaternion cam_orientation;
static Vec3 cam_position;

typedef struct stringList {
	RenderStringConfig rsc;
	struct stringList *next;
} StringList;

/* Global that holds a linked list to all the strings that should be 
 * rendered. */
static StringList *strings = NULL;

static void renderParticle(Particle *p, RenderConf *rc)
{
	glPushMatrix();
		glTranslatef(p->pos.x, p->pos.y, p->pos.z);
		glScalef(rc->radius, rc->radius, rc->radius);
		glDrawElements(GL_TRIANGLES, numIndices, GL_UNSIGNED_SHORT, sphereIndex);
	glPopMatrix();
}

static void createSphere(int slices, int *numVert, Vertex3 **vertices, int *numInd,
		GLushort **indices)
{
	int i, j, k;
	double x, y, z;
	double r;
	int stacks;
	Vertex3 *vert;
	GLushort *ind;

	stacks = slices;
	slices *= 2;

	/* Plus two for the poles */
	*numVert = (stacks - 1) * slices + 2;
	*vertices = calloc(*numVert, sizeof(Vertex3));
	vert = *vertices;

	/* All but the top and bottom stack */
	for (i = 1; i < stacks; i++)
	{
		double phi = M_PI * i / (double) stacks - 2*M_PI;
		
		z = cos(phi);
		r = sqrt(1 - z*z);

		for (j = 0; j < slices; j++)
		{
			double theta = 2*M_PI*j/(double) slices;
			x = r * sin(theta);
			y = r * cos(theta);

			vert[(i-1) * slices + j + 1].x = x;
			vert[(i-1) * slices + j + 1].y = y;
			vert[(i-1) * slices + j + 1].z = z;
		}
	}

	/* Top and bottom */
	vert[0].x = 0;
	vert[0].y = 0;
	vert[0].z = 1;

	vert[*numVert-1].x = 0;
	vert[*numVert-1].y = 0;
	vert[*numVert-1].z = -1;

	*numInd = (stacks - 1) * slices * 6;
	*indices = calloc(*numInd, sizeof(GLushort));
	ind = *indices;

	k = 0;

	for (i = 1; i < slices; i++)
	{
		ind[k++] = 0;
		ind[k++] = i;
		ind[k++] = i+1;
	}
	ind[k++] = 0;
	ind[k++] = 1;
	ind[k++] = slices;
	
	for (i = 0; i < slices - 1; i++)
	{
		ind[k++] = *numVert - 1;
		ind[k++] = (*numVert - 1 - slices) + i;
		ind[k++] = (*numVert - 1 - slices) + i + 1;
	}
	ind[k++] = *numVert - 1;
	ind[k++] = *numVert - 1 - 1;
	ind[k++] = *numVert - 1 - slices + 0;

	for (i = 1; i < stacks - 1; i++)
	{
		int base = 1 + (i - 1) * slices;

		for (j = 0; j < slices - 1; j++)
		{
			ind[k++] = base + j;
			ind[k++] = base + slices + j;
			ind[k++] = base + slices + j + 1;

			ind[k++] = base + j;
			ind[k++] = base + j + 1;
			ind[k++] = base + slices + j + 1;
		}

		ind[k++] = base;
		ind[k++] = base + slices - 1;
		ind[k++] = base + slices;

		ind[k++] = base + slices - 1;
		ind[k++] = base + slices;
		ind[k++] = base + slices + slices - 1;
	}

	return;
}

static int getIterationsPerSecond(void)
{
	static int ips = 0;
	static long tock = 0;
	static long prevIterations = 0;
	long tick;

	tick = SDL_GetTicks(); /* milliseconds */

	if (tick - tock > 1000) {
		int dt = tick - tock;
		tock = tick;
		long currIterations = getIteration();
		long deltaIterations = currIterations - prevIterations;
		prevIterations = currIterations;
		ips = deltaIterations * 1000 / dt;
	}
	return ips;
}
static void calcFps(void)
{
	static long tock = 0;
	static int frames = 0;
	long tick;

	frames++;
	tick = SDL_GetTicks(); /* milliseconds */

	if (tick - tock > 1000) {
		int dt = tick - tock;
		tock = tick;
		snprintf(fps_string, FPS_STRING_CHARS, "%u FPS",
				frames * 1000 / dt);
		SDL_WM_SetCaption(fps_string, fps_string);
		frames = 0;
	}
	return;
}

static void camOrbit(int dx, int dy)
{
	const double radius = 200;
	double dist;
	Vec3 v = cam_position;
	Quaternion o = cam_orientation;
	Quaternion q, q2;

	/* We invert the transformation because we are transforming the camera
	 * and not the scene. */
	q = quat_conjugate(quat_trackball(dx, dy, radius));

	/* The quaternion q gives us an intrinsic transformation, close to unity.
	 * To make it extrinsic, we compute q2 = o * q * ~o */
	q2 = quat_multiply(o, quat_multiply(q, quat_conjugate(o)));
	q2 = quat_normalize(q2);

	/* As round-off errors accumulate, the distance between the camera and the
	 * target would normally fluctuate. We take steps to prevent that here. */
	dist = vec3_length(v);
	v = quat_transform(q2, v);
	v = vec3_normalize(v);
	v = vec3_scale(v, dist);

	cam_position = v;
	cam_orientation = quat_multiply(q2, cam_orientation);
}

static void camDolly(int dz)
{
	Vec3 v = cam_position;

	v = vec3_scale(v, exp(-0.1*dz));
	cam_position = v;
}

/* Handle events.
 * Return true if everything went fine, false if user requested to quit. */
static bool handleEvents(void)
{
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_QUIT) {
			printf("\nRequested Quit.\n\n");
			return false;
		}
		else if (event.type == SDL_KEYDOWN) {
			switch (event.key.keysym.sym) {
			case SDLK_ESCAPE:
				printf("\nRequested Quit.\n\n");
				return false;
				break;
			case SDLK_RETURN:
				SDL_WM_ToggleFullScreen(surface);
				break;
			default:
				break;
			}
		}
		else if (event.type == SDL_MOUSEMOTION) {
			char ms = SDL_GetMouseState(NULL, NULL);
			/* We invert the y-coordinate because SDL has the origin in the
			 * top-left and OpenGL in the bottom-left */
			if (ms & SDL_BUTTON(SDL_BUTTON_LEFT))
				camOrbit(event.motion.xrel, -event.motion.yrel);
		}
		else if (event.type == SDL_MOUSEBUTTONDOWN) {
			if (event.button.button == SDL_BUTTON_WHEELUP)
				camDolly(1);
			else if (event.button.button == SDL_BUTTON_WHEELDOWN)
				camDolly(-1);
		}
	}
	return true;
}


static void gluPerspective(GLfloat fovy, GLfloat aspect, GLfloat zNear, 
		GLfloat zFar)
{
	GLfloat xMin, xMax, yMin, yMax;

	yMax = zNear * tan(fovy * M_PI / 360.0);
	yMin = -yMax;

	xMin = yMin * aspect;
	xMax = yMax * aspect;

	glFrustum(xMin, xMax, yMin, yMax, zNear, zFar);
}

/* Returns false if we couldn't initialize, true otherwise */
static void initRender(void)
{
	int flags = 0;
	const SDL_VideoInfo *vidinfo;

	font = font_load("fonts/Terminus.ttf");
	if (font == NULL)
		die("Font not loaded");

	if (SDL_Init(SDL_INIT_VIDEO) < 0)
		die(SDL_GetError());
		//TODO cleaner way
	
	vidinfo = SDL_GetVideoInfo();
	if (vidinfo == NULL)
		die(SDL_GetError());
	
	flags |= SDL_OPENGL;
	flags |= SDL_HWPALETTE;
	flags |= (vidinfo->hw_available ? SDL_HWSURFACE : SDL_SWSURFACE);
	if (vidinfo->blit_hw) flags |= SDL_HWACCEL;

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_SWAP_CONTROL, 1);

	surface = SDL_SetVideoMode(SCREEN_W, SCREEN_H, 24, flags);
	if (surface == NULL)
		die(SDL_GetError());

	SDL_EnableKeyRepeat(1, 
			SDL_DEFAULT_REPEAT_INTERVAL);

	/*SDL_WM_ToggleFullScreen(surface);	*/

	//atexit(SDL_Quit); //Do it when we stop our render task.

	/* OpenGL Init */
	glShadeModel(GL_SMOOTH);
	glEnable(GL_NORMALIZE);

	glEnable(GL_LIGHT0);
	glLightfv(GL_LIGHT0, GL_POSITION, light_pos);
	glLightfv(GL_LIGHT0, GL_DIFFUSE,  light_diff);
	glLightfv(GL_LIGHT0, GL_SPECULAR, light_spec);
	glLightfv(GL_LIGHT0, GL_AMBIENT,  light_ambi);

	glClearColor(1.0, 1.0, 1.0, 0.0);

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_NORMAL_ARRAY);

	createSphere(SPHERE_SLICES, &numVertices, &sphereVertex,
			&numIndices, &sphereIndex);
	glVertexPointer(3, GL_FLOAT, sizeof(Vertex3), sphereVertex);
	glNormalPointer(   GL_FLOAT, sizeof(Vertex3), sphereVertex);

	cam_position = (Vec3) {0, 0, world.worldSize * 2.5};
	cam_orientation = (Quaternion) {1, 0, 0, 0};
}

static void renderSet3D(void)
{
	double ws = world.worldSize;

	glEnable( GL_DEPTH_TEST);
	glEnable( GL_LIGHTING);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(35, SCREEN_W/(double)SCREEN_H, ws/1000, 100*ws);
}

static void renderSet2D(void)
{
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_LIGHTING);
	glEnable( GL_TEXTURE_2D);
	glEnable( GL_BLEND);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, SCREEN_W, 0, SCREEN_H, -1, +1);
}

static void mat4_from_mat3(double m[16], RenderMat3 n)
{
#define M(i, j) m[4*j + i]
#define N(i, j) n[3*j + i]
	M(0,0) = N(0,0); M(0,1) = N(0,1); M(0,2) = N(0,2); M(0,3) = 0.0;
	M(1,0) = N(1,0); M(1,1) = N(1,1); M(1,2) = N(1,2); M(1,3) = 0.0;
	M(2,0) = N(2,0); M(2,1) = N(2,1); M(2,2) = N(2,2); M(2,3) = 0.0;
	M(3,0) = 0.0;    M(3,1) = 0.0;    M(3,2) = 0.0;    M(3,3) = 1.0;
#undef M
#undef N
}

static void renderString(const char *str, int x, int y)
{
	/* TODO rework, this segfaults if font = 0 (obviously), see note 
	 * below to rework string rendering */
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glTranslatef(x, y, 0);
	text_create_and_render(font, 12, str);
}

/* Renders the frame and calls calcFps() */
static void render(RenderConf *rc)
{
	double ws = world.worldSize;
	RenderMat3 m3;
	double m4[16];

	calcFps();

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/* 3D */
	renderSet3D();

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	mat3_from_quat(m3, quat_conjugate(cam_orientation));
	mat4_from_mat3(m4, m3);
	glMultMatrixd(m4);
	glTranslatef(-cam_position.x, -cam_position.y, -cam_position.z);


	/* Line loops for world box */
	glBegin(GL_LINE_LOOP);
		glVertex3f(-ws/2, -ws/2, -ws/2);
		glVertex3f(-ws/2, -ws/2, +ws/2);
		glVertex3f(-ws/2, +ws/2, +ws/2);
		glVertex3f(-ws/2, +ws/2, -ws/2);
	glEnd();

	glBegin(GL_LINE_LOOP);
		glVertex3f(+ws/2, -ws/2, -ws/2);
		glVertex3f(+ws/2, -ws/2, +ws/2);
		glVertex3f(+ws/2, +ws/2, +ws/2);
		glVertex3f(+ws/2, +ws/2, -ws/2);
	glEnd();


	/* Particles */
	glLightfv(GL_LIGHT0, GL_DIFFUSE, gray);
	for (int p = 0; p < world.numParticles; p++)
		renderParticle(&world.particles[p], rc);


	/* Text */
	renderSet2D();
	const int n = 64;
	char string[n];

	int ips = getIterationsPerSecond();
	snprintf(string, n, "ips = %d", ips);
	renderString(string, 10, 10);

	glLoadIdentity();
	renderString(fps_string, 10, SCREEN_H - 10);

	StringList *node = strings;
	while (node != NULL) {
		renderString(node->rsc.string, node->rsc.x, node->rsc.y);
		node = node->next;
	}

	SDL_GL_SwapBuffers();
}

/* Render the image if we must, based on the requested framerate and the 
 * last invocation of this function. */
static void renderIfWeMust(RenderConf *rc)
{
	static long tock = -1000; /* always draw first frame immediately */

	if (rc->framerate <= 0) {
		render(rc);
		return;
	}

	long tick = SDL_GetTicks(); /* mili seconds */

	if (tick - tock > 1000 / rc->framerate) {
		tock = tick;
		render(rc);
	}
}


static void *renderTaskStart(void *initialData)
{
	assert(initialData != NULL);
	initRender();
	return initialData;
}

static TaskSignal renderTaskTick(void *state)
{
	RenderConf *rc = (RenderConf*) state;

	if (!handleEvents())
		return TASK_ERROR;
	
	renderIfWeMust(rc);
	return TASK_OK;
}

static void renderTaskStop(void *state)
{
	SDL_Quit();
	free(state);
}

Task makeRenderTask(RenderConf *rc)
{
	RenderConf *rcCopy = malloc(sizeof(*rcCopy));
	memcpy(rcCopy, rc, sizeof(*rcCopy));

	Task ret = {
		.initialData = rcCopy,
		.start = &renderTaskStart,
		.tick  = &renderTaskTick,
		.stop  = &renderTaskStop,
	};
	return ret;
}

void registerString(RenderStringConfig *rsc)
{
	StringList *node = malloc(sizeof(*node));
	node->rsc = *rsc; /* struct copy */
	node->next = strings;
	strings = node;
}

#endif // NO_RENDER
