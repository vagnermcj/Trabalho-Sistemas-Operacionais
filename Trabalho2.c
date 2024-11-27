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

#define NUM_PROCESSES 4
#define NUM_FRAMES_RAM 16
#define NUM_FRAMES_TABEL 32


int GLOBAL_NEW_PAGE = -1;
int GLOBAL_END_SIMULATION = -1;

typedef struct {
    int valido;
    int referenciado;
    int modificado;
    int ramIndex;
    int processo;
    int tabelaIndex;
    unsigned int contador; // Campo para o Aging
} Pagina;


typedef struct {
    Pagina* paginas[NUM_FRAMES_TABEL];  
} TabelaPaginacao;

typedef struct {
    Pagina* paginas[NUM_FRAMES_RAM];  
} RAM;

void print_tabelas_paginacao(TabelaPaginacao tabelas[]);
void print_ram(RAM *ram);
void subs_NRU(RAM *ram, TabelaPaginacao *tabelas, int currentProcess, Pagina *novaPagina, int operacao);
void inicializar_tabela(TabelaPaginacao *tabela, int processo);
void inicializar_ram(RAM *ram);
void call_substitution_algorithm(char *n);
void TodosProcessos(int *shm_P1,int *shm_P2,int *shm_P3,int *shm_P4,int num_rodadas);
void SignalHandler(int n);
int isPageFault(RAM *vetor, Pagina *elemento);
void subs_LRU_Aging(RAM *ram, Pagina *novaPagina, int currentProcess, int operacao,TabelaPaginacao * tabelas);
void atualizar_contadores(RAM *ram);
void reset_referenciado(TabelaPaginacao *tabela);


int main(int argc, char *argv[]){
    int ProcessPid;
    int num_rodadas_cmd; 
    int total_pagefaults = 0;
    int shm_P1, shm_P2, shm_P3, shm_P4;
    int *nova_pagina_P1, *nova_pagina_P2,*nova_pagina_P3,*nova_pagina_P4;
    if (argc < 3) {  // Verifica se foi passado pelo menos um argumento além do nome do programa
        printf("Erro: Passe Qual Algoritimo Usar (NRU/ 2nCH/ LRU/ WS) e o Numero de rodadas.\n");
        return 1;
    }

    char* algoritmo_cmd = (argv[1]); 
    num_rodadas_cmd = atoi((argv[2])); //converte pra numero

    printf("-------------- ALGORITMO DE SUBSTITUICAO: %s --------------\n", algoritmo_cmd);
    printf("--------------    NUMERO DE RODADAS: %d      --------------\n", num_rodadas_cmd);


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
        signal(SIGPWR, SignalHandler); 
        
        
        RAM memoria_ram;
        TabelaPaginacao tabelas[NUM_PROCESSES];  // Vetor com 4 tabelas de paginação

        inicializar_ram(&memoria_ram);

        for (int i = 0; i < NUM_PROCESSES; i++) {
            inicializar_tabela(&tabelas[i], i+1);
        }

        while(1)
        {
            if(GLOBAL_NEW_PAGE != -1)
            {
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
                
                Pagina* currentPage = tabelas[currentProcess].paginas[index];
                printf("Pagina lida: Index %d Operacao %d Processo %d\n\n", index, operacao, currentProcess+1);
                int pagefault = isPageFault(&memoria_ram,currentPage);
                if(pagefault == -2) // A pagina ja esta na RAM
                {
                    if(strcmp(algoritmo_cmd,"NRU") == 0 || strcmp(algoritmo_cmd,"LRU"))
                    {
                        currentPage->modificado = operacao;
                        currentPage->referenciado = 1;
                    }
                }
                else if(pagefault == -1) // Pagina nao se encontra na RAM, tem q substituir
                {
                    total_pagefaults++;

                    if(strcmp(algoritmo_cmd,"NRU") == 0)
                    {
                        subs_NRU(&memoria_ram,tabelas,currentProcess,currentPage,operacao);
                    }
                    else if(strcmp(algoritmo_cmd, "LRU") == 0)
                    {
                        subs_LRU_Aging(&memoria_ram, currentPage, currentProcess, operacao,tabelas);
                    }
                    else if(strcmp(algoritmo_cmd, "2nCH") == 0)
                    {

                    }
                    else if(strcmp(algoritmo_cmd, "WS") == 0)
                    {

                    }
                }
                else //Existe espaco vazio na RAM para alocar a Pagina
                {
                    total_pagefaults++;

                    memoria_ram.paginas[pagefault] = currentPage;
                    currentPage->ramIndex = pagefault;
                    currentPage->valido = 1;
                    currentPage->processo = currentProcess;

                    if(strcmp(algoritmo_cmd,"NRU") == 0 || strcmp(algoritmo_cmd,"LRU"))
                    {
                        currentPage->modificado = operacao;
                        currentPage->referenciado = 1;
                    }
                }   
                GLOBAL_NEW_PAGE = -1;
                if(strcmp(algoritmo_cmd, "LRU") == 0) atualizar_contadores(&memoria_ram);
                kill(ProcessPid, SIGCONT);
            }
          
            if(GLOBAL_END_SIMULATION != -1)
                break;
            
        }

        print_tabelas_paginacao(tabelas);
        printf("================ PAGEFAULTS: %d ================\n", total_pagefaults);
        //free, shmdt e shmctl
    }

    return 0;
}

void atualizar_contadores(RAM *ram) {
    for (int i = 0; i < NUM_FRAMES_RAM; i++) {
        if (ram->paginas[i] != NULL) {
            // Desloca o contador para a direita
            ram->paginas[i]->contador >>= 1;

            // Se a página foi referenciada, define o bit mais significativo
            if (ram->paginas[i]->referenciado) {
                ram->paginas[i]->contador |= 0x80000000; // Define o MSB
            }

            // Reseta o bit de referência
            ram->paginas[i]->referenciado = 0;
        }
    }
}

void subs_NRU(RAM *ram, TabelaPaginacao *tabelas, int currentProcess, Pagina *novaPagina, int operacao) 
{
    Pagina *categorias[4][NUM_FRAMES_TABEL];
    int contadores[4] = {0, 0, 0, 0}; // Contadores para cada categoria

    // Percorre a tabela de paginação do processo atual
    for (int i = 0; i < NUM_FRAMES_TABEL; i++) {
        Pagina *pagina = tabelas[currentProcess].paginas[i];

        if (pagina != NULL && pagina->valido == 1) {
            // Classifica a página em uma das quatro categorias
            if (pagina->referenciado == 0 && pagina->modificado == 0) {
                categorias[0][contadores[0]++] = pagina; // Classe 0: Não referenciada, não modificada
            } else if (pagina->referenciado == 0 && pagina->modificado == 1) {
                categorias[1][contadores[1]++] = pagina; // Classe 1: Não referenciada, modificada
            } else if (pagina->referenciado == 1 && pagina->modificado == 0) {
                categorias[2][contadores[2]++] = pagina; // Classe 2: Referenciada, não modificada
            } else if (pagina->referenciado == 1 && pagina->modificado == 1) {
                categorias[3][contadores[3]++] = pagina; // Classe 3: Referenciada, modificada
            }
        }
    }

    Pagina *pagina_para_substituir = NULL;
    for (int classe = 0; classe < 4; classe++) {
        if (contadores[classe] > 0) {
            pagina_para_substituir = categorias[classe][0]; 
            break;
        }
    }
     reset_referenciado(&tabelas[currentProcess]); //Reinicia as tabelas referencered

    if (pagina_para_substituir != NULL) { //Substituicao
        int frame = pagina_para_substituir->ramIndex;

        // Se a página modificada está sendo substituída, escreve no disco (simulado)
        if (pagina_para_substituir->modificado == 1) {
            printf("Página suja substituída. Gravando no swap: Página %d do processo %d\n\n",
                   pagina_para_substituir->tabelaIndex, 
                   pagina_para_substituir->processo + 1);
        }

        // Atualiza a tabela de paginação da página substituída 
        pagina_para_substituir->valido = 0;
        pagina_para_substituir->ramIndex = -1;
        pagina_para_substituir->referenciado = 0;
        pagina_para_substituir->modificado = 0;

        // Atualiza a RAM com a nova página
        ram->paginas[frame] = novaPagina;
        novaPagina->valido = 1;
        novaPagina->ramIndex = frame;
        novaPagina->referenciado = 1;
        novaPagina->modificado = operacao;  
        novaPagina->processo = currentProcess;

        printf("Página substituída. Nova página %d alocada no quadro %d para o processo %d\n\n",
               novaPagina->tabelaIndex, frame, currentProcess+1);
    } else {
        printf("Erro: Não há páginas válidas para substituir no processo %d\n", currentProcess+1);
    }
}
void subs_LRU_Aging(RAM *ram, Pagina *novaPagina, int currentProcess, int operacao,TabelaPaginacao * tabelas) {
    int menor_valor = -1; 
    int frame_para_substituir = -1;

    Pagina *pagina_para_substituir = NULL;
    // Encontra a página com o menor contador
    for (int i = 0; i < NUM_FRAMES_TABEL; i++) {
        Pagina *pagina_atual = tabelas[currentProcess].paginas[i];
        if (pagina_atual->valido == 1) {
            if (menor_valor == -1 ||pagina_atual->contador < menor_valor) {
                menor_valor = pagina_atual->contador;
                frame_para_substituir = i;
                pagina_para_substituir = pagina_atual;
            }
        }
    }

    // Substituir a página encontrada
    if (frame_para_substituir != -1) {

        // Simula gravação em disco se a página foi modificada
        if (pagina_para_substituir->modificado) {
            printf("Página suja substituída. Gravando no swap: Página %d do processo %d\n\n",
                   pagina_para_substituir->tabelaIndex, pagina_para_substituir->processo + 1);
        }

        // Remove a página antiga
        pagina_para_substituir->valido = 0;
        pagina_para_substituir->ramIndex = -1;
        pagina_para_substituir->referenciado = 0;
        pagina_para_substituir->modificado = 0;

        // Insere a nova página
        ram->paginas[frame_para_substituir] = novaPagina;
        novaPagina->valido = 1;
        novaPagina->ramIndex = frame_para_substituir;
        novaPagina->referenciado = 1;
        novaPagina->modificado = operacao;
        novaPagina->contador = 0x80000000; // Reseta o contador (mais alta prioridade)

        printf("Página %d alocada no quadro %d para o processo %d\n",
               novaPagina->tabelaIndex, frame_para_substituir, currentProcess + 1);
    }
}

void reset_referenciado(TabelaPaginacao *tabela) {
    for (int i = 0; i < NUM_FRAMES_TABEL; i++) {
        Pagina *pagina = tabela->paginas[i];
        if (pagina != NULL && pagina->valido == 1) {
            pagina->referenciado = 0;
        }
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
                        shm_P1[0] = numero;
                        shm_P1[1] = (operacao == 'R') ? 0 : 1;
                        kill(getppid(), SIGUSR1);
                        raise(SIGSTOP);
                        break;
                    case 1:
                        shm_P2[0] = numero;
                        shm_P2[1] = (operacao == 'R') ? 0 : 1;
                        kill(getppid(), SIGUSR2);
                        raise(SIGSTOP); 
                        break;
                    case 2:
                        shm_P3[0] = numero;
                        shm_P3[1] = (operacao == 'R') ? 0 : 1;
                        kill(getppid(), SIGTERM);
                        raise(SIGSTOP);
                        break;
                    case 3:
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
    }
    shmdt(shm_P1);
    shmdt(shm_P2);
    shmdt(shm_P3);
    shmdt(shm_P4);


    // Fecha os arquivos
    for (int i = 0; i < 4; i++) {
        fclose(arquivos[i]);
    }
    kill(getppid(),SIGPWR);
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

void SignalHandler(int n){
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
        case SIGPWR:
            GLOBAL_END_SIMULATION = 1;
            break;
    }
}

void inicializar_tabela(TabelaPaginacao *tabela, int processo) {
    for (int i = 0; i < NUM_FRAMES_TABEL; i++) {
        // Aloca memória para cada página antes de inicializar
        tabela->paginas[i] = (Pagina *)malloc(sizeof(Pagina));
        if (tabela->paginas[i] == NULL) {
            perror("Erro ao alocar memória para página");
            exit(1);
        }

        // Inicializa os campos da página
        tabela->paginas[i]->valido = 0;
        tabela->paginas[i]->modificado = 0;
        tabela->paginas[i]->referenciado = 0;
        tabela->paginas[i]->ramIndex = -1;
        tabela->paginas[i]->processo = processo; // Para indicar que nenhuma página pertence a um processo ainda
        tabela->paginas[i]->tabelaIndex = i;
        tabela->paginas[i]->contador = 0;
    }
}


void inicializar_ram(RAM *ram) {
    for (int i = 0; i < NUM_FRAMES_RAM; i++) {
        ram->paginas[i] = NULL;
    }
}

void print_tabelas_paginacao(TabelaPaginacao tabelas[]) {
    for (int processo = 0; processo < NUM_PROCESSES; processo++) {
        printf("Tabela de Paginação do Processo P%d:\n\n", processo + 1);
        printf("Página | valido | Referenciado | Modificado | Índice RAM\n");
        printf("--------------------------------------------------------\n");

        for (int i = 0; i < NUM_FRAMES_TABEL; i++) {
            Pagina *pagina = tabelas[processo].paginas[i];
            if (pagina != NULL) {
                printf("%6d |   %3d  |      %3d     |     %3d     |    %5d\n", 
                       i, 
                       pagina->valido, 
                       pagina->referenciado, 
                       pagina->modificado, 
                       pagina->ramIndex);
            } else {
                printf("%6d |   ---  |      ---     |     ---     |    ---\n", i);
            }
        }

        printf("\n");
    }
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
                   pagina->valido, 
                   pagina->modificado, 
                   pagina->processo);
        }
    }
    printf("\n");
}
