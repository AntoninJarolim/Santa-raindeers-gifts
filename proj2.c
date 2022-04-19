/*
 * Antonín Jarolím
 * FIT VUTBR
 * 30.4.2021
 * IOS - second project
 */

#include <stdio.h>
#include <stdlib.h>

#include <semaphore.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>

#include <time.h>
#include <stdbool.h>

typedef struct{
    FILE * outFile;
    int outNum; // value for printing out
    unsigned int procCount; // number of proceses using this sharedMem
    int shmid; // shared memory id
    int rdAtHomeCount; // counter of raindeers which are at home
    int elfsInQueueCount; // counter of waiting elves
    int elfsInWorkshopCount; // counter of elves in workshop
    bool workshopClosed;
    sem_t semOutWrite; // semaphore for printing out whatever
    sem_t semUsingProcCount; // semaphore for counting processes
    sem_t semIsUsed; // semmaphore to block main process from deallocating memory
    sem_t semHitchRD; // semaphore for waiting raindeers to be hitched
    sem_t semRdAtHomeCount; // semaphore to safe count how many raindeers are at home
    sem_t semSleepinSanta; // semaphore indicating whether is Santa sleeping
    sem_t semElfInWorkshop; // semaphore where Elves waits for santa to be waken up
    sem_t semElfCount; // semaphore to safe count how many elves are waiting
    sem_t semHitching; // santa wait for RD to call free this
    sem_t semHelping; // santa helping Elves with
    sem_t semBlockDeletingMemory; // sometimes main procces deatach shm before needed, this souhld stop him
} SharedMem;

void killAllProc(){
    system("killall proj2");
}

void shmDestroy(SharedMem *shm){
    // Remove the semaphores
    fclose(shm->outFile);
    sem_destroy(&shm->semOutWrite);
    sem_destroy(&shm->semIsUsed);
    sem_destroy(&shm->semUsingProcCount);
    // Remove the shared memory
    int shmid = shm->shmid;
    shmdt(shm);
    shmctl(shmid, IPC_RMID, NULL);

    // printf("Shared memory and semaphores were destroyed.\n");
}

void printHelp(){
    printf("Usage: \n");
    printf("\t ./proj2 NE NR TE TR\n");
    printf("\t NE: number of elfs. 0<NE<1000\n");
    printf("\t NR: počet sobů. 0<NR<20\n");
    printf("\t TE: time in ms at which elf works. 0<=TE<=1000.\n");
    printf("\t TR: time in ms at which reindeer come back. 0<=RE<=1000.\n");
}

void printError(char * str){
    fprintf(stderr, "%s", str);
}

int parseNumber(char *toParse, char * name, int  max){
    int parsed = atoi(toParse);
    char errormsg[80];
    if (parsed < 0){
        sprintf(errormsg,"Invalid argument %s.\nExiting!\n", name);
        printError(errormsg);
        exit(1);
    }
    if(parsed > max){
        sprintf(errormsg,"Argument %s cannot be greater than %d.\nExiting!\n", name, max);
        printError(errormsg);
        exit(1);
    }
    return parsed;
}

void tryLockShm(SharedMem *shm){
    sem_wait(&shm->semUsingProcCount);
    if(shm->procCount == 0){ // nikdo nelocknul (jsem prvni)
        sem_wait(&shm->semIsUsed); // locknu ja
    }
    //printf("###+: %d -> %d\n", shm->procCount, shm->procCount+1);
    shm->procCount++;
    sem_post(&shm->semUsingProcCount);
}

void tryUnlockShm(SharedMem *shm){
    sem_wait(&shm->semUsingProcCount);
    //printf("###-: %d -> %d\n", shm->procCount, shm->procCount-1);
    shm->procCount--;
    if(shm->procCount == 0){ // jsem posledni process
        sem_post(&shm->semIsUsed); // unlocknu ja
    }
    sem_post(&shm->semUsingProcCount);
}

int getRand(int min, int max, unsigned int seed){
    if (max == 0){
        return 0;
    }
    srand(seed);
    int rnd =  (rand() % (max - min + 1)) + min;
    return rnd;
}



void procInitFailed(SharedMem *shm){
    printError("Process could not be started.\n");
    shmDestroy(shm);
    killAllProc();
    exit(1);
}
void createSemaphore(sem_t * sem, int pshared, int value, SharedMem *shm){
    int semerr = sem_init(sem, pshared, value);
    if(semerr < 0){
        shmDestroy(shm);
        exit(1);
    }
}

void runSanta(SharedMem * shm, int NR, int NE);

void runElf(int id, int maxWorking, SharedMem * shm);
void createElfs(int elfCount, int TE, SharedMem * shm){
    for (int i = 0; i < elfCount; ++i) {
        int pid;
        if((pid = fork()) == 0){
            // this code runs Elfes
            int elfId = i+1;
            runElf(elfId, TE, shm);
        } else if (pid < 0){
            procInitFailed(shm);
        }
    }
}

void runRaindeer(int id, int maxToReturn, SharedMem * shm, int NR);
void createRaindeers(int raindeerCount, int TR, SharedMem * shm){
    for (int i = 0; i < raindeerCount; ++i) {
        int pid;
        if((pid = fork()) == 0){
            // this code runs Raindeers
            int raindeerId = i+1;
            runRaindeer(raindeerId, TR, shm, raindeerCount);
        } else if (pid < 0){
            procInitFailed(shm);
        }
    }
}

void safeWrite(char *message, SharedMem *shm){
    sem_wait(&shm->semOutWrite);
    fprintf(shm->outFile,"%d: %s",shm->outNum, message);
    shm->outNum++;
    sem_post(&shm->semOutWrite);
}
int main (int argc, char *argv[]){
        /*  Parsing arguments. */
    int NE, NR, TE, TR;
    if(argc == 5){
        NE = parseNumber(argv[1], "NE", 999);
        NR = parseNumber(argv[2], "NR", 19);
        TE = parseNumber(argv[3], "TE", 999);
        TR = parseNumber(argv[4], "TR", 999);
    }else{
        printf("Wrong parameters.\n");
        printHelp();
        return 1;
    }
    // printf("NE: %d\nNR: %d\nTE: %d\nTR: %d\n", NE, NR, TE, TR);

        /* Memory and semaphores allocations. */


    // Shared memory allocation
    int sharedMemKey = 4000;
    int shmid = shmget(sharedMemKey, sizeof(SharedMem), IPC_CREAT | 0666);
    if(shmid < 0){
        printf("Shared memory could not be created.\n");
        return 1;
    }
    SharedMem *shm = shmat(shmid, NULL, 0);
    if(shm == (SharedMem *) -1){
        printf("Shared memory could not be created.\n");
        return 1;
    }
    // File opening
    char *filename = "proj2.out";
    shm->outFile = fopen(filename, "w"); // change stdout to proj2.out
    setbuf(shm->outFile, NULL);
    if (shm->outFile == NULL) {
        printError("Cannot open file proj2.out.\n");
        exit(1);
    }

    shm->shmid = shmid;
    shm->outNum = 1; // First num to write is 1
    shm->procCount = 0; // number of processes using shared memory
    shm->rdAtHomeCount = 0; // number of raindeers at home
    shm->workshopClosed = false;
    shm->elfsInQueueCount = 0;
    // Semaphores allocation.
    const int pshared = 1; // semafor mam ve sdilene pameti
    unsigned int value = 1; // zacinam s otevrenym semaforem
    createSemaphore(&shm->semOutWrite, pshared, value, shm);
    createSemaphore(&shm->semIsUsed, pshared, value, shm);
    createSemaphore(&shm->semUsingProcCount, pshared, value, shm);
    createSemaphore(&shm->semRdAtHomeCount, pshared, value, shm);
    createSemaphore(&shm->semElfCount, pshared, value, shm);

    value = 0;
    createSemaphore(&shm->semHitchRD, pshared, value, shm);
    createSemaphore(&shm->semSleepinSanta, pshared, value, shm);
    createSemaphore(&shm->semElfInWorkshop, pshared, value, shm);
    createSemaphore(&shm->semHitching, pshared, value, shm);
    createSemaphore(&shm->semHelping, pshared, value, shm);
    createSemaphore(&shm->semBlockDeletingMemory, pshared, value, shm);

    /* Main part of the program. */

    tryLockShm(shm);
    int pid = fork();
    if(pid == 0){
        tryLockShm(shm);
        // This is child which creates Elfs and Raindeers
        pid = fork();
        if (pid == 0){
            // create Elfs
            createElfs(NE, TE, shm);
            exit(0);
        }else if (pid > 0){
            // create Raindeers
            createRaindeers(NR, TR, shm);
        } else{
            procInitFailed(shm);
        }
        // Kill the proces which creates Elfs and raindeers
        tryUnlockShm(shm);
        exit(0);
    }else if (pid > 0){
        // this is run by main process
        tryLockShm(shm);
        pid = fork();
        if(pid == 0){
            // This is Santa
            runSanta(shm, NR, NE);
        }else if (pid < 0){
            procInitFailed(shm);
        }
        tryUnlockShm(shm);
    } else{
        procInitFailed(shm);
    }

    usleep(800); // wait for any procces to lock semIsUsed
    tryUnlockShm(shm);
    sem_wait(&shm->semBlockDeletingMemory); // proces santa started
    sem_wait(&shm->semBlockDeletingMemory); // first process elf started
    sem_wait(&shm->semBlockDeletingMemory); // first process rd started
    sem_wait(&shm->semIsUsed);
        /* Memory and semaphores deallocating. */
    shmDestroy(shm);
    return 0;
}

void runSanta(SharedMem * shm, int NR, int NE){
    tryLockShm(shm);
    sem_post(&shm->semBlockDeletingMemory); // value in tryLockShm is incremented
    char message[80];
    while (1){
        sprintf(message,"Santa: going to sleep\n");
        safeWrite(message, shm);

        sem_wait(&shm->semSleepinSanta); // cekani na vzbuzeni santy (spici santa)
        if (NR == shm->rdAtHomeCount){ // Last raindeer woke up Santa
            sprintf(message,"Santa: closing workshop\n");
            safeWrite(message, shm);
            shm->workshopClosed = true;
            break; // Santa goes hitching raindeers
        }else{ // Santa was waken up by Elves
            sprintf(message,"Santa: helping elves\n");
            safeWrite(message, shm);
            int elvesToCome = 3;
            for (int i = 0; i < elvesToCome; ++i) {
                sem_post(&shm->semElfInWorkshop); // Santa let 3 elves go into workshop
            }
            // Santa waiting for elves to let him sleep
            sem_wait(&shm->semHelping);
        }
    }
    // send Elves to holiday
    for (int i = 0; i < NE; ++i) {
        sem_post(&shm->semElfInWorkshop);
    }
    // Santa frees semaphore where RDs are waiting for getting hitched
    sem_post(&shm->semHitchRD);

    // Waiting for all raindeers to get hitched
    sem_wait(&shm->semHitching);

    sprintf(message,"Santa: Christmas started\n");
    safeWrite(message, shm);

    tryUnlockShm(shm);
    exit(0);
}

void runElf(int id, int maxWorking, SharedMem * shm){
    tryLockShm(shm);
    sem_post(&shm->semBlockDeletingMemory); // value in tryLockShm is incremented
    char message[80];
    // initial start
    sprintf(message,"Elf %d: started\n", id);
    safeWrite(message, shm);

    while(1) {
        // wait for elf do his work
        int timeToWork = getRand(0, maxWorking, getpid());
        // printf("time to work: %d\n", timeToWork);
        usleep(timeToWork);

        // Elf need help
        sprintf(message, "Elf %d: need help\n", id);
        safeWrite(message, shm);

        // Elf is waiting in queue
        sem_wait(&shm->semElfCount); // safe count
        if (shm->workshopClosed){
            sem_post(&shm->semElfCount);
            break;
        }
        shm->elfsInQueueCount++;
        if (shm->elfsInQueueCount == 3){ // if i'm third before workshop
            sem_post(&shm->semSleepinSanta); // then I'm waking up Santa
        }
        sem_post(&shm->semElfCount);

        // Waiting to get in workshop
        sem_wait(&shm->semElfInWorkshop);
        // Elf enters workshop
        if (shm->workshopClosed){
            break;
        }

        sem_wait(&shm->semElfCount); // safe count
        if(shm->elfsInWorkshopCount == 0){
            shm->elfsInWorkshopCount = 3;
        }
        shm->elfsInQueueCount--;
        sem_post(&shm->semElfCount);

        sprintf(message,"Elf %d: get help\n", id);
        safeWrite(message, shm);

        // Elf is waiting in queue
        sem_wait(&shm->semElfCount); // safe count
        shm->elfsInWorkshopCount--;
        if (shm->elfsInWorkshopCount == 0){ // if i'm last to leave
            sem_post(&shm->semHelping); // then I let Santa sleep
        }
        sem_post(&shm->semElfCount);
    }

    sprintf(message,"Elf %d: taking holidays\n", id);
    safeWrite(message, shm);

    tryUnlockShm(shm);
    exit(0);
}

void runRaindeer(int id, int maxToReturn, SharedMem * shm, int NR){
    tryLockShm(shm);
    sem_post(&shm->semBlockDeletingMemory); // value in tryLockShm is incremented
    char message[80]; // message to be written out
    // initial start
    sprintf(message,"RD %d: rstarted\n", id);
    safeWrite(message, shm);

    // waiting to get back from holiday
    int retFromHoliday = getRand(maxToReturn/2, maxToReturn, getpid());
    // printf("time retFromHoliday: %d\n", retFromHoliday);
    usleep(retFromHoliday);

    // raindeer got back from holiday
    sprintf(message,"RD %d: return home\n", id);
    safeWrite(message, shm);

    // counting raindeers, last RD will wake up santa
    sem_wait(&shm->semRdAtHomeCount); // safe count
    // printf("RdCounter: %d -> %d\n", shm->rdAtHomeCount, shm->rdAtHomeCount +1);
    shm->rdAtHomeCount++;
    if (NR == shm->rdAtHomeCount){ // if i'm last RD
        sem_post(&shm->semSleepinSanta); // then I'm waking up Santa
    }
    sem_post(&shm->semRdAtHomeCount);

    // waiting for all RDs to come back
    sem_wait(&shm->semHitchRD);
    sem_post(&shm->semHitchRD);
    sprintf(message,"RD %d: get hitched\n", id); // hitch RD
    safeWrite(message, shm);

    // countdown hitched RDs
    sem_wait(&shm->semRdAtHomeCount); // safe count
    shm->rdAtHomeCount--;
    if (shm->rdAtHomeCount == 0){ // if i'm last RD
        sem_post(&shm->semHitching); // then tell santa to declare Christmas
    }
    sem_post(&shm->semRdAtHomeCount);// safe count

    tryUnlockShm(shm);
    exit(0);
}
