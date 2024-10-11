#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>
#include <pthread.h>

#define N_PROCESSOS 3
#define MAX 10

int GLOBAL_DEVICE = -1;
int GLOBAL_TIMEOUT = -1;
int GLOBAL_HAS_SYSCALL = -1;
int GLOBAL_TERMINATED = -1;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Estrutura da fila
struct queue {
    char* nome;
    int items[N_PROCESSOS];
    int primeiro, ultimo;
};typedef struct queue Queue;

//Estrutura de PCB
struct pcb {
    int PC; 
    char state[200]; //3 Estados Executing, Blocked DX Op, Terminated
    int qttD1;
    int qttD2;
};typedef struct pcb PCB;

// Prototipos das funções
void SignalHandler(int sinal);
void Syscall(char Dx, char Op,char* shm);
void processo(char* shm, PCB* pcb, int id);
void InterruptController();
void initQueue(Queue* q, char* nome);
void printQueue( Queue* q);
int isFull( Queue* q);
int isEmpty( Queue* q);
void enqueue( Queue* q, int value);
int dequeue( Queue* q);
int peek( Queue* q);
int encontrarIndex(int vetor[], int tamanho, int valor);


int main(void) {
    Queue blocked_D1;
    Queue blocked_D2;
    Queue ready_processes;
    Queue exec_process;
    Queue terminated_process;
    int ProcessControlBlock;
    PCB *pPCB;
    char systemcall, *sc;
    int pidInterrupter;
    int pidProcesses[N_PROCESSOS]; 


    initQueue(&blocked_D1, "de bloqueado_1");
    initQueue(&blocked_D2, "de bloqueado_2");
    initQueue(&ready_processes, "de Pronto");
    initQueue(&exec_process, "de ativo");
    initQueue(&terminated_process, "de terminado");
    systemcall = shmget (IPC_PRIVATE, 2*sizeof(char), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR); //Shm para as informacoes de Device e Operation
    ProcessControlBlock = shmget (IPC_PRIVATE, N_PROCESSOS*sizeof(PCB), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
     
    pidInterrupter = fork();
    if (pidInterrupter == 0) { //Interrupter
        InterruptController();  
        exit(0);
    } 
    else //KernelSim 
    { 
        if(pidInterrupter != 0) //Atribui o signal handler ao pai 
        {
            signal(SIGUSR1, SignalHandler);  
            signal(SIGUSR2, SignalHandler);  
            signal(SIGTERM, SignalHandler);  
            signal(SIGTSTP, SignalHandler); 
            signal(SIGIO, SignalHandler);
        }
        for (int i = 0; i < N_PROCESSOS; i++) {
            pidProcesses[i] = fork();
            if (pidProcesses[i] == 0) //Filho
            {  
                pPCB = (PCB*) shmat (ProcessControlBlock, 0, 0);
                sc = (char*)shmat(systemcall,0,0); //Conecta os filhos com shm
                processo(sc, pPCB, i);  // Executa a função do processo
                exit(0);  // Saída do processo filho
            }
        }


        sc = (char*)shmat(systemcall,0,0); //Conecta o pai com shm
        pPCB = (PCB*) shmat (ProcessControlBlock, 0, 0);
        
        for(int i =0; i<N_PROCESSOS;i++) //Filhos criados, agora coloca na fila
        {
            pthread_mutex_unlock(&mutex);
            enqueue(&ready_processes, pidProcesses[i]);
            pthread_mutex_unlock(&mutex);
        }

        int current = dequeue(&ready_processes); 
        enqueue(&exec_process, current);
        kill(current, SIGCONT); //Processo Ativo
        kill(pidInterrupter, SIGCONT); //Interrupter Ativo
        
        printQueue(&ready_processes);
        printQueue(&exec_process);
        printQueue(&blocked_D1);
        printQueue(&blocked_D2);
        printQueue(&terminated_process);
        printf("\n");

        while (1) {
            if(isFull(&terminated_process))
                break;

            if(GLOBAL_HAS_SYSCALL >= 0)
            {
                while(1)
                {
                    if(GLOBAL_HAS_SYSCALL == 1)
                        break;
                }
                int pidsyscall = dequeue(&exec_process); //Tira o processo q fez a syscall da fila
                //kill(pidsyscall, SIGSTOP); //Para ele

                char Dx = sc[0];
                char Op = sc[1];
                printf("DEVICE %c / OP %c\n", Dx, Op);
                if (Dx == '1') { //Atribui a uma fila de blocked
                    enqueue(&blocked_D1, pidsyscall);
                } else if (Dx == '2') {
                    enqueue(&blocked_D2, pidsyscall);
                }

                if (!isEmpty(&ready_processes)) {
                    int next_process = dequeue(&ready_processes);; //Ativa o proximo da fila 
                    enqueue(&exec_process, next_process);
                    kill(next_process, SIGCONT); 
                }

                GLOBAL_HAS_SYSCALL = -1;
                GLOBAL_TIMEOUT = -1;
            }
          
            

            if (GLOBAL_DEVICE != -1) { //Tratamento interrupcao
                switch (GLOBAL_DEVICE) {
                    case 1: 
                        if (!isEmpty(&blocked_D1)) {
                            int released_process = dequeue(&blocked_D1);
                            int index = encontrarIndex(pidProcesses,N_PROCESSOS, released_process);
                            printf("Processo %d com index %d liberado do dispositivo 1\n", released_process, index);
                            if(pPCB[index].PC >= MAX) //Processo deve terminar
                            {
                                enqueue(&terminated_process, released_process);
                                kill(released_process, SIGCONT);
                            }
                            else
                                enqueue(&ready_processes, released_process);                           
                        }
                        break;
                    case 2: 
                        if (!isEmpty(&blocked_D2)) {
                            int released_process = dequeue(&blocked_D2);
                            int index = encontrarIndex(pidProcesses,N_PROCESSOS, released_process);
                            if(pPCB[index].PC >= MAX) //Processo deve terminar
                            {
                                enqueue(&terminated_process, released_process);
                                kill(released_process, SIGCONT);
                            }
                            else
                                enqueue(&ready_processes, released_process);
                        }
                        break;    
                }
                GLOBAL_DEVICE = -1;
                GLOBAL_TIMEOUT = -1;
            }

            if(!isEmpty(&exec_process))
            {
                int index = encontrarIndex(pidProcesses,N_PROCESSOS, peek(&exec_process));
                if(pPCB[index].PC >= MAX && GLOBAL_HAS_SYSCALL == -1) //Terminated se PC > MAX
                {
                    int terminated = dequeue(&exec_process);
                    printf("dequeue terminater %d\n",terminated);
                    enqueue(&terminated_process, terminated);
                    printQueue(&terminated_process);
                    printQueue(&exec_process);
                    if(!isEmpty(&ready_processes))
                    {
                        int next = dequeue(&ready_processes);
                        printf("dequeue next %d\n",next);
                        enqueue(&exec_process, next);
                    }
                    GLOBAL_TERMINATED = -1;
                    GLOBAL_TIMEOUT = -1;
                }
            }

            if(GLOBAL_TIMEOUT != -1 && GLOBAL_HAS_SYSCALL == -1 && !isEmpty(&exec_process)) //Tira por Timeout
            {
                if (!isEmpty(&exec_process)) {
                    kill(peek(&exec_process), SIGSTOP); 
                    int current_process = dequeue(&exec_process);
                    enqueue(&ready_processes, current_process);
                    int next_process = peek(&ready_processes);
                    if (next_process != -1) {
                        dequeue(&ready_processes);
                        enqueue(&exec_process, next_process);
                        kill(next_process, SIGCONT); 
                    }
                }
                GLOBAL_TIMEOUT = -1;
            }

            if(isEmpty(&exec_process) && !isEmpty(&ready_processes))
            {
                int next_process = peek(&ready_processes);
                    if (next_process != -1) {
                        dequeue(&ready_processes);
                        enqueue(&exec_process, next_process);
                        kill(next_process, SIGCONT); 
                    }
            }

            printf("-------------------- COMECO -----------------------\n");
            printQueue(&ready_processes);
            printf("\n");
            printQueue(&exec_process);
            printf("\n");
            printQueue(&blocked_D1);
            printQueue(&blocked_D2);
            printQueue(&terminated_process);
            printf("\n");
            printf("--------------------- FIM ----------------------\n");
            sleep(1); // Pausa no loop para evitar consumo excessivo de CPU
        }
    }

    for (int i = 0; i < N_PROCESSOS; i++) {
        wait(NULL);
    }

    kill(pidInterrupter, SIGKILL);
    shmctl(systemcall,IPC_RMID, 0);
    shmctl(ProcessControlBlock,IPC_RMID, 0);
    printf("CAbO PORRA\n");
    return 0;
}

// Funções de controle de interrupção e syscalls
void SignalHandler(int sinal) {
    switch (sinal) {
        case SIGUSR1:
            printf("Device 1 liberado\n");
            GLOBAL_DEVICE = 1;
            break;
        case SIGUSR2: 
            printf("Device 2 liberado\n");
            GLOBAL_DEVICE = 2;
            break;
        case SIGTERM: 
            printf("Timeout\n");
            GLOBAL_TIMEOUT = 0;
            break;
        case SIGTSTP:
            printf("Syscall\n");
            GLOBAL_HAS_SYSCALL += 1;
            break;
        case SIGIO:
            printf("Terminated\n");
            GLOBAL_TERMINATED = 1;
            break;
    }
}

void processo(char* shm, PCB* pcb, int id) {
    int PC = 0;
    int d;
    char Dx;
    char Op;
    int f;

    pthread_mutex_lock(&mutex);
    pcb[id].PC = PC; 
    pcb[id].qttD1 = 0;
    pcb[id].qttD2 = 0;  
    pthread_mutex_unlock(&mutex);
    raise(SIGSTOP);
    srand(getpid());

    while (PC < MAX) {
        usleep(500000); //Sleep 500ms
        pthread_mutex_lock(&mutex);
        printf("pc do processo %d mutex ante dos ++ --> %d \n",getpid(),pcb[id].PC);
        pcb[id].PC += 1; 
        printf("pc do processo %d mutex depois dos ++ --> %d \n",getpid(),pcb[id].PC);
        pthread_mutex_unlock(&mutex);
        fflush(stdout);
        d = rand();
        f  = (d % 100) + 1;
        printf("d --> %d  f --> %d\n",d,f);
        if (f < 5) { 
            kill(getppid(), SIGTSTP);
            pthread_mutex_lock(&mutex);
            printf("SYSCALL PROCESSO %d\n", getpid());
            if (d % 2) 
                Dx = '1';
            else 
                Dx = '2';

            
            shm[0] = Dx;

            if (d % 3 == 1)
            {
                Op = 'R';
            } 
            else if (d % 3 == 1)
            {
                Op = 'W';
            } 
            else
            {
                Op = 'X';
            } 
            shm[1] = Op;
            printf("IF dx %c op %c\n", Dx, Op);
            pthread_mutex_unlock(&mutex);
            kill(getppid(), SIGTSTP);
            raise(SIGSTOP);
        }
        PC++;
        usleep(500000); //Sleep 500ms
    }

    printf("Processo %d terminated com PC %d\n", getpid(), pcb[id].PC);

    shmdt(pcb);
    shmdt(shm);
    kill(getppid(), SIGIO);
}

void InterruptController() {
    raise(SIGSTOP);
    int parent_id = getppid();
    srand(time(NULL));   
    printf("ENTRE INTERRUPTER\n");
    while (1) {
        double random_num = (((double) rand()) / RAND_MAX);
        
        if ( random_num <= 0.3) {
            kill(parent_id, SIGUSR1);
        }
        else if (random_num <= 0.6) {
            kill(parent_id, SIGUSR2);
        }
        usleep(1000000); //sleep 1 sec
        if(kill(parent_id, SIGTERM) == 0)
            printf("\nSINAL do interrupter ENVIADO\n");
        else{
            printf("\nSINAL do interrupter nao foi ENVIADO\n");
            exit(0);
        }
    }
}

// Funções relacionadas à fila
void initQueue(Queue* q, char* nome) {
    q->nome = nome;
    q->primeiro = -1;
    q->ultimo = -1;
    printf("\n_____ Na Init ______\n\n");
    printQueue(q);
}

void printQueue(Queue* q) {
    if (isEmpty(q)) {
        printf("A fila %s está vazia.\n", q->nome);
        return;
    }

    printf("Fila %s: ", q->nome);
    int i = q->primeiro;
    while (i != q->ultimo) {
        printf("%d ", q->items[i]);
        i = (i + 1) % N_PROCESSOS;  // Avança de forma circular
    }
    printf("%d\n", q->items[q->ultimo]);  // Imprime o último elemento
}


int isFull( Queue* q) {
    return (q->ultimo + 1) % N_PROCESSOS == q->primeiro;
}


int isEmpty( Queue* q) {
    return q->primeiro == -1;
}

void enqueue( Queue* q, int value) {
    if (isFull(q)) {
        printf("-----------------Fila cheia!------------------------\n");
    } else {
        if (isEmpty(q)) {
            q->primeiro = 0;  // Define o índice inicial
        }
        q->ultimo = (q->ultimo + 1) % N_PROCESSOS;  // Incrementa de forma circular
        q->items[q->ultimo] = value;
        printf("Inserido %d na fila %s\n", value, q->nome);
    }
}


int dequeue( Queue* q) {
    int item;
    if (isEmpty(q)) {
        printf("Fila vazia!\n");
        return -1;
    } else {
        item = q->items[q->primeiro];
        q->primeiro = (q->primeiro + 1) % N_PROCESSOS;  // Avança de forma circular
        if (q->primeiro == (q->ultimo + 1) % N_PROCESSOS) {
            // Se a fila estiver vazia após a remoção
            q->primeiro = q->ultimo = -1;
        }
        printf("\nRemovido %d da fila %s\n\n", item, q->nome);
        return item;
    }
}


int peek( Queue* q) {
    if (isEmpty(q)) {
        return -1;
    }
    return q->items[q->primeiro];
}

int encontrarIndex(int vetor[], int tamanho, int valor) {
    for (int i = 0; i < tamanho; i++) {
        if (vetor[i] == valor) {
            return i;  // Retorna o índice do valor encontrado
        }
    }
    return -1;  // Retorna -1 se o valor não for encontrado
}
