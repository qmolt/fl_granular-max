/* Header files required by Max ***********************************************/
#include "ext.h"
#include "ext_obex.h"
#include "buffer.h"
#include "z_dsp.h"
#include "z_sampletype.h"
#include <time.h>
#include <stdlib.h>
#include <math.h>

#define LN2O12 0.057762265
#define CURVE_MIN (-0.98)
#define CURVE_MAX 0.98

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

/* The class pointer **********************************************************/
static t_class *fl_granular_class;

/* Estructura grano ***********************************************************/
typedef struct _fl_grano {

	short busy_state;
	int ini_samps;
	int dur_samps;
	int cont_samps;
	float pan;
	float transp;

} t_fl_grano;

/* The object structure *******************************************************/
typedef struct _fl_granular {
	t_pxobject obj;

	short grstate;
	long samps_periodo;
	void *m_clock;
	long contador;

	double fs;

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

/* Function prototypes ********************************************************/
void *fl_granular_new(t_symbol *s, short argc, t_atom *argv);
void fl_granular_free(t_fl_granular *x);

void fl_granular_nuevograno(t_fl_granular *x);
void fl_granular_state(t_fl_granular *x, long n);
void fl_granular_periodo(t_fl_granular *x, double farg);
void fl_granular_inicio(t_fl_granular *x, double farg);
void fl_granular_rango(t_fl_granular *x, double farg);
void fl_granular_durgrano(t_fl_granular *x, double farg);
void fl_granular_pan(t_fl_granular *x, double farg);
void fl_granular_transp(t_fl_granular *x, double farg);
void fl_granular_lista_ventana(t_fl_granular *x, t_symbol *s, long argc, t_atom *argv);
void fl_granular_dsp64(t_fl_granular *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
t_int *fl_granular_perform64(t_fl_granular *x, t_object *dsp64, double **inputs, long numinputs, double **outputs, long numoutputs, long vectorsize, long flags, void *userparams);
void fl_granular_assist(t_fl_granular *x, void *b, long msg, long arg, char *dst);

void fl_granular_load_buffer(t_fl_granular *x, t_symbol *name, long n);
void fl_granular_build_ventana(t_fl_granular *x);

float parse_curve(float curva);

/* The initialization routine *************************************************/
int C74_EXPORT main()
{
	/* Initialize the class */
	fl_granular_class = class_new("fl_granular~", (method)fl_granular_new, (method)fl_granular_free, (long)sizeof(t_fl_granular), 0, A_GIMME, 0);

	/* Bind the object-specific methods */
	class_addmethod(fl_granular_class, (method)fl_granular_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_state, "int", A_LONG, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_transp, "ft1", A_FLOAT, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_pan, "ft2", A_FLOAT, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_lista_ventana, "list", A_GIMME, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_durgrano, "ft4", A_FLOAT, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_rango, "ft5", A_FLOAT, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_inicio, "ft6", A_FLOAT, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_periodo, "ft7", A_FLOAT, 0);
	
	class_addmethod(fl_granular_class, (method)fl_granular_assist, "assist", A_CANT, 0);

	class_addmethod(fl_granular_class, (method)fl_granular_nuevograno, "bang", 0);

	class_addmethod(fl_granular_class, (method)fl_granular_load_buffer, "load_buffer", A_SYM, A_LONG, 0);

	/* Register the class with Max */
	class_register(CLASS_BOX, fl_granular_class);

	/* Print message to Max window */
	object_post(NULL, "fl_granular • External was loaded");

	/* Return with no error */
	return 0;
}

/* The new and free instance routines *****************************************/
void *fl_granular_new(t_symbol *s, short argc, t_atom *argv)
{
	/* Instantiate a new object */
	t_fl_granular *x = (t_fl_granular *)object_alloc(fl_granular_class);

	/* inlets y outlets max */	//inlet: bang, inicio, final, tiempo_fade, vtilt, vcleft, vcright, pan
	floatin(x, 1);
	floatin(x, 2);
	inlet_new(x, "list");
	floatin(x, 4);
	floatin(x, 5);
	floatin(x, 6);
	floatin(x, 7);

	/* inlets y outlets msp */
	/*dsp_setup((t_pxobject *)x, 0);*/ 	/* msp inlets */
	outlet_new((t_object *)x, "signal"); 								/* Create signal outlets */
	outlet_new((t_object *)x, "signal");
	x->obj.z_misc |= Z_NO_INPLACE;  									/* Avoid sharing memory among audio vectors */

	/* Parse passed argument */
	/*atom_arg_getsym(&x->b_name, 0, argc, argv);*/

	/* Initialize some state variables */
	x->grstate = GRSTATE_DEFAULT;
	x->samps_periodo = TIME_DEFAULT;
	x->contador = 0;

	x->fs = sys_getsr();

	x->ventana_busy = 0;
	x->granos_activos = 0;
	x->buffer_iniciado = 0;
	x->transp = TRANSP_DEFAULT;
	x->pan = PAN_DEFAULT;
	x->samps_grano = 0;
	x->samps_inicio = 0;
	x->samps_rango = 0;

	x->n_puntos = N_PUNTOS_DEFAULT;
	x->bytes_puntos_ventana = MAX_PUNTOS_VENTANA * 3 * sizeof(float);
	x->puntos_ventana = (float *)sysmem_newptr(x->bytes_puntos_ventana);
	if (x->puntos_ventana == NULL) {
		object_error((t_object *)x, "no hay espacio de memoria para puntos ventana");
	}
	else {
		x->puntos_ventana[0] = 1.0;
		x->puntos_ventana[1] = (VENTANA_SIZE - 1) / 2;
		x->puntos_ventana[2] = 0.5;
		x->puntos_ventana[3] = 0.0;
		x->puntos_ventana[4] = (VENTANA_SIZE - 1) / 2;
		x->puntos_ventana[5] = 0.5;
	}

	x->bytes_ventana = VENTANA_SIZE * sizeof(float);
	x->ventana = (float *)sysmem_newptr(x->bytes_ventana);
	if (x->ventana == NULL) {
		object_error((t_object *)x, "no hay espacio de memoria para ventana");
	}
	else {
		fl_granular_build_ventana(x);
	}

	x->bytes_granos = MAX_GRANOS * sizeof(t_fl_grano);
	x->granos = (t_fl_grano *)sysmem_newptr(x->bytes_granos);
	if (x->granos == NULL) {
		object_error((t_object *)x, "no hay espacio de memoria para granos");
	}
	else {
		t_fl_grano *temp_ptr = x->granos;
		for (int i = 0; i < MAX_GRANOS; i++, temp_ptr++) {
			temp_ptr->busy_state = 0;
			temp_ptr->ini_samps = 0;
			temp_ptr->cont_samps = 0;
			temp_ptr->dur_samps = 0;
			temp_ptr->pan = 0;
			temp_ptr->transp = 0;
		}
	}

	srand((unsigned int)clock());

	x->m_clock = clock_new((t_object *)x, (method)fl_granular_nuevograno);

	/* Print message to Max window and return */
	object_post((t_object *)x, "Object was created");
	return x;
}

void fl_granular_free(t_fl_granular *x)
{
	/* Free allocated dynamic memory */
	sysmem_freeptr(x->puntos_ventana);
	sysmem_freeptr(x->source_old);
	sysmem_freeptr(x->ventana_old);
	sysmem_freeptr(x->source);
	sysmem_freeptr(x->ventana);
	sysmem_freeptr(x->granos);

	object_free(x->m_clock);

	/* Print message to Max window */
	object_post((t_object *)x, "Object was deleted");
}

/* The 'assist' method ********************************************************/
void fl_granular_assist(t_fl_granular *x, void *b, long msg, long arg, char *dst)
{
	if (msg == ASSIST_INLET) {
		switch (arg) {
		case I_BANG: sprintf(dst, "(bang) nuevo grano, (int) on/off state");
			break;
		case I_PERIODO: sprintf(dst, "(float) periodo en (ms)");
			break;
		case I_INICIO: sprintf(dst, "(float) inicio (samps)");
			break;
		case I_RANGOINICIO: sprintf(dst, "(float) rango inicio (samps)");
			break;
		case I_DURGRANO: sprintf(dst, "(float) duracion grano (samps)");
			break;
		case I_LVENTANA: sprintf(dst, "(list) lista ventana (formato curve: y, x, c)");
			break;
		case I_PAN: sprintf(dst, "(float) pan [0,1]");
			break;
		case I_TRANSP: sprintf(dst, "(float) transposicion en semitonos");
			break;
		}
	}
	else if (msg == ASSIST_OUTLET) {
		switch (arg) {
		case O_AUDIOL: sprintf(dst, "(signal) audio left");
			break;
		case O_AUDIOR: sprintf(dst, "(signal) audio right");
			break;
		}
	}
}

/******************************************************************************/
void fl_granular_lista_ventana(t_fl_granular *x, t_symbol *s, long argc, t_atom *argv)
{
	if (argc % 3 != 0) {
		object_error((t_object *)x, "multiplo de 3");
		return;
	}

	if (argc > MAX_PUNTOS_VENTANA * 3) {
		object_error((t_object *)x, "muchos puntos");
		return;
	}

	if (!x->ventana_busy) {
		t_atom *aptr = argv;
		x->n_puntos = argc / 3;

		for (int i = 0; i < argc; i++, aptr++) {
			if (!((i+1)%3)) {
				x->puntos_ventana[i] = parse_curve(atom_getfloat(aptr));
			}
			else {
				x->puntos_ventana[i] = atom_getfloat(aptr);
			}
		}

		fl_granular_build_ventana(x);
	}
}

void fl_granular_pan(t_fl_granular *x, double farg)
{
	if (farg < PAN_MIN) {
		farg = PAN_MIN;
		object_warn((t_object *)x, "Invalid number: pan set to %1.1f", farg);
	}
	else if (farg > PAN_MAX) {
		farg = PAN_MAX;
		object_warn((t_object *)x, "Invalid number: Pan set to %1.1f", farg);
	}
	x->pan = farg;
}
void fl_granular_transp(t_fl_granular *x, double farg)
{
	x->transp = farg;
}

void fl_granular_state(t_fl_granular *x, long n) {
	if (n == 1) {
		x->grstate = 1;
		/*object_warn((t_object *)x, "encendido");*/
	}
	else if (n == 0) {
		x->grstate = 0;
	}
}

void fl_granular_periodo(t_fl_granular *x, double farg) {
	if (farg <= TIEMPO_MIN) {
		farg = TIME_DEFAULT;
		object_warn((t_object *)x, "Invalid argument: set to %d(samps)", farg);
	}
	x->samps_periodo = (int)(farg*(x->fs) / 1000.0);
}

void fl_granular_inicio(t_fl_granular *x, double farg) {
	if (farg < TIEMPO_MIN) {
		farg = TIEMPO_MIN;
		object_warn((t_object *)x, "Invalid argument: set to %d(samps)", farg);
	}
	x->samps_inicio = (int)(farg*(x->fs)/1000.0);
}
void fl_granular_rango(t_fl_granular *x, double farg) {
	if (farg < TIEMPO_MIN) {
		farg = TIEMPO_MIN;
		object_warn((t_object *)x, "Invalid argument: set to %d(samps)", farg);
	}
	x->samps_rango = (int)(farg * (x->fs) / 1000.0);
}
void fl_granular_durgrano(t_fl_granular *x, double farg) {
	if (farg < TIEMPO_MIN) {
		farg = TIEMPO_MIN;
		object_warn((t_object *)x, "Invalid argument: set to %d(samps)", farg);
	}
	x->samps_grano = (int)(farg * (x->fs) / 1000.0);
}

/* The object-specific methods ************************************************/

/******************************************************************************/
void fl_granular_load_buffer(t_fl_granular *x, t_symbol *name, long n)
{
	if (!x->source_busy) {
		t_buffer_obj *buffer;
		float *tab;
		int chan;

		if (!x->l_buffer_reference) {
			x->l_buffer_reference = buffer_ref_new((t_object *)x, name); //carga con el nombre
		}
		else {
			buffer_ref_set(x->l_buffer_reference, name); //cambia de buffer
		}

		buffer = buffer_ref_getobject(x->l_buffer_reference);
		tab = buffer_locksamples(buffer);

		if (!tab) {
			object_post((t_object *)x, "no se pudo cargar el buffer");
			return;
		}

		long chunksize;
		if (x->buffer_iniciado) {
			chunksize = x->source_len * sizeof(float);
			if (x->source_old == NULL) {
				x->source_old = (float *)sysmem_newptr(x->bytes_source);
			}
			else {
				x->source_old = (float *)sysmem_resizeptr(x->source_old, chunksize);
			}

			if (x->source == NULL) {
				object_error((t_object *)x, "no hay memoria para buffer antiguo");
				return;
			}

			for (int i = 0; i < x->source_len; i++) {
				x->source_old[i] = x->source[i];
			}
		}

		x->source_frames = buffer_getframecount(buffer);
		x->source_chans = buffer_getchannelcount(buffer);
		x->source_chan_sel = n;
		x->source_len = x->source_frames / x->source_chans;

		x->source_busy = 1;

		chunksize = x->source_len * sizeof(float);
		x->bytes_source = chunksize;
		if (x->source == NULL) {
			x->source = (float *)sysmem_newptr(chunksize);
		}
		else {
			x->source = (float *)sysmem_resizeptr(x->source, chunksize);
		}

		if (x->source == NULL) {
			object_error((t_object *)x, "no hay memoria para copiar buffer");
			return;
		}

		else {
			for (int i = 0; i < x->source_frames; i++) {
				if (x->source_chan_sel == 1) {
					i *= x->source_chans;
					x->source[i] = tab[i*(x->source_chans)];
				}
				else if (x->source_chan_sel == 2) {
					x->source[i] = tab[i*(x->source_chans) + 1];
				}
			}
		}

		buffer_unlocksamples(buffer);

		x->source_busy = 0;
		if (!x->buffer_iniciado) {
			x->buffer_iniciado = 1;
		}

		object_post((t_object *)x, "chans %d, chansel %d, len %d", x->source_chans, x->source_chan_sel, x->source_len);
	}
}

float parse_curve(float curva) {
	if (curva < CURVE_MIN) {
		curva = CURVE_MIN;
	}
	else if (curva > CURVE_MAX) {
		curva = CURVE_MAX;
	}

	if (curva > 0.0) {
		return (1.0 / (1.0 - curva));
	}
	else {
		return (curva + 1.0);
	}
}

void fl_granular_build_ventana(t_fl_granular *x)
{
	if (!x->ventana) {
		object_post((t_object *)x, "no hay ventana");
		return;
	}

	if (x->ventana_old == NULL) {
		x->ventana_old = (float *)sysmem_newptr(x->bytes_ventana);
	}
	for (int i = 0; i < VENTANA_SIZE; i++) {
		x->ventana_old[i] = x->ventana[i];
	}

	if (x->ventana_old == NULL) {
		object_error((t_object *)x, "no hay espacio para ventana antigua");
		return;
	}

	x->ventana_busy = 1;

	/*	yfk=argv[k3],	xfk=argv[k3+1],		ck=argv[k3+2]	*/
	int n = x->n_puntos;
	int k = 0;
	int j = 0;
	float xi = 0;
	int dom = 0;
	float *puntos_ventana = x->puntos_ventana;

	for (int i = 0; i < n; i++) {
		dom += (int)puntos_ventana[i*3 + 1];
	}

	int segmento = (int)(puntos_ventana[1] * VENTANA_SIZE / dom); 	//k*3+1, k=0
	float final = puntos_ventana[0]; 								//k*3, k=0
	float inicio = 0;
	float curva = puntos_ventana[2];

	for (int i = 0; i < VENTANA_SIZE; i++, j++) {
		if (j > segmento) {
			k++;
			segmento = (int)(puntos_ventana[k * 3 + 1] * VENTANA_SIZE / dom);
			inicio = puntos_ventana[(k - 1) * 3];
			final = puntos_ventana[k * 3];
			curva = puntos_ventana[k * 3 + 2];
			j = 0;
		}
		xi = (j / (float)segmento);
		x->ventana[i] = pow(xi, curva)*(final - inicio) + inicio;
	}

	x->ventana_busy = 0;
}

void fl_granular_nuevograno(t_fl_granular *x)
{
	if (x->samps_inicio >= x->source_len) {
		x->samps_inicio = 0;
		object_error((t_object *)x, "inicio es mayor o igual que tamaño de la fuente. valor reajustado a 0");
	}

	int rand_inicio = x->samps_inicio;
	float frandom = (float)(((rand() % 201) / 100.0) - 1.0);
	rand_inicio += (int)(x->samps_rango * frandom);
	rand_inicio *= x->source_chans;

	if (rand_inicio < 0) {
		rand_inicio += x->source_len;
	}
	else if (rand_inicio > x->source_len) {
		rand_inicio -= x->source_len;
	}

	float prandom = (float)(((rand() % 101) / 100.0) - 0.5);
	float rand_pan = 0.5 + (x->pan) * prandom;

	float transp = exp(LN2O12*x->transp);

	t_fl_grano *temp_ptr = x->granos;
	if (x->buffer_iniciado) {
		if (x->granos_activos <= MAX_GRANOS) {
			for (int i = 0; i < MAX_GRANOS; i++, temp_ptr++) {
				if (temp_ptr->busy_state == 0) {
					temp_ptr->busy_state = 1;
					temp_ptr->ini_samps = rand_inicio;
					temp_ptr->cont_samps = 0;
					temp_ptr->dur_samps = x->samps_grano;
					temp_ptr->pan = rand_pan;
					temp_ptr->transp = transp;
					break;
				}
			}
		}
		else {
			object_post((t_object *)x, "limite maximo de granos alcanzado");
		}
	}
	else {
		object_post((t_object *)x, "no hay buffer");
	}
}

void fl_granular_dsp64(t_fl_granular *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
	/* initialize the remaining states variables */
	x->mean = 1.0;
	x->w = MEAN_WEIGHT;

	/* adjust to changes in the sampling rate */
	x->fs = samplerate;

	/* attach object to the DSP chain. print message in max window */
	object_method(dsp64, gensym("dsp_add64"), x, fl_granular_perform64, 0, NULL);
	object_post((t_object *)x, "Executing 64-bit perform routine");
}

t_int *fl_granular_perform64(t_fl_granular *x, t_object *dsp64, double **inputs, long numinputs, double **outputs, long numoutputs, long vectorsize, long flags, void *userparams)
{
	/* Copy signal pointers and signal vector size */
	t_double *outputl = outputs[0];
	t_double *outputr = outputs[1];
	long n = vectorsize;

	t_fl_grano *ptr_grano = x->granos;

	/* load state variables */
	int granos_activos = x->granos_activos;
	int source_len = x->source_len;
	int nchans = x->source_chans;
	float *source = x->source;
	float *source_old = x->source_old;
	float *ventana = x->ventana;
	float *ventana_old = x->ventana_old;
	short buffer_iniciado = x->buffer_iniciado;
	float mean = x->mean;
	float w = x->w;
	short grstate = x->grstate;
	long contador = x->contador;
	long samps_periodo = x->samps_periodo;

	/* declare variables and make calculations */
	int index_ventana = 0;
	int ini_samps = 0;
	int cont_samps = 0;
	int samps_grano;
	float pan_grano;
	float transp_grano;
	int inc = 0;

	float valor_source;
	float valor_ventana;
	double out_sample;
	double panned_out_l;
	double panned_out_r;

	/* Perform the DSP loop */
	while (n--) {

		out_sample = 0;
		panned_out_l = 0;
		panned_out_r = 0;

		if (grstate) {
			if (contador++ >= samps_periodo) {
				clock_delay(x->m_clock, 0);
				contador = 0;
			}
		}

		if (buffer_iniciado) {
			ptr_grano = x->granos;
			granos_activos = 0;
			for (int i=0 ; i < MAX_GRANOS; i++, ptr_grano++) {
				if (ptr_grano->busy_state == 1) {
					++granos_activos;

					cont_samps = ptr_grano->cont_samps;
					samps_grano = ptr_grano->dur_samps;
					pan_grano = ptr_grano->pan;
					ini_samps = ptr_grano->ini_samps;
					transp_grano = ptr_grano->transp;

					if (cont_samps > samps_grano) {
						ptr_grano->busy_state = 0;
						--granos_activos;
						break;
					}
					
					if (ini_samps + cont_samps > source_len) {
						ptr_grano->ini_samps = ini_samps = 0;
					}

					if (inc = ini_samps + (int)(transp_grano*cont_samps) > source_len) {
						inc -= source_len;
					}
					
					index_ventana = (int)((cont_samps/(float)samps_grano)*(VENTANA_SIZE-1));
					if (x->source_busy) {
						valor_source = *(source_old + ini_samps + (int)(cont_samps * transp_grano));
					}
					else {
						valor_source = *(source + ini_samps + (int)(cont_samps * transp_grano));
					}
					if (x->ventana_busy) {
						valor_ventana = *(ventana_old + index_ventana);
					}
					else {
						valor_ventana = *(ventana + index_ventana);
					}
					out_sample = valor_source * valor_ventana;

					panned_out_l += sqrt(1.0 - pan_grano)*out_sample / sqrt(mean);
					panned_out_r += sqrt(pan_grano)*out_sample / sqrt(mean);

					(ptr_grano->cont_samps)++;
				}
			}
			mean = w * granos_activos + (1.0 - w)*mean;
			if (mean < 1.0) { mean = 1.0; }
		}
		*outputl++ = panned_out_l;
		*outputr++ = panned_out_r;
	}
	/* update state variables */
	x->granos_activos = granos_activos;
	x->mean = mean;
	x->contador = contador;
}


