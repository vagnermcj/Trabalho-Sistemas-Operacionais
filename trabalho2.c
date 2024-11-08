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


int GLOBAL_P1_NEW_PAGE = -1;
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
    Pagina paginas[NUM_FRAMES_RAM];  
} RAM;



void inicializar_tabela(TabelaPaginacao *tabela);
void inicializar_ram(RAM *ram);
void call_substitution_algorithm(char *n);
void TodosProcessos(int *tabela_paginacao, int num_rodadas);
void SignalHandler(int n);


int main(int argc, char *argv[]){

    if (argc < 3) {  // Verifica se foi passado pelo menos um argumento além do nome do programa
        printf("Erro: Passe Qual Algoritimo Usar (NRU/ 2nCH/ LRU/ WS) e o Numero de rodadas.\n");
        return 1;
    }

    char* Algoritimo_cmd = (argv[1]); 
    int num_rodadas_cmd = atoi((argv[2])); //converte pra numero

    int shared_mem =  shmget (IPC_PRIVATE, 2*sizeof(int), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR); //Shm 
    int *nova_pagina = (int*)shmat(shared_mem,0,0); //Conecta o pai com shm

    if(fork() == 0){ //Todos Processos P1 - P4
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

        printf("GLOBAL %d\n", GLOBAL_P1_NEW_PAGE);
        while(1)
        {
            if(GLOBAL_P1_NEW_PAGE == 1)
            {
                printf("NOVA PAGINA 1 %d %d\n", nova_pagina[0], nova_pagina[1]);
                GLOBAL_P1_NEW_PAGE = -1;
            }
            else if(GLOBAL_P2_NEW_PAGE == 1)
            {
                printf("NOVA PAGINA 2 %d %d\n", nova_pagina[0], nova_pagina[1]);
                GLOBAL_P2_NEW_PAGE = -1;
            }
            else if(GLOBAL_P3_NEW_PAGE == 1)
            {
                printf("NOVA PAGINA 3 %d %d\n", nova_pagina[0], nova_pagina[1]);
                GLOBAL_P3_NEW_PAGE = -1;
            }
            else if(GLOBAL_P4_NEW_PAGE == 1)
            {
                printf("NOVA PAGINA 4 %d %d\n", nova_pagina[0], nova_pagina[1]);
                GLOBAL_P4_NEW_PAGE = -1;
            }
        }
    }

    return 0;
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
        ram->paginas[i].presente = 0;          // Define como "não presente"
        ram->paginas[i].modificado = 0;        // Define como "não modificado"
        ram->paginas[i].endereco_virtual = -1; // Define um endereço virtual inválido
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


void TodosProcessos(int *tabela_paginacao, int num_rodadas){
    const char *nomes_arquivos[] = {"acessos_P1.txt", "acessos_P2.txt", "acessos_P3.txt", "acessos_P4.txt"};
    char linha[100];
    int numero;
    char operacao;
    FILE *arquivos[4];

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
                switch (i)
                {
                    case 0:
                        kill(getppid(), SIGUSR1);
                        break;
                    case 1:
                        kill(getppid(), SIGUSR2);
                        break;
                    case 2:
                        kill(getppid(), SIGTERM);
                        break;
                    case 3:
                        kill(getppid(), SIGTSTP);
                        break;
                    default:
                        break;
                }

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

/*
void TratamentoNovaPagina(int * pagina, RAM* ram)
{
    if()
}
*/

void SignalHandler(int n)
{
    switch (n)
    {
        case SIGUSR1:
            GLOBAL_P1_NEW_PAGE = 1;
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
