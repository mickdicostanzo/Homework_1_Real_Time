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

#define SIG_HZ 1
#define WG_QUEUE "/watchdog_queue"
#define QUEUE_PERMISSIONS 0660
#define NSEC_PER_SEC 1000000000ULL
#define F_SAMPLE 1
#define MSG_SIZE 256
#define U_SEC 1000000L 
#define USAGE_STR				\
	"Usage: %s [-s] [-n] [-f]\n"		\
	"\t -s: plot original signal\n"		\
	"\t -n: plot noisy signal\n"		\
	"\t -f: plot filtered signal\n"		\
	""
	
struct timespec r;
struct timespec t;
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

void watch_dog(mqd_t coda){
    char msg[MSG_SIZE];
    clock_gettime(CLOCK_REALTIME,&t);
    timespec_add_us(&t, 3*U_SEC);
    if(mq_timedreceive(coda, msg, MSG_SIZE, NULL,&t)== -1){
        printf("Timeout: Filter non attivo");
    }
    else{
        printf("Filter: %s",msg);
    }

}

int main(int argc, char **argv){
    mqd_t wg_queue;
    struct mq_attr wg_attr;

    wg_attr.mq_flags = 0;
    wg_attr.mq_maxmsg = 1;
    wg_attr.mq_msgsize = MSG_SIZE;
    wg_attr.mq_curmsgs = 0;

    if((wg_queue = mq_open(WG_QUEUE,O_RDONLY | O_CREAT, QUEUE_PERMISSIONS,&wg_attr)== -1)){
        perror('Watch Dog: mq_open (watch dog)');
        exit(1);
    }
    start_periodic_timer(0,period);
    while(1){
        wait_next_activation();
        watch_dog(wg_queue);
    }
    mq_close (wg_queue);
    mq_unlink (WG_QUEUE);
}