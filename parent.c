#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <unistd.h>

#define PERMS 0644
typedef struct msgbuffer {
	long mtype;
	char strData[100];
	int intData;
} msgbuffer;

int main(void) {
	msgbuffer buf0, buf1;
	int msqid;
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

	// store pids of our first two children to launch
	pid_t child[2];

	int i = 0;
	// create our two children
	for (i = 0; i < 2; i++) {
		// lets fork off a child
		pid_t pid = fork();
		if (pid > 0) {
			// save this child's pid
			child[i] = pid;
		}
		else if (pid == 0)  {
			// in child, so lets exec off child executable
			execlp("./child","./child",(char *)NULL);

			// should never get here if exec worked
			printf("Exec failed for first child\n");
			exit(1);
		}
		else {
			// fork error
			perror("fork failed in parent");
		}

	}

	// lets send a message only to child1, not child0
	buf1.mtype = child[1];
	buf1.intData = child[1];   // we will give it the pid we are sending to, so we know it received it
	strcpy(buf1.strData,"Message to child 1\n");

	if (msgsnd(msqid, &buf1, sizeof(msgbuffer)-sizeof(long), 0) == -1) {
		perror("msgsnd to child 1 failed\n");
		exit(1);
	}

	msgbuffer rcvbuf;
	// Then let me read a message, but only one meant for me
	// ie: the one the child just is sending back to me
	if (msgrcv(msqid, &rcvbuf,sizeof(msgbuffer), getpid(),0) == -1) {
		perror("failed to receive message in parent\n");
		exit(1);
	}	
	printf("Parent %d received message: %s was my message and my int data was %d\n",getpid(),rcvbuf.strData,rcvbuf.intData);

	// now a message only to child0, not child1
	buf0.mtype = child[0];
	buf0.intData = child[0];
	strcpy(buf0.strData,"Message to child 0\n");

	if (msgsnd(msqid, &buf0, sizeof(msgbuffer)-sizeof(long), 0) == -1) {
		perror("msgsnd to child 0 failed\n");
		exit(1);
	}	


	// Now get the message back from the second child 
	if (msgrcv(msqid, &rcvbuf,sizeof(msgbuffer), getpid(),0) == -1) {
		perror("failed to receive message in parent\n");
		exit(1);
	}	
	printf("Parent %d received message: %s was my message and my int data was %d\n",getpid(),rcvbuf.strData,rcvbuf.intData);





	// wait for children to end
	for (i = 0; i < 2; i++) {
		wait(0);
	}


	// get rid of message queue
	if (msgctl(msqid, IPC_RMID, NULL) == -1) {
		perror("msgctl to get rid of queue in parent failed");
		exit(1);
	}

	return 0;
}
