/*
    Real-Time Programming Homework 1
    Signal Generation, Filtering and Storage using POSIX real-time extensions
    filter.c: Signal filtering module
    PARTECIPANTI:
    - Annunziata Giovanni
    - Di Costanzo Michele Pio
    - Di Palma Lorenzo
    - Zaccone Amedeo
    I più forti
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
#define QUEUE_PERMISSIONS 0660


/*
    POSSIBILI CORREZIONI DA FARE;
    - sistemare la parte di apertura della coda di messaggi (aprire una volta sola)
    - cambiare nei start periodic timer &thd con thd perché start periodic timer prende un puntatore
    - usare struttura per periodic thread e coda per filter thread
*/


#define USAGE_STR				\
	"Usage: %s [-s] [-n] [-f]\n"		\
	"\t -s: plot original signal\n"		\
	"\t -n: plot noisy signal\n"		\
	"\t -f: plot filtered signal\n"		\
	""
	
#define BUTTERFILT_ORD 2
double b [3] = {0.0134,    0.0267,    0.0134};
double a [3] = {1.0000,   -1.6475,    0.7009};

static int first_mean=0;
int flag_signal = 0;
int flag_noise = 0;
int flag_filtered = 0;
int flag_type = 0; // 1:mean, 2:butterworth

double sig_noise;
double sig_val;

pthread_mutex_t mutex;

double glob_time = 0.0;

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
		first_mean ++;
	}
	else{
		retval = (vec_mean[0] + vec_mean[1])/2;	
	}
	return retval;
}


void generator_thread_body(){
	// Generate signal
    // glob_time = 0;
	double sig_val_l= sin(2*M_PI*SIG_HZ*glob_time);

	// Add noise to signal
	double sig_noise_l= sig_val + 0.5*cos(2*M_PI*10*glob_time);
	sig_noise_l += 0.9*cos(2*M_PI*4*glob_time);
	sig_noise_l += 0.9*cos(2*M_PI*12*glob_time);
	sig_noise_l+= 0.8*cos(2*M_PI*15*glob_time);
	sig_noise_l += 0.7*cos(2*M_PI*18*glob_time);

    pthread_mutex_lock(&mutex);
    sig_val = sig_val_l;
    sig_noise = sig_noise_l;
    pthread_mutex_unlock(&mutex);
    glob_time += (1.0/F_SAMPLE); /* Sampling period in s */
   
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
    double sig_filt;
    pthread_mutex_lock(&mutex);
    double sig_noise_l = sig_noise;
    pthread_mutex_unlock(&mutex);
    // Apply Filter to signal
    if(flag_type== 2){
        //double sig_filt = get_butter(sig_noise, a, b);
        sig_filt = get_butter(sig_noise_l, a, b);
    }
    else{
        sig_filt = get_mean_filter(sig_noise_l);
    }
    // Debug
    printf("tempo: %lf, sig_val: %lf, sig_noise: %lf, sig_filter: %lf\n", glob_time, sig_val,sig_noise, sig_filt);
    char msg[MSG_SIZE];
    char msg_signale[20];

    /*
    snprintf(msg_signale,sizeof(msg_signale),"%lf ", sig_val);  
    strcat(msg, msg_signale);
    strcat(msg, " ");
    snprintf(msg_signale,sizeof(msg_signale),"%lf ", sig_noise_l);
    strcat(msg, " ");
    snprintf(msg_signale,sizeof(msg_signale),"%lf\n", sig_filt);
    strcat(msg, msg_signale);
    */

    int n = snprintf(msg, sizeof(msg), "%lf %lf %lf %lf\n",glob_time, sig_val, sig_noise, sig_filt);
    if (n < 0 || n >= (int)sizeof(msg)) {
        fprintf(stderr, "Filter: message truncated or snprintf error\n");
        return;
    }
    if (mq_send (coda, msg, strlen (msg) + 1, 0) == -1) {
        perror ("Filter: mq_send");
        exit (1);
    }
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

     if ((q_store = mq_open (Q_STORE, O_WRONLY)) == -1) {
        perror ("Filter: mq_open (store)");
        exit (1);
    }
    start_periodic_timer(thd, thd->period);
    while(1){
        wait_next_activation(thd);
        filter_thread_body(q_store);
        // if char = boh -> print, break
        if(getchar() == 'q'){
            printf("uscita dal thread avvenuta con successo\n");
            pthread_exit((mqd_t *)q_store);
        }
    }
}

void parse_cmdline(int argc, char ** argv){
	int opt;
	
	while ((opt = getopt(argc, argv, "snfmb")) != -1) {
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
		default: /* '?' */
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

    pthread_t gen;
    pthread_t filt;

    pthread_attr_t gen_attr;
    pthread_attr_t filt_attr;

    struct sched_param _param;
    pthread_mutexattr_t mutex_attr;

    // int f_sample = F_SAMPLE; /* Frequency of sampling in Hz */
	// double t_sample = (1.0/f_sample) * 1000 * 1000 * 1000; /* Sampling period in ns */

    mqd_t q_store_local; //VEDERE SE DA METTERE GLOBALE

	// Command line input parsing
	parse_cmdline(argc, argv);
	
    pthread_attr_init(&gen_attr);
    pthread_attr_init(&filt_attr);

    pthread_attr_setschedpolicy(&gen_attr, SCHED_FIFO);
    pthread_attr_setschedpolicy(&filt_attr, SCHED_FIFO);
    
    _param.sched_priority = 70;  //da rivedere
    pthread_attr_setschedparam(&gen_attr, &_param);
    //fai partire thread
    _param.sched_priority = 70;  //da rivedere
    pthread_attr_setschedparam(&filt_attr, &_param);
    //fai partire thread

    pthread_attr_setinheritsched(&gen_attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setinheritsched(&filt_attr, PTHREAD_EXPLICIT_SCHED);
    
    pthread_attr_setdetachstate(&gen_attr, PTHREAD_CREATE_JOINABLE);
    pthread_attr_setdetachstate(&filt_attr, PTHREAD_CREATE_JOINABLE);

    pthread_mutexattr_init(&mutex_attr);
    pthread_mutex_init(&mutex, &mutex_attr);

    //inizializza i thread periodici       
    generator->index = 0;
    generator->period = PERIOD_US; //in us
    generator->wcet = 1000; //da rivedere    
    generator->priority = 70; //da rivedere

    filter->index = 1;
    filter->period = PERIOD_US;
    filter->wcet = 1000; //da rivedere    
    filter->priority = 70; //da rivedere 

    /*
    if ((q_store = mq_open (Q_STORE, O_WRONLY)) == -1) {
        perror ("Filter: mq_open (store)");
        exit (1);
    }
    */

    pthread_create(&gen, &gen_attr, generator_thread, (void *)generator);
    pthread_create(&filt, &filt_attr, filter_thread, (void *)filter);


    //JOINARE THREAD E CHIUDERE CODE
    
    pthread_join(gen, NULL);
    pthread_join(filt, (void**)&q_store_local);
    mqd_t *q_store_final = (mqd_t *) q_store_local;
    pthread_attr_destroy(&gen_attr);
    pthread_attr_destroy(&filt_attr);
    pthread_mutexattr_destroy(&mutex_attr);
    pthread_mutex_destroy(&mutex);
    mq_close (q_store_final);
    mq_unlink (Q_STORE);
    free(generator);
    free(filter);
}