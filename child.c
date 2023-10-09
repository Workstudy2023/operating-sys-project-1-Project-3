#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <string.h>
#include <sys/msg.h>

/*
The worker will be attached to shared memory and examine the simulated system clock. It will then figure out what time it
should terminate by adding up the system clock time and the time passed to it. This is when the process should decide to leave
the system and terminate.

For example, if the system clock was showing 6 seconds and 100 nanoseconds and the worker was passed 5 and 500000 as
above, the target time to terminate in the system would be 11 seconds and 500100 nanoseconds. The worker will then go into
a loop, constantly checking the system clock to see if this time has passed. If it ever looks at the system clock and sees values
over the ones when it should terminate, it should output some information and then terminate.
*/

#define SHMKEY 2031974 // Parent and child agree on common key. Parent must create the shared memory segment.
// Size of shared memory buffer: two integers; one for seconds and the other for nanoeconds.
#define BUFF_SZ 2 * sizeof(int)
#define PERMS 0644 // Read-only shared memory.

typedef struct msgbuffer {
	long mtype;
	char strData[100];
	int intData;
} msgbuffer;

////// Function prototypes ///////
static void myhandler(int s);
static int setupinterrupt(void);
static int setupitimer(void);

////// Global variables ///////
int *simulatedClock;

int main(int argc, char **argv)
{
	msgbuffer buf;
	buf.mtype = 1;
	int msqid = 0;
	key_t key;
	// Log the start of the child process
	//printf("Child: Starting\n");

	// Process arguments
	if (argc != 3)
	{
		fprintf(stderr, "Child: Error: Incorrect number of arguments\n");
		// Log the number of arguments
		//printf("Child: Number of arguments: %d\n", argc);
		//printf("Child: argv[0]: %s argv[1]: %s\n", argv[0], argv[1]);
		return 1;
	}

	// Log the arguments
	//printf("Child: Seconds: %s Nanoseconds: %s\n", argv[1], argv[2]);

	int seconds = atoi(argv[1]);
	int nanoseconds = atoi(argv[2]);

	// Log the start time
	//printf("Child: Starting at %d seconds and %d nanoseconds\n", seconds, nanoseconds);

	if (setupinterrupt() == -1)
	{
		perror("Failed to set up handler for SIGPROF");
		return 1;
	}
	if (setupitimer() == -1)
	{
		perror("Failed to set up the ITIMER_PROF interval timer");
		return 1;
	}
	// Log the correct execution of the timer
	// printf("Child: Timer set up correctly\n");

	// Attach to shared memory
	// Examine simulated system clock
	int shmid = shmget(SHMKEY, BUFF_SZ, 0777); // Parent must create the shared memory segment. Read-only shared memory.
	if (shmid == -1)
	{
		perror("shmget");
		fprintf(stderr, "Child: ... Error in shmget ...\n");
		exit(1);
	}
	simulatedClock = (int *)(shmat(shmid, 0, 0));
	if (simulatedClock == (int *)(-1))
	{
		fprintf(stderr, "Child: ... Error in shmat ...\n");
		exit(1);
	}

	// TODO: implement the signal handler

	// Figure out what time it should terminate by adding up the system clock time and the time
	// passed to it. This is when the process should decide to leave the system and terminate.
	// Get current time first.
	// Then add the seconds and nanoseconds to it.
	// Then check if the current time is greater than the target time.
	// If it is, then terminate.
	// If it is not, then keep looping.

	// Get simulated clock time
	int previous_simulated_clock_seconds;
	int simulated_clock_seconds = simulatedClock[0];
	int simulated_clock_nanoseconds = simulatedClock[1];
	previous_simulated_clock_seconds = simulated_clock_seconds;

	// Get target time
	int target_time_seconds = simulated_clock_seconds + seconds;
	int target_time_nanoseconds = simulated_clock_nanoseconds + nanoseconds;

	// QUEUE related code
	// get a key for our message queue
	// TODO: check where each block of code should go
	// TODO: commit to github
	if ((key = ftok("msgq.txt", 1)) == -1) {
		perror("ftok");
		exit(1);
	}

	// create our message queue
	if ((msqid = msgget(key, PERMS)) == -1) {
		perror("msgget in child");
		exit(1);
	}

	printf("Child %d has access to the queue\n", getpid());

	// receive a message, but only one for us
	if ( msgrcv(msqid, &buf, sizeof(msgbuffer), getpid(), 0) == -1) {
		perror("failed to receive message from parent\n");
		exit(1);
	}

	// output message from parent
	printf("Child %d received message: %s was my messageand my int data was %d\n", getpid(), buf.strData, buf.intData);
	
	// now send a message back to our parent
	buf.mtype = getppid();
	buf.intData = getppid();
	strcpy(buf.strData,"Message back to muh parent\n");

	if (msgsnd(msqid,&buf,sizeof(msgbuffer)-sizeof(long),0) == -1) {
		perror("msgsnd to parent failed\n");
		exit(1);
	}	
	// QUEUE related code end


	// TODO: implement the output
	// Upon starting up, it should output the following information:
	// WORKER PID:6577 PPID:6576 SysClockS: 5 SysclockNano: 1000 TermTimeS: 11 TermTimeNano: 500100 --Just Starting
	printf("WORKER PID:%d PPID:%d SysClockS: %d SysclockNano: %d TermTimeS: %d TermTimeNano: %d --Just Starting\n",
		   getpid(), getppid(), simulated_clock_seconds, simulated_clock_nanoseconds, target_time_seconds, target_time_nanoseconds);

	// Check if target time is greater than simulated clock time
	while (1)
	{
		// Get simulated clock time
		simulated_clock_seconds = simulatedClock[0];
		simulated_clock_nanoseconds = simulatedClock[1];

		// Check if target time is greater than simulated clock time
		if (target_time_seconds < simulated_clock_seconds)
		{
			printf("Child: Target time is less than simulated clock time. Terminating.\n");
			break;
		}
		else if (target_time_seconds == simulated_clock_seconds)
		{
			if (target_time_nanoseconds < simulated_clock_nanoseconds)
			{
				printf("Child: Target time is less than simulated clock time. Terminating.\n");
				break;
			}
		}
		// Check if simulated clock seconds has changed and output message
		if (simulated_clock_seconds != previous_simulated_clock_seconds)
		{
			printf("WORKER PID:%d PPID:%d SysClockS: %d SysclockNano: %d TermTimeS: %d TermTimeNano: %d\n",
				   getpid(), getppid(), simulated_clock_seconds, simulated_clock_nanoseconds, target_time_seconds, target_time_nanoseconds);
			printf("--%d seconds have passed since starting\n", simulated_clock_seconds);
			previous_simulated_clock_seconds = simulated_clock_seconds;
		}
	}

	// Print the final message and terminate
	printf("WORKER PID:%d PPID:%d SysClockS: %d SysclockNano: %d TermTimeS: %d TermTimeNano: %d\n",
		   getpid(), getppid(), simulated_clock_seconds, simulated_clock_nanoseconds, target_time_seconds, target_time_nanoseconds);
	printf("--Terminating\n");

	shmdt(simulatedClock); // Detach from shared memory
	return EXIT_SUCCESS;
}

static void myhandler(int s)
{
	// Log the signal
	printf("Child: Signal received: %d\n", s);
	printf("It has to now terminate this WORKER\n");
	shmdt(simulatedClock); // Detach from shared memory
	exit(EXIT_SUCCESS);
	/*
	char aster = '*';
	int errsave;
	errsave = errno;
	write(STDERR_FILENO, &aster, 1);
	errno = errsave;
	*/
}

static int setupinterrupt(void)
{ /* set up myhandler for SIGPROF */
	struct sigaction act;
	act.sa_handler = myhandler;
	act.sa_flags = 0;
	return (sigemptyset(&act.sa_mask) || sigaction(SIGPROF, &act, NULL));
}

static int setupitimer(void)
{ /* set ITIMER_PROF for 2-second intervals */
	struct itimerval value;
	value.it_interval.tv_sec = 60;
	value.it_interval.tv_usec = 0;
	value.it_value = value.it_interval;
	return (setitimer(ITIMER_PROF, &value, NULL));
}
