#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <string.h>
#include <sys/msg.h>

// Constants
// Define a constant to limit the number of concurrent processes
#define MAX_CONCURRENT 20
#define BILLION 1000000000
#define TIME_STRING_SIZE 30
#define PERMS 0644 // Read-only shared memory.

// Define a struct to represent a process control block (PCB)
struct PCB
{
    int occupied;     // either true or false
    pid_t pid;        // process id of this child
    int startSeconds; // time when it was forked
    int startNano;    // time when it was forked
};
struct PCB processTable[MAX_CONCURRENT];

typedef struct msgbuffer {
	long mtype;
	char strData[100];
	int intData;
} msgbuffer;

/*
Each iteration in oss you need to increment the clock. So how much should you increment it? You should attempt to very
loosely have your internal clock be similar to the real clock. This does not have to be precise and does not need to be checked,
just use it as a crude guideline. So if you notice that your internal clock is much slower than real time, increase your increment.
If it is moving much faster, decrease your increment. Keep in mind that this will change based on server load possibly, so do
not worry about if it is off sometimes and on other times.
*/
// Our internal clock, similar to the real clock.
/*
struct Clock *clock;
struct Clock
{
    int seconds;
    int nanoSeconds; // 1 billion nanoSeconds (1,000,000,000 nano seconds) = 1 second
};
*/

#define SHMKEY 2031974 // Parent and child agree on common key. Parent must create the shared memory segment.
// Size of shared memory buffer: two integers; one for seconds and the other for nanoeconds.
#define BUFF_SZ 2 * sizeof(int)

// Function prototypes
void print_help();
void incrementClock();
void initializeClock();
int checkIfChildHasTerminated();
int checkIfAllChildrenHaveTerminated();
pid_t CreateChildProcess(int entryIndex, int time_limit);
int FindEntryInProcessTableNotOccupied();
void cleanup();
void SIGINT_handler(int sig);
void printProcessTable();
void CreatePCBentry(int entryIndex, pid_t pid, int startSeconds, int startNano);
void updatePCB(int pidChildHasTerminated);

// Global variables
int shmid;           // Create shared memory segment
int *simulatedClock; // pointer to the simulated clock

// For message queue
int msqid;           // message queue id
msgbuffer msgBuffer; // message buffer to send
msgbuffer receiveBuffer; // message buffer to receive

int main(int argc, char *argv[])
{
    int opt;
    int total_children = 0;
    int max_simultaneous = 0;
    int time_limit = 0;
    pid_t childPid = 0;
    char log_file[FILENAME_MAX] = "";
    char messageToSent[100];
    char messageReceived[100];

    // Queue related code section
	key_t key;
	system("touch msgq.txt");   

	// get a key for our message queue
	if ((key = ftok("msgq.txt", 1)) == -1) {
		perror("ftok");
		exit(1);
	}

	// create our message queue
	if ((msqid = msgget(key, PERMS | IPC_CREAT)) == -1) {
		perror("msgget in parent");
		exit(1);
	}

	printf("Message queue set up\n");     
    // End of queue related code section

    // Signal handler for SIGINT
    /*
    your program to terminate after no more than 60 REAL LIFE seconds. This can be done using a timeout
    signal, at which point it should kill all currently running child processes and terminate. It should also catch the ctrl-c signal,
    free up shared memory and then terminate all children.
    */

    // Set up the signal handler to catch SIGINT 'CTRL+C'
    /*
    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = SIGINT_handler;
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, NULL);
    */
    signal(SIGINT, SIGINT_handler);

    // The permisssion mode should be read only for the child and read and write for the parent.
    // IPC_CREAT creates the shared memory segment if it does not already exist.
    // IPC_EXCL ensures that the shared memory segment is created for the first time.
    // 0777 is the permission mode. It is the same as 777 in octal and 511 in decimal. It means that the owner, group, and others have read, write, and execute permissions.
    if ((shmid = shmget(SHMKEY, BUFF_SZ, 0777 | IPC_CREAT)) == -1) // TODO: check if this is correct
    {
        perror("shmget");
        exit(1);
    }
    if (shmid == -1)
    {
        fprintf(stderr, "Parent: ... Error in shmget ...\n");
        exit(1);
    }
    // Get the pointer to shared block of memory.
    simulatedClock = (int *)(shmat(shmid, 0, 0));

    while ((opt = getopt(argc, argv, "hn:s:t:")) != -1)
    {
        switch (opt)
        {
        case 'h':
            print_help();
            return 0;
        case 'n':
            total_children = atoi(optarg);
            break;
        case 's':
            max_simultaneous = atoi(optarg);
            break;
        case 't':
            time_limit = atoi(optarg);
            break;
        case 'f':
            strcpy(log_file, optarg);
            break;
        default:
            fprintf(stderr, "Invalid option: -%c\n", opt);
            print_help();
            return 1;
        }
    }

    // Check if the parameters are valid
    if (total_children <= 0 || max_simultaneous <= 0 || max_simultaneous > MAX_CONCURRENT || time_limit <= 0 || strcmp(log_file, "") == 0)
    {
        fprintf(stderr, "Invalid parameters. Please provide valid values for -n, -s, -t and -f.\n");
        print_help();
        return 1;
    }

    // Keep track of the number of children currently running.
    int running_children = 0;
    int children_already_executed = 0;
    int entryIndex = 0;
    initializeClock();

    // Generate the children
    // Check if the number of children currently running is less than the maximum number of children allowed to run simultaneously.
    while (children_already_executed < total_children || running_children > 0)
    {
        // Log the time before incrementing the simulated clock
        //printf("the time before incrementing the simulated clock:\n");
        //fflush(stdout);
        incrementClock();
        // Every half a second of simulated clock time, output the process table to the screen
        if (simulatedClock[1] >= 500000000) // if nanoSeconds is greater than or equal a half second
            printProcessTable();
        //printf("simulatedClock[0]: %d\n", simulatedClock[0]);
        //fflush(stdout);

        // Find entry in process table that is not occupied
        int entryIndex = FindEntryInProcessTableNotOccupied();
        if (entryIndex == -1)
        {
            printf("No entry in process table is not occupied.\n");
            fflush(stdout);
            continue;
        }
        else 
        {
            //printf("entryIndex: %d\n", entryIndex);
            //fflush(stdout);
        }

        // Launch new child obeying process limits
        // the simul parameter indicates how many children to allow to run simultaneously
        if (running_children > max_simultaneous)
        {
            printf("Maximum number of children allowed to run simultaneously has been reached.\n");
            fflush(stdout);
            // continue;
        }
        {
            //printf("Maximum number of children allowed to run simultaneously has not been reached.\n");
            //fflush(stdout);
            if (children_already_executed < total_children)
            {
                //printf("There are still children to be executed.\n");
                //fflush(stdout);
                childPid = CreateChildProcess(entryIndex, time_limit); // This is where the child process splits from the parent
                if (childPid != 0)                                     // parent process
                {
                    //printf("I'm a parent! My pid is %d, and my child's pid is %d \n", getpid(), childPid);
                    //fflush(stdout);
                    processTable[children_already_executed].pid = childPid;
                    processTable[children_already_executed].occupied = 1;
                    running_children++;
                    children_already_executed++;
                }
            }
        }

        // Logs calling chilldHasTerminated
        //printf("calling chilldHasTerminated\n");
        // Check if any children have terminated
        int childHasTerminated = checkIfChildHasTerminated();
        if (childHasTerminated)
        {
            printf("A child has terminated.\n");
            fflush(stdout);
            // update PCB Of the Terminated Child
            updatePCB(childHasTerminated);
            running_children--;
            if (children_already_executed < total_children)
            {
                printf("There are still children to be executed.\n");
                // Launch new child obeying process limits
                childPid = CreateChildProcess(entryIndex, time_limit); // This is where the child process splits from the parent
                if (childPid != 0)                                     // parent process
                {
                    //printf("I'm a parent! My pid is %d, and my child's pid is %d \n", getpid(), childPid);
                    //fflush(stdout);
                    running_children++;
                    children_already_executed++;
                }
            }
        }
        else
        {
            //printf("No child has terminated.\n");
            //fflush(stdout);
        }
        // TODO: get pid of next child in pcb
        childPid = processTable[entryIndex].pid; // TODO: check if the use of childPid doesn't create a problem

        // Send message to that pid so that child can check the clock       
        sprintf(messageToSent, "Message to child %d\n", childPid); // TODO: ensure the message sent follows the specifications
        send_message(childPid, messageToSent);

        // Wait for a message back from that child
        receive_message(childPid);

        // TODO: if it indicates it is terminating, output that it intends to 
        // terminate: CHECK IF THIS HAS BEEN ALREADY IMPLEMENTED
    }
    printf("Parent is now ending.\n");
    // Detach and remove shared memory segment
    // TODO: Detach the shared memory segment ONLY if no children are active.
    while (!checkIfAllChildrenHaveTerminated())
        ;
    cleanup();

    return EXIT_SUCCESS;
}

// Function definitions
void print_help()
{
    printf("Usage: oss [-h] [-n proc] [-s simul] [-t timelimit] [-f logfile]\n");
    printf("  -h: Display this help message\n");
    printf("  -n proc: Number of total children to launch\n");
    printf("  -s simul: Number of children allowed to run simultaneously\n");
    printf("  -t timelimit: The bound of time that a child process will be launched for\n");
    printf("  -f logfile: This is for a log file\n");
}

/*
This function should be called the time right before oss does a fork to
launch that child process (based on our own simulated clock).
*/
void CreatePCBentry(int entryIndex, pid_t pid, int startSeconds, int startNano)
{
    processTable[entryIndex].occupied = 1;
    processTable[entryIndex].pid = pid;
    processTable[entryIndex].startSeconds = startSeconds;
    processTable[entryIndex].startNano = startNano;
}

/*
This function updates the PCB of a terminated child. It should set the occupied flag to 0 (false),
set the pid, startSeconds, and startNano to 0.
childHasTerminated represents the pid of the child that has terminated.
*/
void updatePCB(int pidChildHasTerminated)
{
    for (int i = 0; i < MAX_CONCURRENT; i++)
    {
        if (processTable[i].pid == pidChildHasTerminated)
        {
            // log the index of the entry in the process table that is not occupied
            printf("The index of the entry in the process table that is not occupied: %d\n", i);

            // update PCB
            processTable[i].occupied = 0;
            processTable[i].pid = 0;
            processTable[i].startSeconds = 0;
            processTable[i].startNano = 0;
            break;
        }
    }
}

/*
The output of oss should consist of, every half a second in our simulated system, outputting the entire process table in a nice
format. For example:
OSS PID:6576 SysClockS: 7 SysclockNano: 500000
Process Table:
Entry Occupied PID StartS StartN
0 1 6577 5 500000
1 0 0 0 0
2 0 0 0 0
...
19 0 0 0 0
*/
void printProcessTable()
{
    printf("OSS PID: %d SysClockS: %d SysclockNano: %d\n", getppid(), simulatedClock[0], simulatedClock[1]);
    printf("Process Table:\n");
    printf("Entry Occupied PID StartS StartN\n");
    for (int i = 0; i < MAX_CONCURRENT; i++)
    {
        printf("%d %d %d %d %d\n", i, processTable[i].occupied, processTable[i].pid, processTable[i].startSeconds, processTable[i].startNano);
    }
    fflush(stdout);
}

void incrementClock()
{
    // Log the time before incrementing the simulated clock
    //printf("the time before incrementing the simulated clock:\n");
    //printf("simulatedClock[0]: %d\n", simulatedClock[0]);
    //fflush(stdout);
    // increment the simulated clock
    simulatedClock[1] += 5000000000;           // increment by 1000 nanoSeconds (1 microsecond); this is the increment referred to in the comment above
    if (simulatedClock[1] >= 1000000000) // if nanoSeconds is greater than or equal a second
    {
        simulatedClock[0]++;
        simulatedClock[1] = 0;
    }
}

// initialize the clock
void initializeClock()
{
    simulatedClock[0] = 0;
    simulatedClock[1] = 0;
}

/*
The check to see if a child has terminated should be done with a nonblocking wait() call. This can be
done with code along the lines of:

int pid = waitpid(-1, &status, WNOHANG);

waitpid will return 0 if no child processes have terminated and will return the pid of the child if
one has terminated.
*/
int checkIfChildHasTerminated()
{
    int status;
    int pid = waitpid(-1, &status, WNOHANG);
    if (pid == 0) // no child processes have terminated
    {
        return 0;
    }
    else // a child has terminated
    {
        return pid; // TODO: return the pid of the child that has terminated
    }
}
/*
checkIfAllChildrenHaveTerminated()
This function should return 1 if all children have terminated and 0 otherwise.
*/
int checkIfAllChildrenHaveTerminated()
{
    int allChildrenHaveTerminated = 1;
    for (int i = 0; i < MAX_CONCURRENT; i++)
    {
        if (processTable[i].occupied == 1)
        {
            allChildrenHaveTerminated = 0;
            break;
        }
    }
    return allChildrenHaveTerminated;
}

pid_t CreateChildProcess(int entryIndex, int time_limit)
{
    int randomSeconds = rand() % time_limit + 1;
    int randomNanoSeconds = rand() % BILLION;
    // log randomSeconds and randomNanoSeconds
    // log the time when the child was forked
    // printf("the time when the child was forked:\n");
    // printf("randomSeconds: %d\n", randomSeconds);
    // printf("randomNanoSeconds: %d\n", randomNanoSeconds);

    pid_t childPid = fork(); // This is where the child process splits from the parent
    if (childPid == 0)       // child process
    {
        // printf("1: I am a child but a copy of parent! My parent's PID is %d, and my PID is %d\n", getppid(), getpid()); // TODO: remove this line
        // the -t parameter is different. It now stands for the bound of time that a child process will be launched for.
        // So for example, if it is called with -t 7, then when calling worker processes, it should call them with a
        // time interval randomly between 1 second and 7 seconds (with nanoseconds also random).
        // Convert randomSeconds and randomNanoSeconds to strings
        char randomSecondsString[TIME_STRING_SIZE];
        char randomNanoSecondsString[TIME_STRING_SIZE];
        sprintf(randomSecondsString, "%d", randomSeconds);
        sprintf(randomNanoSecondsString, "%d", randomNanoSeconds);
        char *args[] = {"./worker", randomSecondsString, randomNanoSecondsString};
        int x = execlp(args[0], args[0], args[1], args[2], (char *)0); // check if this works with double args[0]
        if (x == -1)
        {
            perror("execlp");
            exit(EXIT_FAILURE);
        }

        printf("2: I am a child but a copy of parent! My parent's PID is %d, and my PID is %d\n", getppid(), getpid());
        // exit(EXIT_SUCCESS);
    }
    else if (childPid > 0) // parent process
    {
        printf("Parent: I am the parent! My PID is %d, and my child's PID is %d\n", getpid(), childPid);
        fflush(stdout);
        //wait(NULL); // This collects the child's exit status
    }
    else // childPid == -1
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    if (childPid == -1) // error
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    CreatePCBentry(entryIndex, childPid, randomSeconds, randomNanoSeconds);
    printf("CreatePCBentry has been called.\n");
    return childPid;
}

/*
Find entry in process table that is not occupied
Returns the index of the entry in the process table that is not occupied.
If no entry is found, returns -1.
*/
int FindEntryInProcessTableNotOccupied()
{
    int entryIndex = -1;
    for (int i = 0; i < MAX_CONCURRENT; i++)
    {
        if (processTable[i].occupied == 0)
        {
            entryIndex = i;
            break;
        }
    }
    return entryIndex;
}

/* Define a global signal handler function. */
void SIGINT_handler(int sig)
{
    /* Issue a message */
    fprintf(stderr, "OSS interrupted. Shutting down.\n");
    /* free up shared memory and then terminate all children */
    cleanup();

    /* Terminate the program */
    exit(EXIT_FAILURE);
}

/* Free up shared memory and then terminate all children */
void cleanup()
{
    // get rid of message queue
	if (msgctl(msqid, IPC_RMID, NULL) == -1) {
		perror("msgctl to get rid of queue in parent failed");
		exit(1);
	}

    // Detach and remove shared memory segment
    // TODO: Detach the shared memory segment ONLY if no children are active.
    if (shmdt(simulatedClock) == -1)
    {
        fprintf(stderr, "Parent: ... Error in shmdt ...\n");
        exit(1);
    }
    if (shmctl(shmid, IPC_RMID, NULL) == -1) // get rid of message queue
    {
        fprintf(stderr, "Parent: ... Error in shmctl ...\n");
        exit(1);
    }
}

/* This function end a message to the message queue */
void send_message(int childIndex, char *message)
{
    printf("Sending a message ...\n");
    // Send a message only to child with index childIndex
	msgBuffer.mtype = processTable[childIndex].pid;
	msgBuffer.intData = processTable[childIndex].pid;   // we will give it the pid we are sending to, so we know it received it
	strcpy(msgBuffer.strData, message);

	if (msgsnd(msqid, &msgBuffer, sizeof(msgbuffer)-sizeof(long), 0) == -1) {
        sprintf(message, "msgsnd to child %d failed\n", childIndex);
		perror(message);
        cleanup();
		exit(1);
	}
}

/* This function receives a message from the message queue */
void receive_message(int childIndex)
{
    char message[100];
    printf("Receiving a message ...\n");
    // Then let me read a message, but only one meant for me
    // ie: the one the child just is sending back to me
    if (msgrcv(msqid, &receiveBuffer, sizeof(msgbuffer), getpid(), 0) == -1) {
        sprintf(message, "failed to receive message in parent\n");
        perror(message);
        cleanup();
        exit(1);
    }	
    printf("Parent %d received message: %s was my message and my int data was %d\n",
           getpid(), receiveBuffer.strData, receiveBuffer.intData);
}