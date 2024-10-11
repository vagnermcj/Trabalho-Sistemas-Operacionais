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

#define N_PROCESSOS 5
#define MAX 20

//Inicialização das FLAGS
int GLOBAL_DEVICE = -1;
int GLOBAL_TIMEOUT = -1;
int GLOBAL_HAS_SYSCALL = -1;
int GLOBAL_TERMINATED = -1;
int GLOBAL_FINISHED_SYSCALL = -1;
int GLOBAL_STOP_SIMULATOR = -1;

// Estrutura da fila
struct queue {
    char* nome;
    int items[N_PROCESSOS];
    int primeiro, ultimo;
};typedef struct queue Queue;

//Estrutura de PCB
struct pcb {
    int PID;
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
char* concatena(char* mensagem, char d1, char op);
void printPCBs(PCB pcbs[], int tamanho);


int main(void) {
    //Filas
    Queue blocked_D1;
    Queue blocked_D2;
    Queue ready_processes;
    Queue exec_process;
    Queue terminated_process;

    //SHM
    int ProcessControlBlock;
    PCB *pPCB;
    int systemcall;
    char *sc;
    int pidInterrupter;
    int pidProcesses[N_PROCESSOS]; 

    //Inicializa Filas
    initQueue(&blocked_D1, "de Bloqueado_1");
    initQueue(&blocked_D2, "de Bloqueado_2");
    initQueue(&ready_processes, "de Pronto");
    initQueue(&exec_process, "de Ativo");
    initQueue(&terminated_process, "de Terminado");
    printf("-------------------------------------------\n");
    systemcall = shmget (IPC_PRIVATE, 2*sizeof(char), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR); //Shm para as informacoes de Device e Operation
    ProcessControlBlock = shmget (IPC_PRIVATE, N_PROCESSOS*sizeof(PCB), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR); //SHM para a PCB

    sc = (char*)shmat(systemcall,0,0); //Conecta o pai com shm
    pPCB = (PCB*) shmat (ProcessControlBlock, 0, 0);
     
    pidInterrupter = fork();
    if (pidInterrupter == 0) { //Interrupter
        InterruptController();  
        exit(0);
    } 
    else //KernelSim 
    { 
        if(pidInterrupter != 0) //Atribui o signal handler ao Kernel 
        {
            signal(SIGUSR1, SignalHandler);  
            signal(SIGUSR2, SignalHandler);  
            signal(SIGTERM, SignalHandler);  
            signal(SIGTSTP, SignalHandler); 
            signal(SIGIO, SignalHandler);
            signal(SIGPWR , SignalHandler);    
            signal(SIGQUIT , SignalHandler);
        }
        for (int i = 0; i < N_PROCESSOS; i++) {
            pidProcesses[i] = fork();
            if (pidProcesses[i] == 0) //Filho
            {         
                processo(sc, pPCB, i);  // Executa a função do processo
                exit(0);  // Saída do processo filho
            }
        }

        for(int i =0; i<N_PROCESSOS;i++) //Filhos criados, agora coloca na fila
        {
            enqueue(&ready_processes, pidProcesses[i]);
            printf("\n");
        }

        //Prepara pra começar o KernelSimulator
        int current = dequeue(&ready_processes); 
        enqueue(&exec_process, current);
        printf("\n");
        kill(current, SIGCONT); //Processo Ativo
        kill(pidInterrupter, SIGCONT); //Interrupter Ativo
        

        
        printf("-------------------- INICIALIZA (PID: %d ) -----------------------\n", getpid());
        printQueue(&ready_processes);
        printf("\n");
        printQueue(&exec_process);
        printf("\n");
        printQueue(&blocked_D1);
        printf("\n");
        printQueue(&blocked_D2);
        printf("\n");
        printQueue(&terminated_process);
        printf("\n");

        while (1) {
            if(isFull(&terminated_process)) //Se todos processos terminarem, break
                break;
            
            if(GLOBAL_STOP_SIMULATOR == 1) //Usuario deu sinal para parada
            {
                //Para todos os processos
                kill(pidInterrupter, SIGSTOP);             
                for (int i = 0; i < N_PROCESSOS; i++)
                {
                    kill(pidProcesses[i], SIGSTOP);
                    
                }
                printPCBs(pPCB, N_PROCESSOS);
                GLOBAL_STOP_SIMULATOR = 2;
                while(GLOBAL_STOP_SIMULATOR == 2) //Pseudo SIGSTOP
                {}
                sleep(1);
                //Recomeça todos os processos 
                GLOBAL_STOP_SIMULATOR = -1;
                kill(pidInterrupter, SIGCONT);
                kill(peek(&exec_process), SIGCONT);             
            }
            

            if(GLOBAL_HAS_SYSCALL == 1) //Processo inciou uma syscall
            {
                while(GLOBAL_FINISHED_SYSCALL != 1) // Espera o processo terminar de solicitar
                {
                }

                int pidsyscall = dequeue(&exec_process); //Tira o processo q fez a syscall da fila
                //Escreve os devices na SHM
                char Dx = sc[0];
                char Op = sc[1];
                
                //Muda o estado do processo para bloqueado
                int index = encontrarIndex(pidProcesses,N_PROCESSOS, pidsyscall);
                char* mensagem = concatena("Estado: Bloqueado", Dx, Op); 
                strcpy(pPCB[index].state, mensagem);
                free(mensagem);
                if (Dx == '1') { //Atribui a uma fila de blocked
                    enqueue(&blocked_D1, pidsyscall);
                } else if (Dx == '2') {
                    enqueue(&blocked_D2, pidsyscall);
                }

                if (!isEmpty(&ready_processes)) { //Verifica se tem proximo na fila de prontos
                    int next_process = dequeue(&ready_processes);; //Ativa o proximo da fila 
                    int index = encontrarIndex(pidProcesses, N_PROCESSOS, next_process);
                    strcpy(pPCB[index].state, "Estado: Ativo"); //Muda o estado para ativo 
                    enqueue(&exec_process, next_process);
                    kill(next_process, SIGCONT); 
                }

                //Reinicia as FLAGS
                GLOBAL_FINISHED_SYSCALL = -1;
                GLOBAL_HAS_SYSCALL = -1;
                GLOBAL_TIMEOUT = -1;
            }
          
            

            if (GLOBAL_DEVICE != -1) { //Tratamento interrupcao IRQ1 e IRQ2
                switch (GLOBAL_DEVICE) {
                    case 1: 
                        if (!isEmpty(&blocked_D1)) { //Device 1
                            int released_process = dequeue(&blocked_D1); //Tira ele da fila
                            int index = encontrarIndex(pidProcesses,N_PROCESSOS, released_process);
                            if(pPCB[index].PC >= MAX) //Processo deve terminar
                            {
                                strcpy(pPCB[index].state, "Estado: Terminado");
                                enqueue(&terminated_process, released_process);
                                kill(released_process, SIGCONT);
                            }
                            else //Processo volta pra ready
                            {
                                strcpy(pPCB[index].state, "Estado: Pronto");
                                enqueue(&ready_processes, released_process);                           
                            }
                        }
                        break;
                    case 2: 
                        if (!isEmpty(&blocked_D2)) { //Device 2
                            int released_process = dequeue(&blocked_D2);
                            int index = encontrarIndex(pidProcesses,N_PROCESSOS, released_process);
                            if(pPCB[index].PC >= MAX) //Processo deve terminar
                            {
                                strcpy(pPCB[index].state, "Estado: Terminado");
                                enqueue(&terminated_process, released_process);
                                kill(released_process, SIGCONT);
                            }
                            else //Processo volta pra ready
                            {
                                strcpy(pPCB[index].state, "Estado: Pronto");
                                enqueue(&ready_processes, released_process);
                            }
                        }
                        break;    
                }

                //Reinicia FLAGS
                GLOBAL_DEVICE = -1;
                GLOBAL_TIMEOUT = -1;
            }

            if(!isEmpty(&exec_process)) //Se tem alguem na fila de ativos
            {
                int index = encontrarIndex(pidProcesses,N_PROCESSOS, peek(&exec_process));
                if(pPCB[index].PC >= MAX && GLOBAL_HAS_SYSCALL == -1) //Termina o programa se PC > MAX
                {
                    int terminated = dequeue(&exec_process); //Tira da fila de ativos
                    int index = encontrarIndex(pidProcesses, N_PROCESSOS, terminated);
                    strcpy(pPCB[index].state, "Estado: Terminado"); //Muda seu Estado
                    enqueue(&terminated_process, terminated);
                    printQueue(&terminated_process);
                    printQueue(&exec_process);
                    if(!isEmpty(&ready_processes)) // Se tem processo em ready, passa pra ativo
                    {
                        int next = dequeue(&ready_processes); 
                        int index = encontrarIndex(pidProcesses, N_PROCESSOS, next);
                        strcpy(pPCB[index].state, "Estado: Ativo");
                        enqueue(&exec_process, next);
                    }

                    //Reinicia as FLAGS
                    GLOBAL_TERMINATED = -1;
                    GLOBAL_TIMEOUT = -1;
                }
            }
            if(GLOBAL_TIMEOUT != -1 && GLOBAL_HAS_SYSCALL == -1 && !isEmpty(&exec_process)) //Tira o programa de ativos quando houver timeout
            {
                if (!isEmpty(&exec_process)) { 
                    kill(peek(&exec_process), SIGSTOP); //Tira o processo de ativos 
                    int current_process = dequeue(&exec_process);
                    int index = encontrarIndex(pidProcesses, N_PROCESSOS, current_process);
                    strcpy(pPCB[index].state, "Estado: Pronto"); // Muda seu estado para pronto
                    enqueue(&ready_processes, current_process);
                    int next_process = peek(&ready_processes);
                    if (next_process != -1) { //Passa o proximo da fila prontos para ativo
                        dequeue(&ready_processes);
                        int index = encontrarIndex(pidProcesses, N_PROCESSOS, next_process);
                        strcpy(pPCB[index].state, "Estado: Ativo");
                        enqueue(&exec_process, next_process);
                        kill(next_process, SIGCONT); 
                    }
                }

                //Reinicia FLAG
                GLOBAL_TIMEOUT = -1;
            }

            if(isEmpty(&exec_process) && !isEmpty(&ready_processes)) //Se não tem processos ativos mas algum esta em pronto
            {
                int next_process = peek(&ready_processes); //Tira de Pronto e passa pra ativo
                    if (next_process != -1) {
                        dequeue(&ready_processes);
                        int index = encontrarIndex(pidProcesses, N_PROCESSOS, next_process);
                        strcpy(pPCB[index].state, "Estado: Ativo");
                        enqueue(&exec_process, next_process);
                        kill(next_process, SIGCONT); 
                    }
            }

            printf("-------------------- COMECO (PID: %d ) -----------------------\n", getpid());
            printQueue(&ready_processes);
            printf("\n");
            printQueue(&exec_process);
            printf("\n");
            printQueue(&blocked_D1);
            printf("\n");
            printQueue(&blocked_D2);
            printf("\n");
            printQueue(&terminated_process);
            printf("\n");
            printf("--------------------- FIM ----------------------\n");
            sleep(1); // Pausa no loop para evitar consumo excessivo de CPU
        }
    }

    for (int i = 0; i < N_PROCESSOS; i++) { //Espera os filhos terminar
        wait(NULL);
    }

    kill(pidInterrupter, SIGKILL); //Termina o Interrupter
     printf("\n\n\n-----------------------------------------------\n\n\n");
    printf("Finalizando kernel Sim......\n\n");
    printPCBs(pPCB, N_PROCESSOS);
    shmctl(systemcall,IPC_RMID, 0);
    shmctl(ProcessControlBlock,IPC_RMID, 0);
    printf("\n\n\n-----------------------------------------------\n\n\n");
    return 0;
}

// Função de Controle dos Sinais
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
            printf("Processo Finalizou Syscall\n");
            GLOBAL_FINISHED_SYSCALL = 1;
            break;
        case SIGIO:
            printf("Processo terminou\n");
            GLOBAL_TERMINATED = 1;
            break;
        case SIGPWR:
            printf("Processo Iniciou Syscall\n");
            GLOBAL_HAS_SYSCALL = 1;
            break;
        case SIGQUIT:
            GLOBAL_STOP_SIMULATOR = 1;
            break;
    }
}

void processo(char* shm, PCB* pcb, int id) {
    int PC = 0;
    int d;
    char Dx;
    char Op;
    int f;

    //Inicializa a PCB
    pcb[id].PID = getpid(); 
    pcb[id].PC = PC; 
    strcpy(pcb[id].state, "Estado: Pronto");
    pcb[id].qttD1 = 0;
    pcb[id].qttD2 = 0;  
   
    raise(SIGSTOP); //Espera permissão do kernel para poder começar
    srand(getpid());

    while (PC < MAX) {
        usleep(500000); //Sleep 500ms
       
        pcb[id].PC += 1; //Incrementa PC na PCB
       
        fflush(stdout);
        d = rand();
        f  = (d % 100) + 1;
        if (f < 15) { //Condição da SYSCALL
            kill(getppid(), SIGPWR); // Informa ao kernel que começou a preparar uma SYSCALL
            if (d % 2) {
                Dx = '1';
                pcb[id].qttD1 += 1;
            }
            else {
                Dx = '2';
                pcb[id].qttD2 += 1;
            }
            
            if (d % 5 == 1)
                Op = 'R';
            else if (d % 3 == 1)
                Op = 'W';
            else
                Op = 'X';

            //Passa o Device e a Operação para SHM
            shm[0] = Dx;
            shm[1] = Op;       
            kill(getppid(), SIGTSTP); //Informa o Kernel que terminou a syscall
            raise(SIGSTOP);
        }
        PC++;
        usleep(500000); //Sleep 500ms
    }

    printf("Processo %d terminou com PC %d\n", getpid(), pcb[id].PC);

    //Dettach das SHM
    shmdt(pcb);
    shmdt(shm);
    kill(getppid(), SIGIO); //Informa ao kernel que o processo terminou
}

void InterruptController() {
    raise(SIGSTOP); //Espera permissão do kernel para executar
    int parent_id = getppid();
    srand(time(NULL));   
    while (1) {
        double random_num = (((double) rand()) / RAND_MAX);
        
        if ( random_num <= 0.3) {
            kill(parent_id, SIGUSR1); //Libera Device 1 IRQ1
        }
        else if (random_num <= 0.6) {
            kill(parent_id, SIGUSR2); //Libera device 2 IRQ2
        }
        usleep(1000000); //sleep 1 sec
        if(kill(parent_id, SIGTERM) != 0) //Libera sinal IRQO
        {
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
    printQueue(q);
    printf("\n");
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

int encontrarIndex(int vetor[], int tamanho, int valor) { //Encontra o index do PID passado em valor na PCB[]
    for (int i = 0; i < tamanho; i++) {
        if (vetor[i] == valor) {
            return i;  // Retorna o índice do valor encontrado
        }
    }
    return -1;  // Retorna -1 se o valor não for encontrado
}

char* concatena(char* mensagem, char d1, char op) {
    // Calcula o tamanho necessário para a nova mensagem
    // "Bloqueado " + 1 char para d1 + " Op " + 1 char para op + null terminator
    int tamanho = strlen(mensagem) + 10; // Mensagem, espaço para d1, " Op ", op e '\0'
    
    // Aloca memória para a nova string
    char* resultado = (char*)malloc(tamanho * sizeof(char));
    
    if (resultado == NULL) {
        perror("Erro ao alocar memória");
        exit(1);
    }

    // Concatena a mensagem com d1 e op
    sprintf(resultado, "Estado: %s D%c Operacao %c\n", mensagem, d1, op);

    return resultado;
}

void printPCBs(PCB pcbs[], int tamanho) { //Exibe o que está na PCB
    for (int i = 0; i < tamanho; i++) {
        printf("Processo %d: %d\n", i + 1, pcbs[i].PID);
        printf("  Program Counter (PC): %d\n", pcbs[i].PC);
        printf("  %s\n", pcbs[i].state);
        printf("  Quantidade de D1: %d\n", pcbs[i].qttD1);
        printf("  Quantidade de D2: %d\n", pcbs[i].qttD2);
        printf("-------------------------\n");  // Separação visual entre processos
    }
}
