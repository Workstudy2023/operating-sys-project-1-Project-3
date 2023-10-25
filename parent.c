# include <time.h>
# include <stdio.h>
# include <ctype.h>
# include <stdint.h>
# include <getopt.h>
# include <unistd.h>
# include <stdlib.h>
# include <signal.h>
# include <string.h>
# include <sys/ipc.h>
# include <sys/msg.h>
# include <sys/shm.h>
# include <stdbool.h>
# include <sys/wait.h>
# include <sys/types.h>

# define SHM_KEY 205431
# define PERMS 0644    

// Process Control Block structure
typedef struct PCB {
    pid_t pid;               
    int occupied;             
    int nanoseconds;       
    int seconds;    
} PCB;

// Message buffer
typedef struct Message {
    long mtype;    
    int intData;  
} Message;

// globals
int queueID;      
Message buffer;   

int shmID;             
int* shmPtr;            
int simClock[2] = {0, 0};  
char* logfile = NULL;    

struct PCB* workerTable;
int processCount;
int simultaneousCount;
int processTimeLimit;
char* tableStr = NULL;
size_t tableStrSize = 0;
int terminatedWorkerCount = 0;

// function prototypes
void handleTermination();
void launchworkers();
void updateWorkerTable();
void sendMessage(int index);

// main
int main(int argc, char** argv) {
    // Register signal handlers for interruption and timeout
    signal(SIGINT, handleTermination);
    signal(SIGALRM, handleTermination);
    alarm(60); 
    
    // Iterate through the arguments manually
    for(int i = 1; i < argc; i += 2) 
    {
        if (i + 1 >= argc) {
            printf("%s", "invalid command line argument(s)\n");
            exit(1);
        }

        char* flag = argv[i];
        char* value = argv[i + 1];
        if (strcmp(flag, "-h") == 0) 
        {
            
            printf("-h : help menu\n"
                "-n : Set the number of processes to be launched\n"
                "-s : Specify the maximum number of concurrent running processes\n"
                "-t : Define the maximum execution time for each child process\n"
                "-f : Provide the filename to log worker process information, messages, and the process table");
                exit(0);
        } 
        else if (strcmp(flag, "-n") == 0) {
            processCount = atoi(value);
        } else if (strcmp(flag, "-s") == 0) {
            simultaneousCount = atoi(value);
            if (simultaneousCount >= 20) {
                exit(1);
            }
        } else if (strcmp(flag, "-t") == 0) {
            processTimeLimit = atoi(value);
        } else if (strcmp(flag, "-f") == 0) {
            char* inputFile = value;
            FILE* file = fopen(inputFile, "r");
            if (file) {
                logfile = inputFile;
                fclose(file);
            } else {
                printf("file doesn't exist.\n");
                exit(1);
            }
        } else {
            printf("%s", "invalid argument\n");
            exit(1);
        }
    }    

    // Set up memory and initialize the process table
    PCB workerProcessTable[processCount];
    for (int i = 0; i < processCount; i++) 
    {
        workerProcessTable[i].pid = 0;
        workerProcessTable[i].occupied = 0;
        workerProcessTable[i].nanoseconds = 0;
        workerProcessTable[i].seconds = 0;
    }

    // Get a shared memory segment
    shmID = shmget(SHM_KEY, sizeof(int) * 2, 0777 | IPC_CREAT);
    if (shmID == -1) 
    {
        perror("Unable to acquire the shared memory segment.\n");
        exit(1);
    }
    shmPtr = (int*)shmat(shmID, NULL, 0);
    if (shmPtr == NULL) 
    {
        perror("Unable to connect to the shared memory segment.\n");
        exit(1);
    }

    memcpy(shmPtr, simClock, sizeof(int) * 2);
    key_t messageQueueKey = ftok("msgq.txt", 1);
    if (messageQueueKey == -1) 
    {
        perror("Unable to generate a key for the message queue.\n");
        exit(1);
    }

    queueID = msgget(messageQueueKey, PERMS | IPC_CREAT);
    if (queueID == -1) 
    {
        perror("Unable to create or access the message queue.\n");
        exit(1);
    }

    workerTable = workerProcessTable;
    launchworkers();
    handleTermination();
    return 0;
}

// terminate program
void handleTermination() {
    msgctl(queueID, IPC_RMID, NULL);
    shmdt(shmPtr);
    shmctl(shmID, IPC_RMID, NULL); 
    kill(0, SIGTERM);  
}

// updates the worker table string
void updateWorkerTable() {
    // Clear the existing table contents
    if (tableStr != NULL)
        free(tableStr);

    size_t new_size = processCount * 256;
    tableStr = (char*)malloc(new_size);
    tableStr[0] = '\0';

    char buff[200];
    snprintf(buff, sizeof(buff), "\n\nOSS PID: \t%d SysClockS: \t%d SysclockNano: \t%d\nProcess Table: \n%s\t%s\t%s\t%s\t%s\n\n",
        getpid(), simClock[0], simClock[1], "Entry", "Occupied", "PID", "StartS", "StartN");

    strcat(tableStr, buff);
    for (int i = 0; i < processCount; i++) 
    {
        char rowStr[200];
        snprintf(rowStr, sizeof(rowStr), "%d\t%d\t%d\t\t%d\t%d\n", 
            i, workerTable[i].occupied, workerTable[i].pid, workerTable[i].seconds, workerTable[i].nanoseconds);
        strcat(tableStr, rowStr);
    }

    tableStrSize = new_size;
}

// start work processes
void launchworkers() {
    int busyWorkers[processCount];
    int newTimeLimit = processTimeLimit;
    int counter1 = 0; 
    double timeElapsed = 0.0;   
    int workerIndex = 0;                    
    int occupiedWorkerCount = 0;                
    double delay = 0.0;

    tableStrSize = processCount * 256;
    tableStr = (char*)malloc(tableStrSize);
    if (tableStr == NULL) {
        perror("Memory allocation error in parent\n");
        exit(1);
    }
    tableStr[0] = '\0';

    while (true) {
        // show table
        delay = 0.5 + ((double)rand() / RAND_MAX) * 0.2;
        if (timeElapsed > delay) {
            updateWorkerTable();
            FILE* file = fopen(logfile, "a");
            if (file) {
                fprintf(file, "%s", tableStr);
                fclose(file);
            }
            printf("%s", tableStr);
            timeElapsed = 0;
        }   

        // Increment simulated clock
        int nanosecondsPerSecond = 1000000000;
        int nanosecondsPerTenthSecond = 100000000;
        simClock[1] += nanosecondsPerTenthSecond;
        if ((simClock[1] + nanosecondsPerTenthSecond) >= nanosecondsPerSecond) {
            simClock[0] += 1;
            simClock[1] = 0;
        }
        else 
        {
             simClock[1] += nanosecondsPerTenthSecond;
        }
        memcpy(shmPtr, simClock, sizeof(int) * 2);
        timeElapsed += 0.1;

        if (counter1 < processCount && counter1 < simultaneousCount + terminatedWorkerCount) {
            // Calculate available processes to run
            int remaining = (simultaneousCount + terminatedWorkerCount) - counter1;
            for (int i = 0; i < remaining; i++) {
                pid_t pid = fork();

                // Identify processes
                if (pid == 0) {
                    srand(time(NULL) + getpid());
                    int randomSeconds = (rand() % newTimeLimit) + 1;
                    int randNanoSeconds = (rand() % 100000) + 1;

                    // Launch worker
                    char secondsBuffer[12];
                    char nanosecondsBuffer[12];
                    char* args[] = {"./worker", secondsBuffer, nanosecondsBuffer, NULL};
                    sprintf(secondsBuffer, "%d", randomSeconds);
                    sprintf(nanosecondsBuffer, "%d", randNanoSeconds);
                    execvp(args[0], args);
                }
                else {
                    // [Parent], store process information
                    workerTable[counter1].pid = pid;
                    workerTable[counter1].occupied = 1;
                    workerTable[counter1].seconds = simClock[0];
                    workerTable[counter1].nanoseconds = simClock[1];
                } 

                busyWorkers[occupiedWorkerCount++] = counter1;
                counter1 += 1;
            }
        }

        if (occupiedWorkerCount > 0) {
            int currentTerminationCount = terminatedWorkerCount;
            int workerToMessageIndex = busyWorkers[workerIndex];

            // Send a message to the selected worker and check worker status
            sendMessage(workerToMessageIndex);
            if (currentTerminationCount != terminatedWorkerCount) {
                occupiedWorkerCount--;
                for (int i = workerIndex; i < occupiedWorkerCount; i++) {
                    busyWorkers[i] = busyWorkers[i + 1];
                }
            }
            workerIndex += 1;
            if (workerIndex >= occupiedWorkerCount) 
            {
                workerIndex = 0; 
            }    
        }

        // Check if all workers have finished processing
        if (terminatedWorkerCount >= processCount) {
            free(tableStr);
            return;
        }
    
    }
}

// Send a message to the worker at the given index
void sendMessage(int workerIndex) {
    // Message buffer to hold communication data
    char msgBuffer[255];

    PCB targetWorker = workerTable[workerIndex];
    buffer.mtype = targetWorker.pid;
    buffer.intData = 0;

    // Output a message indicating the message is being sent to the worker
    sprintf(msgBuffer, "OSS: Sending message to worker \t%d PID \t%d at time %d:%d\n", workerIndex, targetWorker.pid, simClock[0], simClock[1]);
    printf("%s\n", msgBuffer);
    FILE* logFile = fopen(logfile, "a");
    if (logFile) {
        fprintf(logFile, "%s", msgBuffer);
        fclose(logFile);
    }

    // Send the message to the worker
    if (msgsnd(queueID, &buffer, sizeof(Message) - sizeof(long), 0) == -1) {
        perror("msgsnd to child failed\n");
        exit(1);
    }

    // Read the response message from the child
    Message childMsg;
    if (msgrcv(queueID, &childMsg, sizeof(Message), getpid(), 0) == -1) {
        perror("Error couldn't receive message in parent\n");
        exit(1);
    }

    // Check the worker's message
    if (childMsg.intData == 0) {
        sprintf(msgBuffer, "OSS: Worker %d PID %d is about to terminate.\n", workerIndex, targetWorker.pid);
        printf("%s\n", msgBuffer);
        FILE* logFile = fopen(logfile, "a");
        if (logFile) {
            fprintf(logFile, "%s", msgBuffer);
            fclose(logFile);
        }

        // confirm termination of the worker
        for(;;) {
            pid_t result = waitpid(workerTable[workerIndex].pid, NULL, WNOHANG);
            if (result == workerTable[workerIndex].pid) {
                workerTable[workerIndex].occupied = 0;
                terminatedWorkerCount += 1;
                break;
            }
        }

    } else {
        // Output a message indicating a received message from the worker
        sprintf(msgBuffer, "OSS: Receiving message from worker %d PID \t%d at time %d:%d\n",workerIndex, targetWorker.pid, simClock[0], simClock[1]);
        printf("%s\n", msgBuffer);
        FILE* logFile = fopen(logfile, "a");
        if (logFile) {
            fprintf(logFile, "%s", msgBuffer);
            fclose(logFile);
        }
    }
}
