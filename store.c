/*
    Real-Time Programming Homework 1
    Signal Generation, Filtering and Storage using POSIX real-time extensions
    filter.c: Signal filtering module
    PARTECIPANTI:
    - Annunziata Giovanni
    - Di Costanzo Michele Pio
    - Di Palma Lorenzo
    - Zaccone Amedeo
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
//#include "rt-lib.h"
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
#define QUEUE_PERMISSIONS 0660
#define NSEC_PER_SEC 1000000000ULL

#define USAGE_STR				\
	"Usage: %s [-s] [-n] [-f]\n"		\
	"\t -s: plot original signal\n"		\
	"\t -n: plot noisy signal\n"		\
	"\t -f: plot filtered signal\n"		\
	""
	
struct timespec r;
int period = 1/F_SAMPLE * NSEC_PER_SEC;

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

void store_body(mqd_t q_store, FILE * outfd){
    char msg[MSG_SIZE];
    int count = 0;
    const char delim[] = " ";
    char * token;
    for(int i=0; i<SIZEQ; i++){

    if (mq_receive (q_store, msg, MSG_SIZE, NULL) == -1) {
            perror ("Store: mq_receive");
            exit (1);
       }
 }
    token = strtok(msg, delim);
    while (token != NULL) {
        fprintf(outfd, "%s", token);  // scrive il token
        count++;

        if (count % 3 == 0) {
            fprintf(outfd, "\n");     // ogni 3 token, vai a capo
        } else {
            fprintf(outfd, "\t");      // altrimenti, separa con tab
        }
        token = strtok(NULL, delim);  // prossimo token
    }

    //printf("Scrittura completata!\n");
    fflush(outfd);
}

int main(int argc, char ** argv){
    int outfile;
    FILE * outfd;

    mqd_t q_store;
    struct mq_attr attr;

    attr.mq_flags |= O_NONBLOCK;
    attr.mq_maxmsg = SIZEQ;
    attr.mq_msgsize = MSG_SIZE;
    attr.mq_curmsgs = 0;

    if ((q_store = mq_open (Q_STORE, O_RDONLY | O_CREAT,QUEUE_PERMISSIONS,&attr)) == -1) {
        perror ("Store: mq_open (store)");
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
        store_body(q_store, outfd);
    }
    mq_close (q_store);
    mq_unlink (Q_STORE);
    fclose(outfd);

}