# include <stdio.h>
# include <stdint.h>
# include <string.h>
# include <unistd.h>
# include <stdlib.h>
# include <stdbool.h>
# include <sys/msg.h>
# include <sys/ipc.h>
# include <sys/shm.h>

// Globals
int simClock[2] = {0, 0};    
int terminationTime[2] = {0, 0};    
int timeIncrement[2] = {0, 0}; 

# define SHM_KEY 205431 
# define PERMS 0644   

typedef struct messages {
    long mtype;
    int intData;
} messages;

int queueID;
messages msgBuffer;      


// function prototypes
void startWorkerTasks();

int main(int argc, char** argv) {
    if (argc != 3) {
        perror("Invalid command line arguments'\n");
        exit(1);
    }

    // Calculate worker end time
    timeIncrement[0] = atoi(argv[1]);
    timeIncrement[1] = atoi(argv[2]);

    key_t msgQueueKey = ftok("msgq.txt", 1);
    if (msgQueueKey == -1) {
        perror("Failed to generate a key using ftok.\n");
        exit(1);
    }

    queueID = msgget(msgQueueKey, PERMS);
    if (queueID == -1) {
        perror("Failed to access the message queue.\n");
        exit(1);
    }

    startWorkerTasks();

    // show termination msg
    printf("WORKER PID: %d, PPID: %d, SysClockS: %d SysclockNano: %d TermTimeS: %d TermTimeNano: %d -- %s\n", 
        getpid(), getppid(), simClock[0], simClock[1], terminationTime[0], terminationTime[1], "Terminating");

    return 0;
}

// wait for worker to finish
void startWorkerTasks() {
    int time_passed = 0;  
    int prevTime = 0;

    // Main loop for termination
    while (true) {
        // Wait for an available message and retrieve new simulated time
        if (msgrcv(queueID, &msgBuffer, sizeof(msgBuffer), getpid(), 0) == -1) {
            perror("Error: Failed to receive a message in the parent process.\n");
            exit(EXIT_FAILURE);
        }

        // Access and attach to shared memory to update the local simulated clock
        int sharedMemID = shmget(SHM_KEY, sizeof(int) * 2, 0777); 
        if (sharedMemID == -1) {
            perror("Error: Failed to access shared memory using shmget.\n");
            exit(EXIT_FAILURE);
        }

        int* sharedMemPtr = (int*)shmat(sharedMemID, NULL, SHM_RDONLY);
        if (sharedMemPtr == NULL) {
            perror("Error: Failed to attach to shared memory using shmat.\n");
            exit(EXIT_FAILURE);
        }

        simClock[0] = sharedMemPtr[0];     
        simClock[1] = sharedMemPtr[1];  
        shmdt(sharedMemPtr);

        if (terminationTime[0] == 0 && terminationTime[1] == 0) 
        {
            terminationTime[0] = simClock[0] + timeIncrement[0];
            terminationTime[1] = simClock[1] + timeIncrement[1];
            printf("WORKER PID: %d, PPID: %d, SysClockS: %d SysclockNano: %d TermTimeS: %d TermTimeNano: %d -- %s\n", 
                getpid(), getppid(), simClock[0], simClock[1], terminationTime[0], terminationTime[1], "Received message");
        }

        // Check if it's time to handle worker messages
        if ((simClock[0] - prevTime) >= 1)
        {
            time_passed++;
            prevTime = simClock[0];
            char message[255] = "";
            sprintf(message, "WORKER PID: %d, PPID: %d, SysClockS: %d SysclockNano: %d TermTimeS: %d TermTimeNano: %d -- %d seconds have passed since starting\n", 
                getpid(), getppid(), simClock[0], simClock[1], terminationTime[0], terminationTime[1], time_passed);
            
            printf("%s", message);
        }

        // Check if the clock reached the worker termination time
        if (simClock[0] >= terminationTime[0]) 
        {
            msgBuffer.intData = 0;   
        }
        else 
        {
            msgBuffer.intData = 1;
        }

        msgBuffer.mtype = getppid();
        if (msgsnd(queueID, &msgBuffer, sizeof(messages)-sizeof(long), 0) == -1) {
            perror("Error, msgsnd to parent failed\n");
            exit(EXIT_FAILURE);
        }
        if (msgBuffer.intData == 0)
        {
            break;
        }
    }
}

