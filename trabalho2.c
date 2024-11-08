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
int GLOBAL_P2_NEW_PAGE = -1;
int GLOBAL_P3_NEW_PAGE = -1;
int GLOBAL_P4_NEW_PAGE = -1;



typedef struct {
    int presente;
    int modificado;
    int endereco_virtual;
} Pagina;

typedef struct {
    Pagina paginas[NUM_FRAMES_TABEL];  
} TabelaPaginacao;

typedef struct {
    Pagina* paginas[NUM_FRAMES_RAM];  
} RAM;



void inicializar_tabela(TabelaPaginacao *tabela);
void inicializar_ram(RAM *ram);
void call_substitution_algorithm(char *n);
void TodosProcessos(int *tabela_paginacao, int num_rodadas);
void SignalHandler(int n);
int isPageFault(RAM *vetor, Pagina *elemento);


int main(int argc, char *argv[]){
    int ProcessPid;
    if (argc < 3) {  // Verifica se foi passado pelo menos um argumento além do nome do programa
        printf("Erro: Passe Qual Algoritimo Usar (NRU/ 2nCH/ LRU/ WS) e o Numero de rodadas.\n");
        return 1;
    }

    //char* Algoritimo_cmd = (argv[1]); 
    int num_rodadas_cmd = atoi((argv[2])); //converte pra numero
    int shared_mem =  shmget (IPC_PRIVATE, 3*sizeof(int), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR); //Shm 
    int *nova_pagina = (int*)shmat(shared_mem,0,0); //Conecta o pai com shm
    ProcessPid = fork();
    if(ProcessPid == 0){ //Todos Processos P1 - P4
        TodosProcessos(nova_pagina,num_rodadas_cmd);
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
            if(GLOBAL_NEW_PAGE == 1)
            {
                int index = nova_pagina[0];
                int operacao = nova_pagina[1];
                int currentProcess = nova_pagina[2];
                Pagina* currentPage = &tabelas[currentProcess].paginas[index];
                int pagefault = isPageFault(&memoria_ram,currentPage);
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
                    printf("Coloca na RAM\n");
                }     
                kill(ProcessPid, SIGCONT);
            }
        }
    }

    return 0;
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
            GLOBAL_P2_NEW_PAGE = 1;
            break;
        case SIGTERM: 
            GLOBAL_P3_NEW_PAGE = 1;
            break;
        case SIGTSTP:
            GLOBAL_P4_NEW_PAGE = 1;
            break;
    }
}


void TodosProcessos(int *tabela_paginacao, int num_rodadas){
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

                // Escreve na memória compartilhada
                tabela_paginacao[0] = numero;
                tabela_paginacao[1] = (operacao == 'R') ? 0 : 1;
                tabela_paginacao[2] = i;
                kill(getppid(), SIGUSR1);
                raise(SIGSTOP);
                
                // Exemplo de saída para visualização (pode ser removido)
                printf("Arquivo %d: Numero = %d, Operacao = %c\n", i + 1, numero, operacao);
                printf("Escrito na SHM: [%d, %d]\n", tabela_paginacao[0], tabela_paginacao[1]);
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
        tabela->paginas[i].presente = 0;
        tabela->paginas[i].modificado = 0;
        tabela->paginas[i].endereco_virtual = -1;
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




/*
void TratamentoNovaPagina(int * pagina, RAM* ram)
{
    if()
}
*/

