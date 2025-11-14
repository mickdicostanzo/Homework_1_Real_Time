/*
    Real-Time Programming Homework 1
    Signal Generation, Filtering and Storage using POSIX real-time extensions
    filter.c: Signal filtering module
    PARTECIPANTI:
    - Annunziata Giovanni              DE6000015
    - Di Costanzo Michele Pio          DE6000001
    - Di Palma Lorenzo                 N39001908 
    - Zaccone Amedeo                   DE6000014 
    
    Autonomous System regna.
*/

/* 
        TO DO LIST:
        1. Implentare MSE CALCULATOR
        2. Implementare scrittura su terminale dell'MSE CALCULATOR
        3. Implementare filtro di Savitzky-Golay (OPTIONAL - SFIZIO PER MICHELE)
        4. iMPLEMTARE WATCHDOG (OPTIONAL)
*/


#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include "rt-lib.h"
#include <string.h>
#include <mqueue.h>

#define SIG_SAMPLE SIGRTMIN
#define SIG_HZ 1
#define OUTFILE "signal.txt"
#define F_SAMPLE 50
#define HZ_FILT 50
#define PERIOD_US (1000000L / F_SAMPLE)
#define SIZEQ 10
#define MSG_SIZE 256
#define Q_STORE "/print_q"
#define QMSE_STORE "/mse_q"
#define WG_QUEUE "/watchdog_queue"
#define QUEUE_PERMISSIONS 0660
#define MSE_SAMPLE 1 
#define BUFF_DIM 50
#define PERIOD_US_MSE (1000000L / MSE_SAMPLE)

#define SG_WINDOW_SIZE 5
// Coefficienti CAUSALI per: Finestra=5, Ordine=2 [i, i-1, i-2, i-3, i-4]
double SG_COEFFS_W5_P2[] = {
    0.88571429, 0.25714286, -0.08571429, -0.14285714, 0.08571429
};

#define USAGE_STR               \
    "Usage: %s [-s] [-n] [-f] [-m | -b | -z]\n"    \
    "\t -s: plot original signal\n"     \
    "\t -n: plot noisy signal\n"        \
    "\t -f: plot filtered signal\n"     \
    "\t -m: use mean filter\n"          \
    "\t -b: use butterworth filter\n"   \
    "\t -z: use Savitzky-Golay filter\n" \
    ""
	
#define BUTTERFILT_ORD 2
double b [3] = {0.0134,    0.0267,    0.0134};
double a [3] = {1.0000,   -1.6475,    0.7009};

static int first_mean=0;
int flag_signal = 0;
int flag_noise = 0;
int flag_filtered = 0;
int flag_type = 0; // 1:mean, 2:butterworth 
unsigned long sample_index = 0;
double sig_noise;
double sig_val;

double period_mse = 1/MSE_SAMPLE * NSEC_PER_SEC;

pthread_mutex_t mutex;
pthread_mutex_t mse_mutex_gen;
pthread_mutex_t mse_mutex_filt;

double glob_time = 0.0;
double last_timestamp = 0;
double buffer_gen[BUFF_DIM];
double buffer_filt[BUFF_DIM];

/*
    struct queue_{
    mqd_t store;
    mqd_t wg;
};typedef struct queue_ code_c;
*/


double get_butter(double cur, double * a, double * b)
{
	double retval;
	int i;

	static double in[BUTTERFILT_ORD+1];
	static double out[BUTTERFILT_ORD+1];
	
	// Perform sample shift
	for (i = BUTTERFILT_ORD; i > 0; --i) {
		in[i] = in[i-1];
		out[i] = out[i-1];
	}
	in[0] = cur;

	// Compute filtered value
	retval = 0;
	for (i = 0; i < BUTTERFILT_ORD+1; ++i) {
		retval += in[i] * b[i];
		if (i > 0)
			retval -= out[i] * a[i];
	}
	out[0] = retval;

	return retval;
}

double get_mean_filter(double cur)
{
    double retval;

	static double vec_mean[2];
	
	// Perform sample shift
	vec_mean[1] = vec_mean[0];
	vec_mean[0] = cur;

	//printf("in[0]: %f, in[1]: %f\n", in[0], in[1]); //DEBUG

	// Compute filtered value
	if (first_mean == 0){
		retval = vec_mean[0];
		++first_mean;
	}
	else{
		retval = (vec_mean[0] + vec_mean[1])/2;	
	}
	return retval;
}


double get_sg_filter(double cur)
{
    // Coefficienti e finestra definiti globalmente
    static double history[SG_WINDOW_SIZE] = {0.0};
    static int sg_startup_count = 0; // Simile a 'first_mean'
    
    double retval = 0.0;
    int i;

    // 1. Shifta la storia (i campioni più vecchi vanno verso la fine)
    for (i = SG_WINDOW_SIZE - 1; i > 0; i--) {
        history[i] = history[i-1];
    }
    
    // 2. Inserisci il campione corrente in posizione 0
    history[0] = cur;

    // 3. Gestione Startup: finché non abbiamo 'SG_WINDOW_SIZE' campioni,
    //    restituiamo il campione originale per evitare calcoli errati.
    if (sg_startup_count < SG_WINDOW_SIZE) {
        sg_startup_count++;
        return cur; // Restituisce l'originale finché il buffer non è pieno
    }

    // 4. Applica il filtro (convoluzione)
    //    history[0] = campione t
    //    history[1] = campione t-1
    //    ...
    for (i = 0; i < SG_WINDOW_SIZE; i++) {
        retval += SG_COEFFS_W5_P2[i] * history[i];
    }

    return retval;
}


void generator_thread_body(){
    static int h=0;
    double buff_loc[BUFF_DIM]= {0.0};
	// Generate signal
    // 
	double sig_val_l= sin(2*M_PI*SIG_HZ*glob_time);

	// Add noise to signal
	double sig_noise_l= sig_val_l + 0.5*cos(2*M_PI*10*glob_time);
	sig_noise_l += 0.9*cos(2*M_PI*4*glob_time);
	sig_noise_l += 0.9*cos(2*M_PI*12*glob_time);
	sig_noise_l+= 0.8*cos(2*M_PI*15*glob_time);
	sig_noise_l += 0.7*cos(2*M_PI*18*glob_time);

    pthread_mutex_lock(&mutex);
    sig_val = sig_val_l;
    sig_noise = sig_noise_l;
    glob_time += (1.0/F_SAMPLE); 
    pthread_mutex_unlock(&mutex);
    buff_loc[h%BUFF_DIM] = sig_val_l;
    if(h%BUFF_DIM ==0){
        pthread_mutex_lock(&mse_mutex_gen);
        /*for(int i=0; i<BUFF_DIM; i++){
            buffer_gen[i]=buff_loc[i];
        }
            */
        memcpy(buffer_gen, buff_loc, BUFF_DIM * sizeof(double));
        pthread_mutex_unlock(&mse_mutex_gen);
    }
    ++h;
    //pthread_mutex_lock(&mse_mutex_gen);
    //buffer_gen[h%BUFF_DIM] = sig_val;
    //h++;
    //pthread_mutex_unlock(&mse_mutex_gen);
   
}

void* generator_thread(void * arg){
    periodic_thread * thd = (periodic_thread *) arg;
    start_periodic_timer(thd, thd->period);
    while(1){
        wait_next_activation(thd);
        generator_thread_body();
    }
}

void filter_thread_body(mqd_t coda){
    //static unsigned long last_processed_index = 0;
    double sig_filt;
    static int h=0;
    double buff_loc[BUFF_DIM] ={0.0};
    //static int count =0;
    //= last_timestamp;
    //unsigned long local_index;
    pthread_mutex_lock(&mutex);
    double sig_noise_l = sig_noise;
    double sig_val_l = sig_val;
    double time_l = glob_time;
    //local_index   = sample_index;
    pthread_mutex_unlock(&mutex);
    //buff_loc[h%BUFF_DIM] = sig_filt;
    
    /*
    if (local_index == last_processed_index && last_processed_index !=0) {
        return;
    }
    last_processed_index = local_index;
    */
    
    // Apply Filter to signal
    if(flag_type== 2){
        //double sig_filt = get_butter(sig_noise, a, b);
        sig_filt = get_butter(sig_noise_l, a, b);
    }
    else if(flag_type == 3){
        sig_filt = get_sg_filter(sig_noise_l);
    }
    else{
        sig_filt = get_mean_filter(sig_noise_l);
    }

    buff_loc[h%BUFF_DIM] = sig_filt;
    if(h%50 ==0){
        
        pthread_mutex_lock(&mse_mutex_filt);
        /*
        for(int j=0; j<BUFF_DIM; j++){
            buffer_filt[j] = buff_loc[j];
        }
        */
       
        memcpy(buffer_filt, buff_loc, BUFF_DIM * sizeof(double));
        pthread_mutex_unlock(&mse_mutex_filt);
    }
    ++h;
    // Debug
    //printf("tempo: %lf, sig_val: %lf, sig_noise: %lf, sig_filter: %lf\n", time_l, sig_val_l,sig_noise_l, sig_filt);
   

    char msg[MSG_SIZE];
    char msg_signale[20];
    char msg_noise[20];
    char msg_filt[20];
    int n;
    
   
    //n = snprintf(msg, sizeof(msg), "%lf %lf %lf %lf\n",time_l, sig_val_l, sig_noise_l, sig_filt);  
    double val_to_send   = flag_signal  ? sig_val_l   : NAN;
    double noise_to_send = flag_noise  ? sig_noise_l : NAN;
    double filt_to_send  = flag_filtered ? sig_filt  : NAN;

    // 3. Formatta la stringa in una sola riga, usando gli SPAZI.
    //    Questa è la versione semplice e sicura.
    n = snprintf(msg, sizeof(msg), "%lf %lf %lf %lf\n", 
                     time_l, 
                     val_to_send, 
                     noise_to_send, 
                     filt_to_send);
    
if (n < 0 || n >= (int)sizeof(msg)) {
    fprintf(stderr, "Filter: message truncated or snprintf error\n");
    return;
}

    if (mq_send (coda, msg, n + 1, 0) == -1) {
        perror ("Filter: mq_send");
        exit (1);
    }

    /*
    char wg_msg[MSG_SIZE]="I'm Alive";

    if(count %BUFF_DIM==0){
        if (mq_send (watch_dog, msg, n + 1, 0) == -1) {
        perror ("Filter: mq_send");
        exit (1);
    }
    }
    ++count;

    */
    
    //pthread_mutex_lock(&mse_mutex_filt);
    //buffer_filt[h%BUFF_DIM] = sig_filt;
    //h++;
    //pthread_mutex_unlock(&mse_mutex_filt);
}

void* filter_thread(void * arg){
    periodic_thread * thd = (periodic_thread *) arg;
    //mqd_t q_store;
    struct mq_attr attr;

    //attr.mq_flags |= O_NONBLOCK;
    //attr.mq_maxmsg = SIZEQ;
    //attr.mq_msgsize = MSG_SIZE;
    //attr.mq_curmsgs = 0;

    mqd_t q_store;
    //mqd_t q_wg;

     if ((q_store = mq_open (Q_STORE, O_WRONLY)) == -1) {
        perror ("Filter: mq_open (store)");
        exit (1);
    }

    /*
    if ((q_wg = mq_open (WG_QUEUE, O_WRONLY)) == -1) {
        perror ("Filter: mq_open (watch dog)");
        exit (1);
    }
    */
    start_periodic_timer(thd, thd->period);
    while(1){
        wait_next_activation(thd);
        filter_thread_body(q_store);
        // if char = boh -> print, break
        
        /*
        if(getchar() == 'q'){
            printf("uscita dal thread avvenuta con successo\n");
            pthread_exit((mqd_t *)q_store);
        }
        */
    }
}

void mse_calc_thread_body(mqd_t coda){

    double signal_original[BUFF_DIM] = {0.0};
    double signal_filtered[BUFF_DIM] = {0.0};
    double mse =0.0;

    pthread_mutex_lock(&mse_mutex_gen);
    memcpy(signal_original, buffer_gen, BUFF_DIM * sizeof(double));
    pthread_mutex_unlock(&mse_mutex_gen);

    pthread_mutex_lock(&mse_mutex_filt);
    memcpy(signal_filtered, buffer_filt, BUFF_DIM * sizeof(double));
    pthread_mutex_unlock(&mse_mutex_filt);

    for(int j=0; j<BUFF_DIM; ++j){
        double diff = signal_filtered[j] - signal_original[j];
        mse += (diff * diff); 
    }
    mse = mse/BUFF_DIM;

    char msg[MSG_SIZE];
    int n =snprintf(msg, sizeof(msg), "%lf\n", mse);
    if (n < 0 || n >= (int)sizeof(msg)) {
        fprintf(stderr, "MSE Calculator: message truncated or snprintf error\n");
        return;
    }
    if(mq_send(coda,msg,n+1,0)==-1){
        perror("MSE CALCULATOR: errore nella send");
        exit(1);
    }
}

void * mse_calc_thread(void * arg){
    periodic_thread * thd = (periodic_thread *) arg;
    mqd_t mse_store;

    if((mse_store=mq_open(QMSE_STORE,O_WRONLY| O_NONBLOCK,QUEUE_PERMISSIONS))==-1){
        perror("MSE Calculator: mq_open (mse_store)");
        exit(1);
    }
    start_periodic_timer(thd, thd->period);
    while(1){
        wait_next_activation(thd);
        mse_calc_thread_body(mse_store);
    }
    
}

void parse_cmdline(int argc, char ** argv){
	int opt;
	
	while ((opt = getopt(argc, argv, "snfmbz")) != -1) {
		switch (opt) {
		case 's':
			flag_signal = 1;
			break;
		case 'n':
			flag_noise = 1;
			break;
		case 'f':
			flag_filtered = 1;
			break;
        case 'm':
            flag_type = 1; //mean filter
            break;
        case 'b':
            flag_type = 2; //butterworth filter
            break;
        case 'z':
            flag_type = 3; //sivatzky-golay filter
            break;
		default: 
			fprintf(stderr, USAGE_STR, argv[0]);
			exit(EXIT_FAILURE);
		}
	}
	
	if ((flag_signal | flag_noise | flag_filtered | flag_type) == 0)
	{
		flag_signal = flag_noise = flag_filtered = flag_type = 1;
	}

}


int main(int argc, char ** argv){


    periodic_thread *generator =  (periodic_thread *)malloc(sizeof(periodic_thread));
    periodic_thread *filter = (periodic_thread *) malloc(sizeof(periodic_thread));
    periodic_thread *mse_calculator = (periodic_thread *) malloc (sizeof(periodic_thread));

    pthread_t gen;
    pthread_t filt;
    pthread_t mse_calc;

    pthread_attr_t gen_attr;
    pthread_attr_t filt_attr;
    pthread_attr_t mse_attr;

    struct sched_param _param;
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_t mse_mutex_attr_gen;
    pthread_mutexattr_t mse_mutex_attr_filt;

    mqd_t q_store_local; //VEDERE SE DA METTERE GLOBALE
    mqd_t mse_store_local;
    mqd_t wg_queue_local;

    //code_c code_locali = {q_store_local, wg_queue_local};

    //mlockall();

	// Command line input parsing
	parse_cmdline(argc, argv);
	
    pthread_attr_init(&gen_attr);
    pthread_attr_init(&filt_attr);
    pthread_attr_init(&mse_attr);

    pthread_attr_setschedpolicy(&gen_attr, SCHED_FIFO);
    pthread_attr_setschedpolicy(&filt_attr, SCHED_FIFO);
    pthread_attr_setschedpolicy(&mse_attr, SCHED_FIFO);
    
    _param.sched_priority = 70;  //da rivedere
    pthread_attr_setschedparam(&gen_attr, &_param);
    //fai partire thread
    _param.sched_priority = 69;  //da rivedere
    pthread_attr_setschedparam(&filt_attr, &_param);
    //fai partire thread
    _param.sched_priority = 60;  //da rivedere
    pthread_attr_setschedparam(&mse_attr, &_param);

    pthread_attr_setinheritsched(&gen_attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setinheritsched(&filt_attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setinheritsched(&mse_attr, PTHREAD_EXPLICIT_SCHED);
    // pthread_attr_setdetachstate(&gen_attr, PTHREAD_CREATE_JOINABLE);
    pthread_attr_setdetachstate(&gen_attr, PTHREAD_CREATE_JOINABLE);
    pthread_attr_setdetachstate(&filt_attr, PTHREAD_CREATE_JOINABLE);
    pthread_attr_setdetachstate(&mse_attr, PTHREAD_CREATE_JOINABLE);

    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setprotocol(&mutex_attr,PTHREAD_PRIO_PROTECT);
    pthread_mutexattr_setprioceiling(&mutex,70);
    pthread_mutex_init(&mutex, &mutex_attr);
    pthread_mutexattr_init(&mse_mutex_attr_gen);
    pthread_mutexattr_setprotocol(&mse_mutex_attr_gen, PTHREAD_PRIO_PROTECT);
    pthread_mutexattr_setprioceiling(&mse_mutex_attr_gen, 70);
    pthread_mutex_init(&mse_mutex_gen, &mse_mutex_attr_gen);

    pthread_mutexattr_init(&mse_mutex_attr_filt);
    pthread_mutexattr_setprotocol(&mse_mutex_attr_filt, PTHREAD_PRIO_PROTECT);
    pthread_mutexattr_setprioceiling(&mse_mutex_attr_filt, 69);
    pthread_mutex_init(&mse_mutex_filt, &mse_mutex_attr_filt);

    //inizializza i thread periodici       
    generator->index = 0;
    generator->period = PERIOD_US; //in us
    generator->wcet = 1000; //da rivedere    
    generator->priority = 70; //da rivedere

    filter->index = 1;
    filter->period = PERIOD_US;
    filter->wcet = 1000; //da rivedere    
    filter->priority = 69; //da rivedere 

    mse_calculator->index = 2;
    mse_calculator->period = PERIOD_US_MSE;
    mse_calculator->wcet = 1000; 
    mse_calculator->priority = 60; 

    /*
    if ((q_store = mq_open (Q_STORE, O_WRONLY)) == -1) {
        perror ("Filter: mq_open (store)");
        exit (1);
    }
    */


    /*
     cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(3, &cpuset);

    */
   
    pthread_create(&gen, &gen_attr, generator_thread, (void *)generator);
    pthread_create(&filt, &filt_attr, filter_thread, (void *)filter);
    pthread_create(&mse_calc, &mse_attr, mse_calc_thread, (void *)mse_calculator);

    /*
    pthread_setaffinity_np(gen, sizeof(cpu_set_t), &cpuset);
    pthread_setaffinity_np(filt, sizeof(cpu_set_t), &cpuset);
    */

    
    pthread_join(gen, NULL);
    pthread_join(filt, (void**)&q_store_local);
    pthread_join(mse_calc, (void **)&mse_store_local);
    //VEDERE PER CODA
    //pthread_join(mse_calc, NULL);
    //code_c * code_locali_final = &code_locali;
    //q_store_local = code_locali.store;
    //wg_queue_local = code_locali.wg;
    mqd_t *q_store_final = (mqd_t *) q_store_local;
    mqd_t *mse_store_final = (mqd_t *) wg_queue_local;
    pthread_attr_destroy(&gen_attr);
    pthread_attr_destroy(&filt_attr);
    pthread_attr_destroy(&mse_attr);
    pthread_mutexattr_destroy(&mutex_attr);
    pthread_mutexattr_destroy(&mse_mutex_attr_gen);
    pthread_mutexattr_destroy(&mse_mutex_attr_filt);
    pthread_mutex_destroy(&mutex);
    pthread_mutex_destroy(&mse_mutex_gen);
    pthread_mutex_destroy(&mse_mutex_filt);
    //CHIUDERE CODA
    mq_close (q_store_final);
    mq_close (mse_store_final);
    mq_close(wg_queue_local);
    mq_unlink (Q_STORE);
    mq_unlink (QMSE_STORE);
    mq_unlink(WG_QUEUE);
    free(generator);
    free(filter);
    free(mse_calculator);
}