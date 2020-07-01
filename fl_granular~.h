#include "ext.h"
#include "ext_obex.h"
#include "buffer.h"
#include "z_dsp.h"
#include "z_sampletype.h"
#include <time.h>
#include <stdlib.h>
#include <math.h>

#define OCT_MULT_DEFAULT 2.
#define OCT_DIV_DEFAULT 12.
#define CURVE_MIN (-0.98)
#define CURVE_MAX 0.98

#define MINIMUM_CROSSFADE 0.0
#define DEFAULT_CROSSFADE 200.0
#define MAXIMUM_CROSSFADE 10000.0
#define NO_CROSSFADE 0
#define LINEAR_CROSSFADE 1
#define POWER_CROSSFADE 2

#define GRSTATE_DEFAULT 0
#define TIME_DEFAULT 48000
#define MAX_PUNTOS_VENTANA 20
#define N_PUNTOS_DEFAULT 2
#define MAX_GRANOS 50
#define VENTANA_SIZE 1024
#define PAN_MIN 0.0
#define PAN_DEFAULT 0.5
#define PAN_MAX 1.0
#define TRANSP_DEFAULT 0.0
#define TIEMPO_MIN 0
#define DURGRAMP_DEFAULT 1000
#define INICIO_DEFAULT 0
#define MEAN_WEIGHT 0.001

enum INLETS { I_BANG, I_PERIODO, I_INICIO, I_RANGOINICIO, I_DURGRANO, I_LVENTANA, I_PAN, I_TRANSP, NUM_INLETS };
enum OUTLETS { O_AUDIOL, O_AUDIOR, NUM_OUTLETS };

static t_class *fl_granular_class;

/* grano */
typedef struct _fl_grano {

	short busy_state;
	int ini_samps;
	int dur_samps;
	int cont_samps;
	float pan;
	float transp;

} t_fl_grano;

/* object */
typedef struct _fl_granular {
	t_pxobject obj;

	short grstate;
	long samps_periodo;
	void *m_clock;
	long contador;

	double fs;

	float crossfade_time;
	short crossfade_type;
	short crossfade_in_progress;
	long crossfade_countdown;
	long crossfade_samples;
	short just_turned_on;

	short buffer_iniciado;
	int granos_activos;

	int samps_inicio;
	int samps_rango;
	int samps_grano;
	long bytes_puntos_ventana;
	float *puntos_ventana;
	int n_puntos;
	float pan;
	float transp;
	float oct_mult;
	float div_oct;

	short ventana_busy;
	long bytes_ventana;
	float *ventana_old;
	float *ventana;

	short source_busy;
	long bytes_source;
	float *source_old;
	float *source;

	int source_frames;
	int source_chans;
	int source_chan_sel;
	int source_len;

	t_buffer_ref *l_buffer_reference;

	long bytes_granos;
	t_fl_grano *granos;

	float mean;
	float w;

} t_fl_granular;

/* function prototypes */
	/* max stuff */
void *fl_granular_new(t_symbol *s, short argc, t_atom *argv);
void fl_granular_assist(t_fl_granular *x, void *b, long msg, long arg, char *dst);

	/* memory */
void fl_granular_free(t_fl_granular *x);

	/* inlets */
void fl_granular_nuevograno(t_fl_granular *x);
void fl_granular_state(t_fl_granular *x, long n);
void fl_granular_periodo(t_fl_granular *x, double farg);
void fl_granular_inicio(t_fl_granular *x, double farg);
void fl_granular_rango(t_fl_granular *x, double farg);
void fl_granular_durgrano(t_fl_granular *x, double farg);
void fl_granular_pan(t_fl_granular *x, double farg);
void fl_granular_transp(t_fl_granular *x, double farg);
void fl_granular_lista_ventana(t_fl_granular *x, t_symbol *s, long argc, t_atom *argv);

	/*messages*/
void fl_granular_tuning(t_fl_granular *x, t_symbol *s, long argc, t_atom *argv);
void fl_granular_fadetime(t_fl_granular *x, t_symbol *msg, short argc, t_atom *argv);
void fl_granular_fadetype(t_fl_granular *x, t_symbol *msg, short argc, t_atom *argv);

	/* aux */
float parse_curve(float curva);
void fl_granular_build_ventana(t_fl_granular *x);

	/* buffer */
void fl_granular_load_buffer(t_fl_granular *x, t_symbol *name, long n);

	/* audio */
void fl_granular_dsp64(t_fl_granular *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
t_int *fl_granular_perform64(t_fl_granular *x, t_object *dsp64, double **inputs, long numinputs, double **outputs, long numoutputs, long vectorsize, long flags, void *userparams);
