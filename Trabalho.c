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
#define MS 5
#define MAX 10

int GLOBAL_IRQ = -1;
int GLOBAL_HAS_SYSCALL = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Estrutura da fila
struct Queue {
    int items[N_PROCESSOS];
    int primeiro, ultimo;
};
// Prototipos das funções
void SignalHandler(int sinal);
void Syscall(char Dx, char Op,char* shm);
void processo(char* shm);
void InterruptController();
void initQueue(struct Queue* q);
void printQueue(struct Queue* q);
int isFull(struct Queue* q);
int isEmpty(struct Queue* q);
void enqueue(struct Queue* q, int value);
int dequeue(struct Queue* q);
int peek(struct Queue* q);


int main(void) {
    struct Queue blocked_D1;
    struct Queue blocked_D2;
    struct Queue ready_processes;
    char systemcall, *sc;
    int pidInterrupter;
    int pidProcesses[N_PROCESSOS]; 
    int fd[2];      // Pipe para comunicação de dispositivo e operação
    int pipepid[2]; // Pipe para receber o pid dos processos

    initQueue(&blocked_D1);
    initQueue(&blocked_D2);
    initQueue(&ready_processes);

    systemcall = shmget (IPC_PRIVATE, 2*sizeof(char), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    


    if ((pipe(fd) < 0) || (pipe(pipepid) < 0)) {
        puts("Erro ao abrir os pipes\n");
        exit(-1);
    }

    pidInterrupter = fork();
    if (pidInterrupter == 0) { //Interrupter
        close(fd[0]);
        close(fd[1]);
        close(pipepid[0]);
        close(pipepid[1]);
        raise(SIGSTOP);
        InterruptController();  
        exit(0);
    } 
    else //KernelSim 
    { 
        if(pidInterrupter != 0)
        {
            signal(SIGUSR1, SignalHandler);  
            signal(SIGUSR2, SignalHandler);  
            signal(SIGTERM, SignalHandler);  
        }
        for (int i = 0; i < N_PROCESSOS; i++) {
            pidProcesses[i] = fork();
            if (pidProcesses[i] == 0) //Filho
            {  
                close(fd[0]);
                close(pipepid[0]);
                sc = (char*)shmat(systemcall,0,0); //Conecta os filhos com shm
                int pid = getpid();  // Pega o PID do processo filho
                //Mutex Lock
                pthread_mutex_lock(&mutex);
                write(pipepid[1], &pid, sizeof(int));
                pthread_mutex_unlock(&mutex);
                processo(sc);  // Executa a função do processo
                exit(0);  // Saída do processo filho
            }
        }


        close(fd[1]);        
        close(pipepid[1]);   

        sc = (char*)shmat(systemcall,0,0); //Conecta o pai com shm
        
        for(int i =0; i<N_PROCESSOS;i++) //Filhos criados, agora coloca na fila
        {
            pthread_mutex_unlock(&mutex);
            enqueue(&ready_processes, pidProcesses[i]);
            pthread_mutex_unlock(&mutex);
        }

        kill(ready_processes.items[ready_processes.primeiro], SIGCONT); //Primeiro da fila ativo
        kill(pidInterrupter, SIGCONT); //Interrupter Ativo

        while (1) {
            //system("clear");
            printf("Fila de processos prontos: ");
            printQueue(&ready_processes);
            printf("Fila de bloqueio do dispositivo 1: ");
            printQueue(&blocked_D1);
            printf("Fila de bloqueio do dispositivo 2: ");
            printQueue(&blocked_D2);
            if (GLOBAL_IRQ != -1) { //Tratamento interrupcao
                switch (GLOBAL_IRQ) {
                    case 0: 
                        if (!isEmpty(&ready_processes)) {
                            int current_process = dequeue(&ready_processes);
                            printf("Processo %d colocado no final da fila de prontos\n", current_process);
                            kill(current_process, SIGSTOP); 
                            enqueue(&ready_processes, current_process);
                            int next_process = peek(&ready_processes);
                            if (next_process != -1) {
                                kill(next_process, SIGCONT); 
                            }
                        }
                        break;
                    case 1: 
                        if (!isEmpty(&blocked_D1)) {
                            int released_process = dequeue(&blocked_D1);
                            printf("Processo %d liberado do dispositivo 1 e colocado na fila de prontos\n", released_process);
                            enqueue(&ready_processes, released_process);
                        }
                        break;
                    case 2: 
                        if (!isEmpty(&blocked_D2)) {
                            int released_process = dequeue(&blocked_D2);
                            printf("Processo %d liberado do dispositivo 2 e colocado na fila de ativos\n", released_process);
                            enqueue(&ready_processes, released_process);
                        }
                        break;    
                }
                GLOBAL_IRQ = -1;
            }

            if(GLOBAL_HAS_SYSCALL)
            {
                char Dx = sc[0];
                char Op = sc[1];
                printf("DEVICE %c / OP %c\n", Dx, Op);
                int pidsyscall = dequeue(&ready_processes); //Tira o processo q fez a syscall da fila
                kill(pidsyscall, SIGSTOP); //Para ele
                if (Dx == '1') { //Atribui a uma fila de blocked
                    enqueue(&blocked_D1, pidsyscall);
                } else if (Dx == '2') {
                    enqueue(&blocked_D2, pidsyscall);
                }
                int next_process = peek(&ready_processes); //Ativa o proximo da fila 
                if (next_process != -1) {
                    kill(next_process, SIGCONT); 
                }

                GLOBAL_HAS_SYSCALL = -1;
            }

            sleep(1); // Pausa no loop para evitar consumo excessivo de CPU
        }
    }

    for (int i = 0; i < N_PROCESSOS; i++) {
        wait(NULL);
    }

    return 0;
}

// Funções de controle de interrupção e syscalls
void SignalHandler(int sinal) {
    switch (sinal) {
        case SIGUSR1:
            printf("Device 1 liberado\n");
            GLOBAL_IRQ = 1;
            break;
        case SIGUSR2: 
            printf("Device 2 liberado\n");
            GLOBAL_IRQ = 2;
            break;
        case SIGTERM: 
            printf("Timeout\n");
            GLOBAL_IRQ = 0;
            break;
        case SIGIOT:
            printf("Syscall");
            GLOBAL_HAS_SYSCALL = 1;
    }
}

void Syscall(char Dx, char Op, char* shm) {
    shm[0] = Dx;
    shm[1] = Op;
}

void processo(char* shm) {
    int PC = 0;
    int d;
    char Dx;
    char Op;
    int f;
    while (PC < MAX) {
        d = rand() - 74;
        f  = (d % 100) + 1;
        sleep(1);
        if (f < 15) { 
            printf("SYSCALL!!!!!\n");
            if (d % 2) 
                Dx = '1';
            else 
                Dx = '2';

            if (d % 3 == 1) 
                Op = 'R';
            else if (d % 3 == 1) 
                Op = 'W';
            else 
                Op = 'X';
            Syscall(Dx, Op, shm);   
        }
        kill(getpid(), SIGTRAP);
        sleep(1);
        PC++;
    }
}

void InterruptController() {
    srand(time(NULL)); 
    int parent_id = getppid();

    while (1) {
        if (((double) rand() / RAND_MAX) <= 0.3) {
            kill(parent_id, SIGUSR1);
        }
        if (((double) rand() / RAND_MAX) <= 0.6) {
            kill(parent_id, SIGUSR2);
        }
        usleep(MS * 1000000);
        kill(parent_id, SIGTERM);
    }
}

// Funções relacionadas à fila
void initQueue(struct Queue* q) {
    q->primeiro = -1;
    q->ultimo = -1;
}

void printQueue(struct Queue* q) {
    if (q->primeiro == -1) {
        printf("A fila está vazia.\n");
        return;
    }

    printf("Fila: ");
    for (int i = q->primeiro; i <= q->ultimo; i++) {
        printf("%d ", q->items[i]);
    }
    printf("\n");
}

int isFull(struct Queue* q) {
    return q->ultimo == N_PROCESSOS - 1;
}

int isEmpty(struct Queue* q) {
    return q->primeiro == -1;
}

void enqueue(struct Queue* q, int value) {
    if (isFull(q)) {
        printf("-----------------Fila cheia!------------------------\n");
    } else {
        if (isEmpty(q)) {
            q->primeiro = 0; // Definir o primeiro quando o primeiro elemento for inserido
        }
        q->ultimo++;
        q->items[q->ultimo] = value;
        printf("Inserido %d na fila\n", value);
    }
}

int dequeue(struct Queue* q) {
    int item;
    if (isEmpty(q)) {
        printf("Fila vazia!\n");
        return -1;
    } else {
        item = q->items[q->primeiro];
        q->primeiro++;
        if (q->primeiro > q->ultimo) {
            // Reseta a fila após remover todos os elementos
            q->primeiro = q->ultimo = -1;
        }
        return item;
    }
}

int peek(struct Queue* q) {
    if (isEmpty(q)) {
        return -1;
    }
    return q->items[q->primeiro];
}
