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
//Revisar includes

#define NUM_PROCESSES 4
#define NUM_FRAMES_RAM 16
#define NUM_FRAMES_TABEL 32


int GLOBAL_NEW_PAGE = -1;

typedef struct {
    int presente;
    int modificado;
    int endereco_virtual_na_ram;
    int myProcess;
} Pagina;

typedef struct {
    Pagina paginas[NUM_FRAMES_TABEL];  
} TabelaPaginacao;

typedef struct {
    Pagina* paginas[NUM_FRAMES_RAM];  
} RAM;


void print_ram(RAM *ram);
void inicializar_tabela(TabelaPaginacao *tabela);
void inicializar_ram(RAM *ram);
void call_substitution_algorithm(char *n);
void TodosProcessos(int *shm_P1,int *shm_P2,int *shm_P3,int *shm_P4,int num_rodadas);
void SignalHandler(int n);
int isPageFault(RAM *vetor, Pagina *elemento);


int main(int argc, char *argv[]){
    int ProcessPid;
    int num_rodadas_cmd; 
    int shm_P1;
    int shm_P2;
    int shm_P3;
    int shm_P4;
    int* nova_pagina_P1;
    int* nova_pagina_P2;
    int* nova_pagina_P3;
    int* nova_pagina_P4;
    if (argc < 3) {  // Verifica se foi passado pelo menos um argumento além do nome do programa
        printf("Erro: Passe Qual Algoritimo Usar (NRU/ 2nCH/ LRU/ WS) e o Numero de rodadas.\n");
        return 1;
    }

    //char* Algoritimo_cmd = (argv[1]); 
    num_rodadas_cmd = atoi((argv[2])); //converte pra numero
    shm_P1 =  shmget (IPC_PRIVATE, 2*sizeof(int), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);  
    shm_P2 =  shmget (IPC_PRIVATE, 2*sizeof(int), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR); 
    shm_P3 =  shmget (IPC_PRIVATE, 2*sizeof(int), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR); 
    shm_P4 =  shmget (IPC_PRIVATE, 2*sizeof(int), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR); 
    nova_pagina_P1 = (int*)shmat(shm_P1,0,0); 
    nova_pagina_P2 = (int*)shmat(shm_P2,0,0); 
    nova_pagina_P3 = (int*)shmat(shm_P3,0,0); 
    nova_pagina_P4 = (int*)shmat(shm_P4,0,0); 

    ProcessPid = fork();
    if(ProcessPid == 0){ //Todos Processos P1 - P4
        TodosProcessos(nova_pagina_P1,nova_pagina_P2,nova_pagina_P3,nova_pagina_P4,num_rodadas_cmd);
        exit(0);
    }
    else //GMV
    {
        signal(SIGUSR1, SignalHandler);  
        signal(SIGUSR2, SignalHandler);  
        signal(SIGTERM, SignalHandler);  
        signal(SIGTSTP, SignalHandler); 
        
        
        RAM memoria_ram;
        TabelaPaginacao tabelas[NUM_PROCESSES];  // Vetor com 4 tabelas de paginação

        inicializar_ram(&memoria_ram);

        for (int i = 0; i < NUM_PROCESSES; i++) {
            inicializar_tabela(&tabelas[i]);
        }

        while(1)
        {
            if(GLOBAL_NEW_PAGE != -1)
            {
                printf("Nova Pagina!\n");
                int index;
                int operacao;
                int currentProcess = GLOBAL_NEW_PAGE - 1; //Pega o index do processo pras tabelas de processos
                switch (GLOBAL_NEW_PAGE)
                {
                    case 1:
                        index = nova_pagina_P1[0];
                        operacao = nova_pagina_P1[1];
                        break;
                    case 2:
                        index = nova_pagina_P2[0];
                        operacao = nova_pagina_P2[1];
                        break;
                    case 3:
                        index = nova_pagina_P3[0];
                        operacao = nova_pagina_P3[1];
                        break;
                    case 4:
                        index = nova_pagina_P4[0];
                        operacao = nova_pagina_P4[1];
                        break;
                }
                printf("Dados da SHM: index %d | op %d | Process %d\n", index,operacao,currentProcess);

                
                Pagina* currentPage = (Pagina*)malloc(sizeof(Pagina));
                currentPage = &(tabelas[currentProcess].paginas[index]);
                printf("informacoes current page: %d %d\n",currentProcess, index);
                int pagefault = isPageFault(&memoria_ram,currentPage);
                printf("Pagefault index: %d\n", pagefault);
                if(pagefault == -2)
                {
                    printf("Faz nada\n");
                }
                else if(pagefault == -1)
                {
                    printf("Faz Algoritmo\n");
                }
                else
                {
                    memoria_ram.paginas[pagefault] = currentPage;
                    currentPage->endereco_virtual_na_ram = pagefault;
                    currentPage->presente = 1;
                    currentPage->myProcess = currentProcess;
                    print_ram (&memoria_ram);
                }   
                GLOBAL_NEW_PAGE = -1;
                kill(ProcessPid, SIGCONT);
            }
        }
    }

    return 0;
}

void print_ram(RAM *ram) {
    printf("\nEstado da Memória RAM:\n");
    printf("Posição | Presente | Modificado | Processo \n");
    printf("--------------------------------------------------------\n");
    for (int i = 0; i < NUM_FRAMES_RAM; i++) {
        if (ram->paginas[i] == NULL) {
            printf("%7d |   Vazia   |     ---     |        ---\n", i);
        } else {
            Pagina *pagina = ram->paginas[i];
            printf("%7d |     %d     |     %d       |        %d\n", 
                   i, 
                   pagina->presente, 
                   pagina->modificado, 
                   pagina->myProcess);
        }
    }
    printf("\n");
}



int isPageFault(RAM *vetor, Pagina *elemento) {
    for (int i = 0; i < NUM_FRAMES_RAM; i++) {
        if (vetor->paginas[i] == elemento) {
            return -2;
        }
        else if (vetor->paginas[i] == NULL) {
            return i;
        }
    }
    return -1;
}


void SignalHandler(int n)
{
    switch (n)
    {
        case SIGUSR1:
            GLOBAL_NEW_PAGE = 1;
            break;
        case SIGUSR2: 
            GLOBAL_NEW_PAGE = 2;
            break;
        case SIGTERM: 
            GLOBAL_NEW_PAGE = 3;
            break;
        case SIGTSTP:
            GLOBAL_NEW_PAGE = 4;
            break;
    }
}


void TodosProcessos(int *shm_P1,int *shm_P2,int *shm_P3,int *shm_P4,int num_rodadas){
    FILE *arquivos[4];
    const char *nomes_arquivos[] = {"acessos_P1.txt", "acessos_P2.txt", "acessos_P3.txt", "acessos_P4.txt"};
    char linha[100];
    int numero;
    char operacao;

    // Abrindo os 4 arquivos
    for (int i = 0; i < 4; i++) {
        arquivos[i] = fopen(nomes_arquivos[i], "r");
        if (arquivos[i] == NULL) {
            perror("Erro ao abrir o arquivo");
            exit(1);
        }
    }
    // Variável para contar as rodadas de leitura
    int cont = 0;

    // Leitura das linhas de cada arquivo em sequência
    while (cont < num_rodadas) {
        for (int i = 0; i < 4; i++) { // Itera pelos arquivos
            // Lê uma linha do arquivo atual
            if (fgets(linha, sizeof(linha), arquivos[i]) != NULL) {
                // Extrai o número e a operação (R ou W) da linha
                sscanf(linha, "%d %c", &numero, &operacao);

                switch (i)
                {
                    case 0:
                        printf("Case 0\n");
                        shm_P1[0] = numero;
                        shm_P1[1] = (operacao == 'R') ? 0 : 1;
                        kill(getppid(), SIGUSR1);
                        raise(SIGSTOP);
                        break;
                    case 1:
                        printf("Case 1\n");
                        shm_P2[0] = numero;
                        shm_P2[1] = (operacao == 'R') ? 0 : 1;
                        kill(getppid(), SIGUSR2);
                        raise(SIGSTOP); 
                        break;
                    case 2:
                        printf("Case 2\n");
                        shm_P3[0] = numero;
                        shm_P3[1] = (operacao == 'R') ? 0 : 1;
                        kill(getppid(), SIGTERM);
                        raise(SIGSTOP);
                        break;
                    case 4:
                        printf("Case 3\n");
                        shm_P4[0] = numero;
                        shm_P4[1] = (operacao == 'R') ? 0 : 1;
                        kill(getppid(), SIGTSTP);                 
                        raise(SIGSTOP);
                        break;
                    default:
                        break;
                }
            } else {
                // Volta ao início do arquivo se chegar ao final
                rewind(arquivos[i]);
            }
        }

        cont++; // Incrementa o contador de rodadas
        printf("Rodada %d completa\n", cont);
    }

    // Fecha os arquivos
    for (int i = 0; i < 4; i++) {
        fclose(arquivos[i]);
    }
}

void inicializar_tabela(TabelaPaginacao *tabela) {
    for (int i = 0; i < NUM_FRAMES_TABEL; i++) {
        tabela->paginas[i].presente = -1;
        tabela->paginas[i].modificado = -1;
        tabela->paginas[i].endereco_virtual_na_ram = -1;
    }
}

void inicializar_ram(RAM *ram) {
    for (int i = 0; i < NUM_FRAMES_RAM; i++) {
        ram->paginas[i] = NULL;
    }
}


//alterar ta so printando ainda
void call_substitution_algorithm(char* n){
    if (strcmp(n, "NRU") == 0) {
        printf("Not Recently Used (NRU)\n");
    } else if (strcmp(n, "2nCH") == 0) {
        printf("Segunda Chance\n");
    } else if (strcmp(n, "LRU") == 0) {
        printf("LRU/Aging\n");
    } else if (strcmp(n, "WS") == 0) {
        printf("Working Set(k)\n");
    } else {
        printf("nenhum\n");
    }

}


