#include "flgranular~.h"

/* The initialization routine *************************************************/
void ext_main(void *r)
{
	/* Initialize the class */
	fl_granular_class = class_new("flgranular~", (method)fl_granular_new, (method)fl_granular_free, (long)sizeof(t_fl_granular), 0, A_GIMME, 0);

	/* Bind the object-specific methods */
	class_addmethod(fl_granular_class, (method)fl_granular_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_assist, "assist", A_CANT, 0);

	class_addmethod(fl_granular_class, (method)fl_granular_nuevograno, "bang", 0);
	class_addmethod(fl_granular_class, (method)fl_granular_state, "int", A_LONG, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_float, "float", A_FLOAT, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_lista_ventana, "list", A_GIMME, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_transp, "ft1", A_FLOAT, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_pan, "ft2", A_FLOAT, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_durgrano, "ft3", A_FLOAT, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_rango, "ft4", A_FLOAT, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_inicio, "ft5", A_FLOAT, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_periodo, "ft6", A_FLOAT, 0);

	class_addmethod(fl_granular_class, (method)fl_granular_tuning, "tuning", A_GIMME, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_fadetime, "fadetime", A_GIMME, 0);
	class_addmethod(fl_granular_class, (method)fl_granular_fadetype, "fadetype", A_GIMME, 0);

	class_addmethod(fl_granular_class, (method)fl_granular_load_buffer, "load_buffer", A_SYM, A_LONG, 0);

	/* Register the class with Max */
	class_register(CLASS_BOX, fl_granular_class);
}

/* new and assist -----------------------------------------------------------------------------*/
void *fl_granular_new(t_symbol *s, short argc, t_atom *argv)
{
	t_fl_granular *x = (t_fl_granular *)object_alloc(fl_granular_class); 	/* Instantiate a new object */

	/* inlets max: bang/int, (7)periodo, (6)inicio, (5)rango inicio, (4)duracion, (3)lista grano, (2)pan, (1)transp */
	floatin(x, 1);
	floatin(x, 2);
	floatin(x, 3);
	floatin(x, 4);
	floatin(x, 5);
	floatin(x, 6);

	/* outlets msp */
	outlet_new((t_object *)x, "signal"); 
	outlet_new((t_object *)x, "signal");
	x->obj.z_misc |= Z_NO_INPLACE;		/* Avoid sharing memory among audio vectors */

	/* Initialize some state variables */
	x->grstate = GRSTATE_DEFAULT;
	x->samps_periodo = TIME_DEFAULT;
	x->contador = 0;

	x->fs = sys_getsr();

	x->source_chans = 0;
	x->source_sr = 0;
	x->source_chan_sel = 0;
	x->source_frames = 0;
	x->source_len = 0;
	x->source_old_len = 0;
	
	x->crossfade_time = DEFAULT_CROSSFADE;
	x->crossfade_type = LINEAR_CROSSFADE;
	x->crossfade_samples = (long)(x->crossfade_time * x->fs / 1000.0);
	x->crossfade_in_progress = 0;
	x->crossfade_countdown = 0;

	x->ventana_busy = 0;
	x->granos_activos_cero = 0;
	x->granos_activos_uno = 0;
	x->new_buf_is = 0;
	x->buffer_iniciado = 0;
	x->transp = TRANSP_DEFAULT;
	x->pan = PAN_DEFAULT;
	x->samps_grano = 0;
	x->samps_inicio = 0;
	x->samps_rango = 0;
	x->oct_mult = (float)OCT_MULT_DEFAULT;
	x->div_oct = (float)OCT_DIV_DEFAULT;
	x->ventana_iniciada = 0;

	/* memory for window; build curve */
	x->bytes_ventana = VENTANA_SIZE * sizeof(float);
	x->ventana = (float *)sysmem_newptr(x->bytes_ventana);
	if (x->ventana == NULL) { object_error((t_object *)x, "no hay espacio de memoria para ventana"); return x; }
	x->ventana_old = (float *)sysmem_newptr(x->bytes_ventana);
	if (x->ventana_old == NULL) { object_error((t_object *)x, "no hay espacio de memoria para ventana"); return x; }

	/* memory for grain window curve points; initialize */
	x->n_puntos = N_PUNTOS_DEFAULT;
	x->bytes_puntos_ventana = MAX_PUNTOS_VENTANA * 3 * sizeof(float);
	x->puntos_ventana = (float *)sysmem_newptr(x->bytes_puntos_ventana);
	if (x->puntos_ventana == NULL) { object_error((t_object *)x, "no hay espacio de memoria para puntos ventana"); return x; }
	else {
		x->puntos_ventana[0] = 0.0;
		x->puntos_ventana[1] = 0.0;
		x->puntos_ventana[2] = 0.0;
		x->puntos_ventana[3] = 1.0;
		x->puntos_ventana[4] = VENTANA_SIZE;
		x->puntos_ventana[5] = 0.5;
	}
	
	fl_granular_build_ventana(x);

	/* memory for grain structures */
	x->bytes_granos = MAX_GRANOS * sizeof(t_fl_grano);
	x->granos = (t_fl_grano *)sysmem_newptr(x->bytes_granos);
	if (x->granos == NULL) {
		object_error((t_object *)x, "no hay espacio de memoria para puntos curva");
		return x;
	}
	else {
		t_fl_grano *temp_ptr = x->granos;
		for (int i = 0; i < MAX_GRANOS; i++, temp_ptr++) {
			temp_ptr->busy_state = 0;
			temp_ptr->buf = 0;
			temp_ptr->ini_samps = 0;
			temp_ptr->cont_samps = 0;
			temp_ptr->dur_samps = 0;
			temp_ptr->pan = 0;
			temp_ptr->ratio = 0;
		}
	}

	/* initialize clock */
	srand((unsigned int)clock());
	x->m_clock = clock_new((t_object *)x, (method)fl_granular_nuevograno);

	return x;
}

void fl_granular_assist(t_fl_granular *x, void *b, long msg, long arg, char *dst)
{
	if (msg == ASSIST_INLET) { 
		switch (arg) {
		case I_BANG: sprintf(dst, "(bang) nuevo grano; (int) on/off state; (list) ventana (frmt curve: y, dx, c)"); break;
		case I_PERIODO: sprintf(dst, "(float) periodo en (ms)"); break;
		case I_INICIO: sprintf(dst, "(float) inicio (ms)"); break;
		case I_RANGOINICIO: sprintf(dst, "(float) rango inicio (ms)"); break;
		case I_DURGRANO: sprintf(dst, "(float) duracion grano (ms)"); break;
		case I_PAN: sprintf(dst, "(float) pan [0,1]"); break;
		case I_TRANSP: sprintf(dst, "(float) transposicion (semitonos)"); break;
		}
	}
	else if (msg == ASSIST_OUTLET) {
		switch (arg) {
		case O_AUDIOL: sprintf(dst, "(signal) audio left"); break;
		case O_AUDIOR: sprintf(dst, "(signal) audio right"); break;
		}
	}
}

/* memory ---------------------------------------------------------------------*/
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
}

/* granular--------------------------------------------------------------------- */
void fl_granular_float(t_fl_granular *x, float f) 
{}

void fl_granular_nuevograno(t_fl_granular *x)
{
	/* dont make a grain if there is no buffer or max reached */
	if (!x->buffer_iniciado) { object_warn((t_object *)x, "no hay buffer"); return; }
	if (x->granos_activos_cero + x->granos_activos_uno >= MAX_GRANOS) { object_warn((t_object *)x, "limite maximo de granos alcanzado"); return; }

	/* random beginning within range selected */
	long rand_inicio = x->samps_inicio;
	float frandom = (float)(((rand() % 201) / 100.0) - 1.0);
	rand_inicio += (long)(x->samps_rango * frandom);
	
	/* keep whole grain within buffer limits */
	long dur_grano = x->samps_grano;
	long source_len = x->source_len;
	if (rand_inicio < 0) { rand_inicio = 0; }
	if (dur_grano >= source_len) { dur_grano = source_len - 1; }
	if (rand_inicio + dur_grano >= source_len) { rand_inicio = x->source_len - dur_grano; }

	/* random pan within range selected */
	float prandom = (float)(((rand() % 101) / 100.0) - 0.5);
	float rand_pan = (float)(0.5 + x->pan * (double)prandom);

	/* transposition in semitones converted to ratio */
	float ratio = (float)(exp(log(x->oct_mult) * x->transp / x->div_oct));

	/* iterate until a non busy grain is found */
	t_fl_grano *temp_ptr = x->granos;
	for (int i = 0; i < MAX_GRANOS; i++, temp_ptr++) {
		if (temp_ptr->busy_state == 0) {
			temp_ptr->busy_state = 1;
			temp_ptr->ini_samps = rand_inicio;
			temp_ptr->cont_samps = 0;
			temp_ptr->dur_samps = dur_grano;
			temp_ptr->pan = rand_pan;
			temp_ptr->ratio = ratio;
			temp_ptr->buf = x->new_buf_is;
			break;
		}
	}
}

void fl_granular_state(t_fl_granular *x, long n) {
	if (!x->buffer_iniciado) { object_warn((t_object *)x, "no hay buffer"); return; }

	if (n == 1) { x->grstate = 1; }
	else if (n == 0) { x->grstate = 0; }
}
void fl_granular_periodo(t_fl_granular *x, double farg) {
	if (!x->buffer_iniciado) { object_warn((t_object *)x, "no hay buffer"); return; }

	if (farg <= TIEMPO_MIN) {
		farg = TIME_DEFAULT;
		object_warn((t_object *)x, "Invalid argument: set to %d(ms)", farg);
	}
	x->samps_periodo = (int)(farg * (x->fs) / 1000.0);
}

void fl_granular_inicio(t_fl_granular *x, double farg) {
	if (!x->buffer_iniciado) { object_warn((t_object *)x, "no hay buffer"); return; }

	if (farg < 0) {
		farg = INICIO_DEFAULT;
		object_warn((t_object *)x, "Invalid argument: set to %d(ms)", farg);
	}
	if (farg >= x->source_len) {
		farg = INICIO_DEFAULT;
		object_error((t_object *)x, "Inicio es mayor o igual que tamaño de la fuente: set to 0");
	}
	x->samps_inicio = (int)(farg * (x->fs) / 1000.0);
}
void fl_granular_rango(t_fl_granular *x, double farg) {
	if (!x->buffer_iniciado) { object_warn((t_object *)x, "no hay buffer"); return; }

	if (farg < TIEMPO_MIN) {
		farg = TIEMPO_MIN;
		object_warn((t_object *)x, "Invalid argument: set to %d(ms)", farg);
	}
	x->samps_rango = (int)(farg * (x->fs) / 1000.0);
}
void fl_granular_durgrano(t_fl_granular *x, double farg) {
	if (!x->buffer_iniciado) { object_warn((t_object *)x, "no hay buffer"); return; }

	if (farg < TIEMPO_MIN) {
		farg = TIEMPO_MIN;
		object_warn((t_object *)x, "Invalid argument: set to %d(ms)", farg);
	}
	x->samps_grano = (int)(farg * (x->fs) / 1000.0);
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
	x->pan = (float)farg;
}
void fl_granular_transp(t_fl_granular *x, double farg)
{
	x->transp = (float)farg;
}
void fl_granular_lista_ventana(t_fl_granular *x, t_symbol *s, long argc, t_atom *argv)
{
	if (argc % 3 != 0) { object_error((t_object *)x, "multiplo de 3"); return; }
	if (argc > MAX_PUNTOS_VENTANA * 3) { object_error((t_object *)x, "muchos puntos"); return; }

	if (!x->ventana_busy) {
		t_atom *aptr = argv;
		x->n_puntos = argc / 3;

		for (int i = 0; i < argc; i++, aptr++) {
			if (!((i + 1) % 3)) {
				x->puntos_ventana[i] = parse_curve((float)atom_getfloat(aptr));
			}
			else {
				x->puntos_ventana[i] = (float)atom_getfloat(aptr);
			}
		}

		/* normalize */
		float max_val = 1.;
		float min_val = 0.;
		for (int i = 0; i < x->n_puntos; i++) {
			if (max_val < x->puntos_ventana[3 * i]) {
				max_val = x->puntos_ventana[3 * i];
			}
			if (min_val > x->puntos_ventana[3 * i]) {
				min_val = x->puntos_ventana[3 * i];
			}
		}
		if (max_val > 1. || min_val < 0.) {
			float rescale = (float)(1. / (max_val - (double)min_val));
			for (int i = 0; i < x->n_puntos; i++) {
				x->puntos_ventana[3 * i] -= min_val;
				x->puntos_ventana[3 * i] *= rescale;
			}
		}

		if (x->ventana != NULL && x->ventana_old != NULL) { fl_granular_build_ventana(x); }
	}
}

/* messages ------------------------------------------------------------------------------------- */
void fl_granular_tuning(t_fl_granular *x, t_symbol *s, long argc, t_atom *argv) {
	if (argc != 2) {
		object_error((t_object *)x, "octava y division");
		return;
	}

	t_atom *aptr = argv;

	x->oct_mult = (float)atom_getfloat(aptr);
	x->div_oct = (float)atom_getfloat(aptr + 1);
}

void fl_granular_fadetime(t_fl_granular *x, t_symbol *msg, short argc, t_atom *argv)
{
	if (argc > 1) { return; }
	if (atom_gettype(argv) != A_FLOAT) { return; }
	float crossfade_ms = (float)atom_getfloat(argv);
#ifdef MAC_VERSION
    x->crossfade_time = (float)MIN(MAXIMUM_CROSSFADE, MAX(MINIMUM_CROSSFADE, crossfade_ms));
#endif
#ifdef WIN_VERSION
    x->crossfade_time = (float)min(MAXIMUM_CROSSFADE, max(MINIMUM_CROSSFADE, crossfade_ms));
#endif
	x->crossfade_samples = (long)(x->crossfade_time * x->fs / 1000.0);
	object_attr_touch((t_object *)x, gensym("fadetime"));
}

void fl_granular_fadetype(t_fl_granular *x, t_symbol *msg, short argc, t_atom *argv)
{
	if (argc > 1) { return; }
	if (atom_gettype(argv) != A_LONG) { return; }
	float crossfade_type = (short)atom_getfloat(argv);
#ifdef MAC_VERSION
    x->crossfade_type = (short)MIN(POWER_CROSSFADE, MAX(NO_CROSSFADE, crossfade_type));
#endif
#ifdef WIN_VERSION
    x->crossfade_type = (short)min(POWER_CROSSFADE, max(NO_CROSSFADE, crossfade_type));
#endif
	object_attr_touch((t_object *)x, gensym("fadetype"));
}

/* aux ------------------------------------------------------------------------------------------ */
float parse_curve(float curva) {
	if (curva < CURVE_MIN) {
		curva = (float)CURVE_MIN;
	}
	else if (curva > CURVE_MAX) {
		curva = (float)CURVE_MAX;
	}

	if (curva > 0.0) {
		return (float)(1.0 / (1.0 - curva));
	}
	else {
		return (float)(curva + 1.0);
	}
}

void fl_granular_build_ventana(t_fl_granular *x)
{
	float *ventana = x->ventana;
	float *ventana_old = x->ventana_old;

	if (x->crossfade_in_progress) { object_warn((t_object *)x, "crossfade in progress"); return; }
	
	/* backup window */
	if (x->ventana_iniciada) {
		for (int i = 0; i < VENTANA_SIZE; i++) {
			x->ventana_old[i] = x->ventana[i];
		}
	}

	x->ventana_busy = 1;

	/* Initialize crossfade */
	if (x->crossfade_type != NO_CROSSFADE && x->ventana_iniciada) {
		x->crossfade_countdown = x->crossfade_samples;
		x->crossfade_in_progress = 1;
	}
	else {
		x->crossfade_countdown = 0;
		x->crossfade_in_progress = 0;
	}

	fl_granular_build_curve(x);

	x->ventana_busy = 0;
	x->ventana_iniciada = 1;
}

void fl_granular_build_curve(t_fl_granular *x)
{
	float *ventana = x->ventana;
	float *puntos_ventana = x->puntos_ventana;
	int n = x->n_puntos;

	float accum = 0;
	int dom = 0;
	for (int i = 0; i < n; i++) {	//accum dominio function
		accum += puntos_ventana[i * 3 + 1];
	}
	dom = (int)accum;

	float x_i = 0.0;
	int j = 0;
	int k = 0;
	int segmento = -1;
	float y_i = 0.0;
	float y_f = 0.0;
	float curva = 0.5;

	for (int i = 0, j = 0; i < VENTANA_SIZE; i++, j++) {	//k:puntos i:samps_table j:samps_segm 
		if (j > segmento) {
			y_f = puntos_ventana[k * 3];
			segmento = (int)(puntos_ventana[k * 3 + 1] * VENTANA_SIZE / dom);
			curva = puntos_ventana[k * 3 + 2];
			y_i = puntos_ventana[((n + k - 1) % n) * 3];
			j = 0;
			k++;
		}
#ifdef MAC_VERSION
        x_i = (j / (float)MAX(segmento, 1));
#endif
#ifdef WIN_VERSION
        x_i = (j / (float)max(segmento, 1));
#endif

		x->ventana[i] = ((float)pow(x_i, curva)) * (y_f - y_i) + y_i;
	}
}

/* buffer -------------------------------------------------------------------------------------- */
void fl_granular_load_buffer(t_fl_granular *x, t_symbol *name, long n)
{
	t_buffer_obj *buffer;
	float *tab;

	/* don't load if busy */
	if (x->source_busy) { object_warn((t_object *)x, "already loading a new buffer"); }
	if ((x->granos_activos_cero > 0) && (x->granos_activos_uno > 0)) { object_warn((t_object *)x, "old buffer still in use"); }

	/* buffere reference */
	if (!x->l_buffer_reference) {
		x->l_buffer_reference = buffer_ref_new((t_object *)x, name); /* load new */
	}
	else {
		buffer_ref_set(x->l_buffer_reference, name); /* change */
	}
	
	/* secure buffer */
	buffer = buffer_ref_getobject(x->l_buffer_reference);
	tab = buffer_locksamples(buffer);
	if (!tab) { object_error((t_object *)x, "no se pudo cargar el buffer"); return; }

	/* backup buffer if loaded buffer isnt the first one to be loaded */
	long chunksize;
	if (x->buffer_iniciado && x->source != NULL) {
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
		
	x->source_busy = 1;
	
	/* get info of new buffer */
	long source_len = x->source_len = (long)buffer_getframecount(buffer);
	short source_chans = x->source_chans = (short)buffer_getchannelcount(buffer);
	x->source_sr = (float)buffer_getsamplerate(buffer);
	short source_chan_sel = x->source_chan_sel = (short)n;
	x->source_frames = x->source_frames * x->source_chan_sel;

	/* memory alloc for buffer content */
	chunksize = source_len * sizeof(float);
	x->bytes_source = chunksize;
	if (x->source == NULL) {
		x->source = (float *)sysmem_newptr(chunksize);
	}
	else {
		x->source = (float *)sysmem_resizeptr(x->source, chunksize);
	}

	/* error if no memory */
	if (x->source == NULL) { object_error((t_object *)x, "no hay memoria para copiar buffer");	return; }

	/* copy buffer content */
	for (int i = 0; i < source_len; i++) {
		x->source[i] = tab[i * source_chans + source_chan_sel - 1];
	}

	/* don't need the buffer anymore */
	buffer_unlocksamples(buffer);

	x->source_busy = 0;

	/* buffer identifier */
	x->new_buf_is = !(x->new_buf_is);

	/* is this the first buffer loaded? */
	if (!x->buffer_iniciado) { x->buffer_iniciado = 1; }

	/* info to user */
	object_post((t_object *)x, "buffer: %s \nchannels: %d \nselected chan: %d \nlength(smps): %d", name->s_name, x->source_chans, x->source_chan_sel, x->source_len);
}

/* audio ------------------------------------------------------------------------------- */
void fl_granular_dsp64(t_fl_granular *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
	/* initialize the remaining states variables */
	x->mean = 1.0;
	x->w = (float)MEAN_WEIGHT;

	/* adjust to changes in the sampling rate */
	x->fs = samplerate;

	/* attach object to the DSP chain. print message in max window */
	object_method(dsp64, gensym("dsp_add64"), x, fl_granular_perform64, 0, NULL);
}

void fl_granular_perform64(t_fl_granular *x, t_object *dsp64, double **inputs, long numinputs, double **outputs, long numoutputs, long vectorsize, long flags, void *userparams)
{
	/* Copy signal pointers and signal vector size */
	t_double *outputl = outputs[0];
	t_double *outputr = outputs[1];
	long n = vectorsize;

	t_fl_grano *ptr_grano = x->granos;

	/* load state variables */
	short granos_activos_cero = x->granos_activos_cero;
	short granos_activos_uno = x->granos_activos_uno;
	long source_len = x->source_len;
	long source_old_len = x->source_old_len;
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

	short crossfade_type = x->crossfade_type;
	long crossfade_samples = x->crossfade_samples;
	long crossfade_countdown = x->crossfade_countdown;

	short source_busy = x->source_busy;
	short ventana_busy = x->ventana_busy;
	short new_buf_is = x->new_buf_is;

	/* declare variables and make calculations */
	float samps_ratio = (float) (x->fs / x->source_sr);
	long index_ventana = 0;
	long index_buffer = 0;
	long ini_samps = 0;
	long cont_samps = 0;
	long samps_grano;
	float pan_grano;
	float ratio_grano;
	short buf_id;

	float old_ventana;
	float new_ventana;
	float fraction;

	float valor_source;
	float valor_ventana = 0.;
	double out_sample;
	double panned_out_l;
	double panned_out_r;

	/* Perform the DSP loop */
	while (n--) {

		out_sample = 0;
		panned_out_l = 0;
		panned_out_r = 0;

		/* auto mode on: clock new grain */
		if (grstate) {
			if (contador++ >= samps_periodo) {
				clock_delay(x->m_clock, 0);
				contador = 0;
			}
		}

		/* if buffer exist, iterate grains structs */
		if (buffer_iniciado) {
			ptr_grano = x->granos;
			granos_activos_cero = 0;
			granos_activos_uno = 0;
			for (int i=0 ; i < MAX_GRANOS; i++, ptr_grano++) {

				/* play grain if not played yet */
				if (ptr_grano->busy_state == 1) {
					
					cont_samps = ptr_grano->cont_samps;
					samps_grano = ptr_grano->dur_samps;
					pan_grano = ptr_grano->pan;
					ini_samps = ptr_grano->ini_samps;
					ratio_grano = ptr_grano->ratio;
					buf_id = ptr_grano->buf;

					if(buf_id){ ++granos_activos_uno; }
					else { ++granos_activos_cero; }

					/* don't play grain if duration reached */
					if (cont_samps > samps_grano) {
						ptr_grano->busy_state = 0;
						if (buf_id) { --x->granos_activos_uno; }
						else { --x->granos_activos_cero; }
						break;
					}
					
					/* buffer value depending on buffer state */
					index_buffer = ini_samps + (long)(cont_samps * ratio_grano * samps_ratio);
					if (source_busy || (buf_id != new_buf_is)) {	// loading new buffer
						valor_source = source_old[index_buffer%source_old_len];
					}
					else {											// just play
						valor_source = source[index_buffer%source_len];
					}

					/* window value depending on grain window state */
					index_ventana = (long)(((float)cont_samps/(float)samps_grano)*(VENTANA_SIZE-1.));
					if (ventana_busy) {	/* building a new window */
						valor_ventana = ventana_old[index_ventana];
					}
					else if (crossfade_countdown > 0) { /* xfade between old and new window */
						old_ventana = ventana_old[index_ventana];
						new_ventana = ventana[index_ventana];
						fraction = (float)crossfade_countdown / (float)crossfade_samples;

						if (crossfade_type == POWER_CROSSFADE) {
							fraction *= (float)PIOVERTWO;
							valor_ventana = (float)(sin(fraction) * old_ventana + cos(fraction) * new_ventana);
						}
						else if (crossfade_type == LINEAR_CROSSFADE) {
							valor_ventana = (float)(fraction * (double)old_ventana + (1 - (double)fraction) * new_ventana);
						}
						else {
							valor_ventana = old_ventana;
						}
						crossfade_countdown--;
					}
					else {	/* just play */
						valor_ventana = ventana[index_ventana];
					}

					/* output */
					out_sample = (double)valor_source * valor_ventana;

					panned_out_l += sqrt(1.0 - pan_grano)*out_sample / sqrt(mean);
					panned_out_r += sqrt(pan_grano)*out_sample / sqrt(mean);

					(ptr_grano->cont_samps)++;
				}
			}
			/* mean value */
#ifdef MAC_VERSION
            mean = (float)MIN(1., w * ((double)granos_activos_uno + granos_activos_cero) + (1.0 - (double)w) * mean);
#endif
#ifdef WIN_VERSION
            mean = (float)min(1., w * ((double)granos_activos_uno + granos_activos_cero) + (1.0 - (double)w) * mean);
#endif
		}
		*outputl++ = panned_out_l;
		*outputr++ = panned_out_r;
	}
	
	if (crossfade_countdown <= 0) { x->crossfade_in_progress = 0; }

	/* update state variables */
	x->crossfade_countdown = crossfade_countdown;
	x->granos_activos_cero = granos_activos_cero;
	x->granos_activos_uno = granos_activos_uno;
	x->mean = mean;
	x->contador = contador;
}


