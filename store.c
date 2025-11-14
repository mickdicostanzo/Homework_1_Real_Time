/*
    Real-Time Programming Homework 1
    Signal Generation, Filtering and Storage using POSIX real-time extensions
    filter.c: Signal filtering module
    PARTECIPANTI:
    - Annunziata Giovanni              DE6000015
    - Di Costanzo Michele Pio          DE6000001
    - Di Palma Lorenzo                 N39001908 
    - Zaccone Amedeo                   DE6000014  
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
#include <string.h>
#include <mqueue.h>
#include <stdint.h>
#include <string.h>

#define SIG_SAMPLE SIGRTMIN
#define SIG_HZ 1
#define OUTFILE "signal.txt"
#define F_SAMPLE 5 
#define SIZEQ 10
#define MSG_SIZE 256
#define Q_STORE "/print_q"
#define QMSE_STORE "/mse_q"
#define QUEUE_PERMISSIONS 0660
#define NSEC_PER_SEC 1000000000ULL


#define USAGE_STR				\
	"Usage: %s [-s] [-n] [-f]\n"		\
	"\t -s: plot original signal\n"		\
	"\t -n: plot noisy signal\n"		\
	"\t -f: plot filtered signal\n"		\
	""
	
struct timespec r;
double period = 1/F_SAMPLE * NSEC_PER_SEC;


void timespec_add_us(struct timespec *t, unsigned long d)
{
    d *= 1000;
    t->tv_nsec += d;
    t->tv_sec += t->tv_nsec / NSEC_PER_SEC;
    t->tv_nsec %= NSEC_PER_SEC;
}

void wait_next_activation(void)
{
    clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &r, NULL);
    timespec_add_us(&r, period);
}

void start_periodic_timer(uint64_t offs, int t)
{
    clock_gettime(CLOCK_REALTIME, &r);
    timespec_add_us(&r, offs);
    period = t;
}


void store_body(mqd_t q_store,mqd_t mse_store, FILE * outfd){
    char msg[MSG_SIZE];
    const char delim[] = " ";
    char * token;
    for(int i=0; i<SIZEQ; i++){

    if (mq_receive (q_store, msg, MSG_SIZE, NULL) == -1) {
            perror ("Store: mq_receive");
            exit (1);
       }
    msg[strcspn(msg, "\n")] = 0;
    int count = 0;
    token = strtok(msg, delim);
    while (token != NULL) {
        //fprintf(outfd, "%s", token);  // scrive il token
        count++;

        if (strcasecmp(token, "nan") != 0 ) {
                // Se NON è "nan" o "inf", scrive il token
                fprintf(outfd, "%s", token);
            }
       
        if (count % 4 == 0) {
            fprintf(outfd, "\n");     // ogni 3 token, vai a capo
        } else {
            fprintf(outfd, ",");      // altrimenti, separa con tab
        }
        token = strtok(NULL, delim);  // prossimo token
    }
}
    //printf("Scrittura completata!\n");
    fflush(outfd);

    char mse_msg[MSG_SIZE];
    if(mq_receive(mse_store,mse_msg,MSG_SIZE,NULL) == -1){
        perror("Store: mq_receive (mse)");
        exit(1);
    }
    printf("current_mse: %s",mse_msg);
}

int main(int argc, char ** argv){
    int outfile;
    FILE * outfd;

    mqd_t q_store;
    mqd_t mse_store;
    struct mq_attr attr;
    struct mq_attr mse_attr;

    attr.mq_flags = 0;
    attr.mq_maxmsg = SIZEQ;
    attr.mq_msgsize = MSG_SIZE;
    attr.mq_curmsgs = 0;

    mse_attr.mq_flags =0;
    mse_attr.mq_maxmsg = SIZEQ;
    mse_attr.mq_msgsize = MSG_SIZE;
    mse_attr.mq_curmsgs = 0;

    if ((q_store = mq_open (Q_STORE, O_RDONLY | O_CREAT,QUEUE_PERMISSIONS,&attr)) == -1) {
        perror ("Store: mq_open (store)");
        exit (1);
    }

    if ((mse_store = mq_open (QMSE_STORE, O_RDONLY | O_CREAT,QUEUE_PERMISSIONS,&mse_attr)) == -1) {
        perror ("Store: mq_open (mse_store)");
        exit (1);
    }

    outfile = open(OUTFILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    outfd = fdopen(outfile, "w");
    if (outfile < 0 || !outfd) {
        perror("Unable to open/create output file. Exiting.");
        return EXIT_FAILURE;
    }
    start_periodic_timer(0, period);
    while(1){
        wait_next_activation();
        store_body(q_store,mse_store, outfd);
    }
    mq_close (q_store);
    mq_close (mse_store);
    mq_unlink (Q_STORE);
    mq_unlink (QMSE_STORE);
    fclose(outfd);
}