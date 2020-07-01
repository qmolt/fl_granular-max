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

#define MAX_GRANOS 50
#define VENTANA_SIZE 1024
#define SIDES_MIN 0.0
#define SIDES_DEFAULT 0.0
#define SIDES_MAX 0.98
#define TILT_MIN 0.05
#define TILT_DEFAULT 0.5
#define TILT_MAX 0.95
#define PAN_MIN 0.0
#define PAN_DEFAULT 0.5
#define PAN_MAX 1.0
#define TRANSP_DEFAULT 0.0
#define CURVE_MIN 0.01
#define CURVE_DEFAULT 0.5 
#define CURVE_MAX 0.95
#define TIEMPO_MIN 0
#define DURGRAMP_DEFAULT 1000
#define INICIO_DEFAULT 0
#define MEAN_WEIGHT 0.001

enum INLETS { I_BANG, I_INICIO, I_RANGOINICIO, I_DURGRANO, I_SIDES, I_TILT, I_CURVELEFT, I_CURVERIGHT, I_PAN, I_TRANSP, NUM_INLETS };
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

	double fs;

	short buffercopiado;
	int granos_activos;

	int samps_inicio;
	int samps_rango;
	int samps_grano;
	float sides;
	float tilt;
	float curvader;
	float curvaizq;
	float pan;
	float transp;

	long bytes_ventana;
	float *ventana;
	long bytes_source;
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
void fl_granular_inicio(t_fl_granular *x, double farg);
void fl_granular_rango(t_fl_granular *x, double farg);
void fl_granular_durgrano(t_fl_granular *x, double farg);
void fl_granular_sides(t_fl_granular *x, double farg);
void fl_granular_tilt(t_fl_granular *x, double farg);
void fl_granular_curveleft(t_fl_granular *x, double farg);
void fl_granular_curveright(t_fl_granular *x, double farg);
void fl_granular_pan(t_fl_granular *x, double farg);
void fl_granular_transp(t_fl_granular *x, double farg);

void fl_granular_dsp64(t_fl_granular *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
t_int *fl_granular_perform64(t_fl_granular *x, t_object *dsp64, double **inputs, long numinputs, double **outputs, long numoutputs, long vectorsize, long flags, void *userparams);
void fl_granular_assist(t_fl_granular *x, void *b, long msg, long arg, char *dst);

void fl_granular_load_buffer(t_fl_granular *x, t_symbol *name, long n);
void fl_granular_build_ventana(t_fl_granular *x);

/* The initialization routine *************************************************/
int C74_EXPORT main()
{
	/* Initialize the class */
	fl_granular_class = class_new("fl_granular~", (method)fl_granular_new, (method)fl_granular_free, (long)sizeof(t_fl_granular), 0, A_GIMME, 0);

	/* Bind the object-specific methods */
	class_addmethod(fl_granular_class, (method)fl_granular_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_transp, "ft1", A_FLOAT, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_pan, "ft2", A_FLOAT, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_curveright, "ft3", A_FLOAT, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_curveleft, "ft4", A_FLOAT, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_tilt, "ft5", A_FLOAT, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_sides, "ft6", A_FLOAT, 0); 
	class_addmethod(fl_granular_class, (method)fl_granular_durgrano, "ft7", A_FLOAT, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_rango, "ft8", A_FLOAT, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_inicio, "ft9", A_FLOAT, 0);
	
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
	floatin(x, 3);
	floatin(x, 4);
	floatin(x, 5);
	floatin(x, 6);
	floatin(x, 7);
	floatin(x, 8);
	floatin(x, 9);

	/* inlets y outlets msp */
	/*dsp_setup((t_pxobject *)x, 0);*/ 	/* msp inlets */
	outlet_new((t_object *)x, "signal"); 								/* Create signal outlets */
	outlet_new((t_object *)x, "signal");
	x->obj.z_misc |= Z_NO_INPLACE;  									/* Avoid sharing memory among audio vectors */

	/* Parse passed argument */
	/*atom_arg_getsym(&x->b_name, 0, argc, argv);*/

	/* Initialize some state variables */
	x->fs = sys_getsr();

	x->granos_activos = 0;
	x->buffercopiado = 0;

	x->curvaizq = CURVE_DEFAULT;
	x->curvader = CURVE_DEFAULT;
	x->transp = TRANSP_DEFAULT;
	x->pan = PAN_DEFAULT;
	x->tilt = TILT_DEFAULT;
	x->sides = SIDES_DEFAULT;
	x->samps_grano = 0;
	x->samps_inicio = 0;
	x->samps_rango = 0;

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

	/* Print message to Max window and return */
	object_post((t_object *)x, "Object was created");
	return x;
}

void fl_granular_free(t_fl_granular *x)
{
	/* Free allocated dynamic memory */
	sysmem_freeptr(x->source);
	sysmem_freeptr(x->ventana);
	sysmem_freeptr(x->granos);

	/* Print message to Max window */
	object_post((t_object *)x, "Object was deleted");
}

/* The 'assist' method ********************************************************/
void fl_granular_assist(t_fl_granular *x, void *b, long msg, long arg, char *dst)
{
	if (msg == ASSIST_INLET) {
		switch (arg) {
		case I_BANG: sprintf(dst, "(bang) bang");
			break;
		case I_INICIO: sprintf(dst, "(float) inicio (samps)");
			break;
		case I_RANGOINICIO: sprintf(dst, "(float) rango inicio (samps)");
			break;
		case I_DURGRANO: sprintf(dst, "(float) duracion grano (samps)");
			break;
		case I_SIDES: sprintf(dst, "(float) sides [0,1]");
			break;
		case I_TILT: sprintf(dst, "(float) tilt [0,1]");
			break;
		case I_CURVELEFT: sprintf(dst, "(float) curva izq (0,1)");
			break;
		case I_CURVERIGHT: sprintf(dst, "(float) curva der (0,1)");
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
void fl_granular_sides(t_fl_granular *x, double farg)
{
	if (farg < SIDES_MIN) {
		farg = SIDES_MIN;
		object_warn((t_object *)x, "Invalid argument: sides set to %.3f", farg);
	}
	else if (farg > SIDES_MAX) {
		farg = SIDES_MAX;
		object_warn((t_object *)x, "Invalid argument: sides set to %.3f", farg);
	}
	x->sides = farg;
	fl_granular_build_ventana(x);
}

void fl_granular_tilt(t_fl_granular *x, double farg)
{
	if (farg < TILT_MIN) {
		farg = TILT_MIN;
		object_warn((t_object *)x, "Invalid argument: tilt set to %.3f", farg);
	}
	else if (farg > TILT_MAX) {
		farg = TILT_MAX;
		object_warn((t_object *)x, "Invalid argument: tilt set to %.3f", farg);
	}
	x->tilt = farg;
	fl_granular_build_ventana(x);
}
void fl_granular_curveleft(t_fl_granular *x, double farg) 
{
	if (farg < CURVE_MIN) {
		farg = CURVE_MIN;
		object_warn((t_object *)x, "Invalid argument: left curve set to %1.1f", farg);
	}
	else if (farg > CURVE_MAX) {
		farg = CURVE_MAX;
		object_warn((t_object *)x, "Invalid argument: left curve set to %1.1f", farg);
	}
	x->curvaizq = farg;
	fl_granular_build_ventana(x);
}
void fl_granular_curveright(t_fl_granular *x, double farg) 
{
	if (farg < CURVE_MIN) {
		farg = CURVE_MIN;
		object_warn((t_object *)x, "Invalid argument: right curve set to %1.1f", farg);
	}
	else if (farg > CURVE_MAX) {
		farg = CURVE_MAX;
		object_warn((t_object *)x, "Invalid argument: right curve set to %1.1f", farg);
	}
	x->curvader = farg;
	fl_granular_build_ventana(x);
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
	t_buffer_obj *buffer;
	float *tab;
	int chan;

	if (!x->l_buffer_reference)
		x->l_buffer_reference = buffer_ref_new((t_object *)x, name); //carga con el nombre
	else
		buffer_ref_set(x->l_buffer_reference, name); //cambia de buffer

	buffer = buffer_ref_getobject(x->l_buffer_reference);
	tab = buffer_locksamples(buffer);

	if (!tab) { 
		object_post((t_object *)x,"no se pudo cargar el buffer");
		return;
	}

	x->source_frames = buffer_getframecount(buffer);
	x->source_chans = buffer_getchannelcount(buffer);
	x->source_chan_sel = n;
	x->source_len = x->source_frames / x->source_chans;
	object_post((t_object *)x, "frames %d, chans %d, chansel %d, len %d", x->source_frames,x->source_chans,x->source_chan_sel,x->source_len);

	long chunksize = x->source_len * sizeof(float);
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
	x->buffercopiado = 1;
}

void fl_granular_build_ventana(t_fl_granular *x)
{
	float xi;
	float ai;
	float ad;
	float curvaizq = x->curvaizq;
	float curvader = x->curvader;
	float tilt = x->tilt;
	float sides = x->sides;
	float fadein = (tilt * (1.0 - sides)*(VENTANA_SIZE));
	float fadeout = ((1.0 - tilt)*(1.0 - sides)*(VENTANA_SIZE));
	float fadein_and_sides = VENTANA_SIZE - fadeout;

	if (curvaizq > 0.5) {
		ai = 1.0 / (1.0 - sqrt(2.0*(curvaizq - 0.5)));
	}
	else {
		ai = 2.0*curvaizq;
	}
	if (curvader > 0.5) {
		ad = 1.0 / (1.0 - sqrt(2.0*(curvader - 0.5)));
	}
	else {
		ad = 2.0*curvader;
	}

	if (!x->ventana) { 
		object_post((t_object *)x, "no hay ventana");
		return; 
	}

	for (int ii = 0; ii < VENTANA_SIZE; ii++) {
		if (ii < (int)fadein) {
			xi = (ii / fadein);
			x->ventana[ii] = pow(xi, ai);
		}
		else if (ii > (int)fadein_and_sides) {
			xi = ((ii-fadein_and_sides) / fadeout);
			x->ventana[ii] = pow(1.0 - xi, ad);
		}
		else {
			x->ventana[ii] = 1.0;
		}
	}
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
	if (x->buffercopiado) {
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
	float *ventana = x->ventana;
	short buffercargado = x->buffercopiado;
	float mean = x->mean;
	float w = x->w;

	/* declare variables and make calculations */
	int index_ventana = 0;
	int ini_samps = 0;
	int cont_samps = 0;
	int samps_grano;
	float pan_grano;
	float transp_grano;
	int inc = 0;

	double out_sample;
	double panned_out_l;
	double panned_out_r;

	/* Perform the DSP loop */
	while (n--) {

		out_sample = 0;
		panned_out_l = 0;
		panned_out_r = 0;

		if (buffercargado) {
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
					out_sample = (*(source + ini_samps + (int)(cont_samps * transp_grano)) * *(ventana + index_ventana));;

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
}