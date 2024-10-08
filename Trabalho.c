#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>

#define N_PROCESSOS 3
#define MS 50000
#define MAX 10

int GLOBAL_IRQ = -1;

// Estrutura da fila
struct Queue {
    int items[N_PROCESSOS];
    int primeiro, ultimo;
};

// Inicializa a fila
void initQueue(struct Queue* q) {
    q->primeiro = -1;
    q->ultimo = -1;
}

// Mostra a fila
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

// Verifica se a fila está cheia
int isFull(struct Queue* q) {
    return q->ultimo == N_PROCESSOS - 1;
}

// Verifica se a fila está vazia
int isEmpty(struct Queue* q) {
    return q->primeiro == -1;
}

// Adiciona um elemento à fila
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

// Remove um elemento da fila
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

// Obtem o primeiro elemento da fila
int peek(struct Queue* q) {
    if (isEmpty(q)) {
        return -1;
    }
    return q->items[q->primeiro];
}

void SignalHandler2(int sinal) {
    switch (sinal) {

        case SIGUSR2: 
            printf("Teoria da Conspiração\n");
            break;

    }
}

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
    }
}

void Syscall(char Dx, char Op, int* fd, int * pipepid) {
    int pid = getpid();
    write(pipepid[1], &pid, sizeof(int));
    write(fd[1], &Dx, sizeof(char));
    write(fd[1], &Op, sizeof(char));
}

void processo(int* fd, int* pipepid) {
    int PC = 0;
    int d;
    char Dx;
    char Op;
    int f;
    while (PC < MAX) {
        d = rand() - 74;
        f  = (d % 100) + 1;
        sleep(0.5);
        if (f < 15) { 
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
        }
        Syscall(Dx, Op, fd, pipepid);
        sleep(0.5);
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
        usleep(MS * 100);
        kill(parent_id, SIGTERM);
    }
}

int main(void) {
    struct Queue blocked_D1;
    struct Queue blocked_D2;
    struct Queue ready_processes;
    int fd[2];      // Pipe para comunicação de dispositivo e operação
    int pipepid[2]; // Pipe para receber o pid dos processos

    initQueue(&blocked_D1);
    initQueue(&blocked_D2);
    initQueue(&ready_processes);

    if ((pipe(fd) < 0) || (pipe(pipepid) < 0)) {
        puts("Erro ao abrir os pipes\n");
        exit(-1);
    }

    if (fork() == 0) { //Interrupter
        close(fd[0]);
        close(fd[1]);
        close(pipepid[0]);
        close(pipepid[1]);
        InterruptController();  
        exit(0);
    } 
    else //KernelSim 
    { 
       for (int i = 0; i < N_PROCESSOS; i++) {
            int pid = fork();  // Criação do processo filho
            if (pid == 0) 
            {  // Filho
                signal(SIGUSR2, SignalHandler2);  
                close(fd[0]);
                close(pipepid[0]);
                pid = getpid();  // Pega o PID do processo filho
                printf("Filho criado com PID: %d\n", pid); 
                //Mutex Lock
                if(write(pipepid[1], &pid, sizeof(int)) == -1){
                    printf("DEu merda");
                };  // Envia o PID para o pai
                
                //Mutex unlock
                processo(fd, pipepid);  // Executa a função do processo
                exit(0);  // Saída do processo filho
            }
        }

        sleep(2);
        signal(SIGUSR1, SignalHandler);  
        signal(SIGUSR2, SignalHandler);  
        signal(SIGTERM, SignalHandler);  

        close(fd[1]);        
        close(pipepid[1]);   

        int callingpid;
        char Dx;
        char Op;
        
        for(int i =0; i<N_PROCESSOS;i++) //Filhos criados, agora coloca na fila
        {
            int pid_filho; 
            read(pipepid[0], &pid_filho, sizeof(int));
            enqueue(&ready_processes, pid_filho);
        }

        kill(ready_processes.items[ready_processes.primeiro], SIGCONT);

        while (1) {
            printf("Verificando interrupções e syscalls...\n");
            printf("Fila de processos prontos: ");
            printQueue(&ready_processes);
            printf("Fila de bloqueio do dispositivo 1: ");
            printQueue(&blocked_D1);
            printf("Fila de bloqueio do dispositivo 2: ");
            printQueue(&blocked_D2);
            printf("\n");
            printf("GLOBAL_IRQ: %d\n", GLOBAL_IRQ);
            sleep(2);
            if (GLOBAL_IRQ != -1) {
                switch (GLOBAL_IRQ) {
                    case 0: 
                        if (!isEmpty(&ready_processes)) {
                            int current_process = dequeue(&ready_processes);
                            printf("Processo %d colocado no final da fila\n", current_process);
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
                            printf("Processo %d liberado do dispositivo 1 e colocado na fila de ativos\n", released_process);
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

            if (read(pipepid[0], &callingpid, sizeof(int)) > 0) {
                printf("LEU O PIPE\n");
                read(fd[0], &Dx, sizeof(char));
                read(fd[0], &Op, sizeof(char));
                printf("DEVICE OP: %c %c", Dx, Op);

                if (Dx == '1') {
                    enqueue(&blocked_D1, callingpid);
                } else if (Dx == '2') {
                    enqueue(&blocked_D2, callingpid);
                }
                kill(callingpid, SIGSTOP);
            }

            sleep(1); // Pausa no loop para evitar consumo excessivo de CPU
        }
    }

    for (int i = 0; i < N_PROCESSOS; i++) {
        wait(NULL);
    }

    return 0;
}
