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
#define DEBUG_MODE 0


int GLOBAL_NEW_PAGE = -1;
int GLOBAL_END_SIMULATION = -1;
int GLOBAL_TODOS_PROCESSOS = -1;

typedef struct {
    int valido;
    int referenciado;
    int modificado;
    int ramIndex;
    int processo;
    int tabelaIndex;
    unsigned int contador; // Campo para o Aging
} Pagina;

typedef struct WorkingSet {
    Pagina *pagina;                 // Ponteiro para a página
    struct WorkingSet *proximo;     // Ponteiro para o próximo elemento
} WorkingSet;


// Estrutura da fila
struct queue {
    char* nome;
    Pagina *paginas[NUM_FRAMES_RAM];
    int primeiro, ultimo;
};typedef struct queue Queue;

typedef struct {
    Pagina* paginas[NUM_FRAMES_TABEL];  
} TabelaPaginacao;

typedef struct {
    Pagina* frames[NUM_FRAMES_RAM];  
} RAM;

void imprimir_working_set(WorkingSet *ws);
Pagina* obter_ultima_pagina(WorkingSet *ws);
void inserir_working_set(WorkingSet **ws, Pagina *pagina);
Pagina* remover_pagina_working_set(WorkingSet **ws, Pagina *pagina_alvo);
int contar_elementos_working_set(WorkingSet *ws);
int contar_ocorrencias_pagina(WorkingSet *ws, Pagina *pagina);
void liberar_working_set(WorkingSet **ws);
void print_tabelas_paginacao(TabelaPaginacao tabelas[]);
void print_ram(RAM *ram);
void subs_NRU(RAM *ram, TabelaPaginacao *tabelas, int currentProcess, Pagina *novaPagina, int operacao);
void subs_2nd_chance(Queue *q, RAM *ram, Pagina *novaPagina, int operacao);
void inicializar_tabela(TabelaPaginacao *tabela, int processo);
void inicializar_ram(RAM *ram);
void call_substitution_algorithm(char *n);
void TodosProcessos(int *shm_P1,int *shm_P2,int *shm_P3,int *shm_P4,int num_rodadas);
void SignalHandler(int n);
int isPageFault(RAM *vetor, Pagina *elemento);
void subs_LRU_Aging(RAM *ram, Pagina *novaPagina, int currentProcess, int operacao,TabelaPaginacao * tabelas);
void subs_WS(RAM *ram, int currentProcess, int operacao, Pagina* novaPagina, WorkingSet *ws);
void atualizar_contadores(RAM *ram);
void reset_referenciado(TabelaPaginacao *tabela);
void init_queue(Queue *q, const char *nome);
int is_queue_empty(Queue *q);
void enqueue(Queue *q, Pagina *pagina);
Pagina* dequeue(Queue *q);
Pagina* peek(Queue *q);
void SignalHandler2(int n);



int main(int argc, char *argv[]){
    int ProcessPid;
    int num_rodadas_cmd; 
    int total_pagefaults = 0;
    int shm_P1, shm_P2, shm_P3, shm_P4;
    int *nova_pagina_P1, *nova_pagina_P2,*nova_pagina_P3,*nova_pagina_P4;
    int k;
    WorkingSet* WS[4];
    WorkingSet *WS_P1, *WS_P2, *WS_P3, *WS_P4;
    Queue filas[4];
    Queue fila_P1;
    Queue fila_P2;
    Queue fila_P3;
    Queue fila_P4;

    WS_P1 = NULL;
    WS_P2 = NULL;
    WS_P3 = NULL;
    WS_P4 = NULL;
    init_queue(&fila_P1,"Fila P1");
    init_queue(&fila_P2,"Fila P2");
    init_queue(&fila_P3,"Fila P3");
    init_queue(&fila_P4,"Fila P4");

    WS[0] = WS_P1;
    WS[1] = WS_P2;
    WS[2] = WS_P3;
    WS[3] = WS_P4;
    filas[0] = fila_P1;
    filas[1] = fila_P2;
    filas[2] = fila_P3;
    filas[3] = fila_P4;

    if (argc < 3) {  // Verifica se foi passado pelo menos um argumento além do nome do programa
        printf("Erro: Passe Qual Algoritimo Usar (NRU/ 2nCH/ LRU/ WS) e o Numero de rodadas(Garanta que o numero de rodadas seja menor que o numero de linhas dos arquivos).\n");
        return 1;
    }

    char* algoritmo_cmd = (argv[1]); 
    num_rodadas_cmd = atoi((argv[2])); //converte pra numero
    if(strcmp(algoritmo_cmd,"WS") == 0)
    {
        printf("Insira o valor de K: ");
        scanf("%d", &k);
        WS_P1 = NULL;
        WS_P2 = NULL;
        WS_P3 = NULL;
        WS_P4 = NULL;
    }

   


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
                printf("===============================================\n");
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
                if(DEBUG_MODE)
                    printf("Pagina lida: Index %d Operacao %d Processo %d\n\n", index, operacao, currentProcess+1);
                    
                int pagefault = isPageFault(&memoria_ram,currentPage);
                if(pagefault == -2) // A pagina ja esta na RAM
                {
                    if(strcmp(algoritmo_cmd,"2nCH") != 0)
                        reset_referenciado(&tabelas[currentProcess]);
                    
                    currentPage->modificado = operacao;
                    currentPage->referenciado = 1;

                    if(strcmp(algoritmo_cmd, "WS") == 0)
                    {
                        int contagem = 0;

                        if(contar_elementos_working_set(WS[currentProcess]) < k)
                        {   
                            inserir_working_set(&WS[currentProcess], currentPage);
                        }
                        else
                        {
                            contagem = contar_ocorrencias_pagina(WS[currentProcess], currentPage);
                            if(contagem > 0)
                            {
                                for (int i = 0; i < contagem; i++)
                                {
                                    remover_pagina_working_set(&WS[currentProcess], currentPage);
                                }
                                inserir_working_set(&WS[currentProcess], currentPage);
                            }
                            else
                            {
                                remover_pagina_working_set(&WS[currentProcess], obter_ultima_pagina(WS[currentProcess]));
                                inserir_working_set(&WS[currentProcess],currentPage);
                            }
                        }
                        imprimir_working_set(WS[currentProcess]);
                    }
                }
                else if(pagefault == -1) // a ram ta cheia e ele nao está la, tem q substituir
                {
                    total_pagefaults++;
                    printf("Pagefault do %d com processo %d\n", index,currentProcess+1);

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
                        subs_2nd_chance(&filas[currentProcess],&memoria_ram,currentPage,operacao);
                    }
                    else if(strcmp(algoritmo_cmd, "WS") == 0)
                    {
                        subs_WS(&memoria_ram,currentProcess,operacao,currentPage,WS[currentProcess]);
                    }
                }
                else //Existe espaco vazio na RAM para alocar a Pagina
                {
                    total_pagefaults++;

                    if(strcmp(algoritmo_cmd, "2nCh") != 0)
                        reset_referenciado(&tabelas[currentProcess]);
                    
                    memoria_ram.frames[pagefault] = currentPage;
                    currentPage->ramIndex = pagefault;
                    currentPage->valido = 1;
                    currentPage->processo = currentProcess;
                    currentPage->modificado = operacao;
                    currentPage->referenciado = 1;

                    if(strcmp(algoritmo_cmd,"WS") == 0)
                    {  

                        if(contar_elementos_working_set(WS[currentProcess]) < k)
                        {
                            if(DEBUG_MODE) printf("Quantidade de elementos WS antes de inserir: %d\n", contar_elementos_working_set(WS[currentProcess]));
                            inserir_working_set(&WS[currentProcess],currentPage);
                        }
                        else
                        {
                            
                            while(contar_elementos_working_set(WS[currentProcess]) >= k)
                            {
                                remover_pagina_working_set(&WS[currentProcess], obter_ultima_pagina(WS[currentProcess]));
                            }
                            inserir_working_set(&WS[currentProcess],currentPage);
                        }
                        imprimir_working_set(WS[currentProcess]);
                    }

                    if(strcmp(algoritmo_cmd,"2nCH") == 0)
                    {
                        enqueue(&filas[currentProcess],currentPage);
                    }
                }   
                GLOBAL_NEW_PAGE = -1;
                if(strcmp(algoritmo_cmd, "LRU") == 0) atualizar_contadores(&memoria_ram);
                kill(ProcessPid, SIGUSR1);
            }
          
            if(GLOBAL_END_SIMULATION != -1)
                break;
            
        }

        print_tabelas_paginacao(tabelas);
        printf("-------------- ALGORITMO DE SUBSTITUICAO: %s --------------\n", algoritmo_cmd);
        printf("--------------         NUMERO DE RODADAS: %d --------------\n", num_rodadas_cmd);
        printf("--------------                PAGEFAULTS: %d --------------n", total_pagefaults);
        if(strcmp(algoritmo_cmd, "WS") == 0)
            printf("Tamanho k do working set: %d", k);
    }

    return 0;
}

void atualizar_contadores(RAM *ram) {
    for (int i = 0; i < NUM_FRAMES_RAM; i++) {
        if (ram->frames[i] != NULL) {
            // Desloca o contador para a direita
            ram->frames[i]->contador >>= 1;

            // Se a página foi referenciada, define o bit mais significativo
            if (ram->frames[i]->referenciado) {
                ram->frames[i]->contador |= 0x80000000; // Define o MSB
            }

            // Reseta o bit de referência
            ram->frames[i]->referenciado = 0;
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
            if(DEBUG_MODE) printf("Contadores: %d %d %d %d\n", contadores[0],contadores[1],contadores[2],contadores[3]);
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
        ram->frames[frame] = novaPagina;
        novaPagina->valido = 1;
        novaPagina->ramIndex = frame;
        novaPagina->referenciado = 1;
        novaPagina->modificado = operacao;  
        novaPagina->processo = currentProcess;

        printf("Página substituída. Nova página %d alocada no lugar do %d para o processo %d\n\n",
               novaPagina->tabelaIndex, pagina_para_substituir->tabelaIndex, currentProcess+1);
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
        ram->frames[frame_para_substituir] = novaPagina;
        novaPagina->valido = 1;
        novaPagina->ramIndex = frame_para_substituir;
        novaPagina->referenciado = 1;
        novaPagina->modificado = operacao;
        novaPagina->contador = 0x00000000; // Reseta o contador (mais alta prioridade)

        printf("Página %d alocada no lugar do %d para o processo %d\n",
               novaPagina->tabelaIndex, pagina_para_substituir->tabelaIndex, currentProcess + 1);
    }
}

void subs_2nd_chance(Queue *q, RAM *ram, Pagina *novaPagina, int operacao) {
    while (1) {
        Pagina *pagina_para_substituir = peek(q); // Pega a página no início da fila
        if (pagina_para_substituir->referenciado == 0) {
            // Substituir a página
            printf("Página substituída: Página %d do processo %d\n", 
                   pagina_para_substituir->tabelaIndex, pagina_para_substituir->processo + 1);

            // Atualiza a RAM e tabela de paginação
            ram->frames[pagina_para_substituir->ramIndex] = novaPagina;
            novaPagina->ramIndex = pagina_para_substituir->ramIndex;
            novaPagina->valido = 1;
            novaPagina->referenciado = 1;
            novaPagina->modificado = operacao;

            pagina_para_substituir->valido = 0;
            pagina_para_substituir->ramIndex = -1;
            pagina_para_substituir->referenciado = 0;
            pagina_para_substituir->modificado = 0;


            dequeue(q); // Remove a página antiga
            enqueue(q, novaPagina); // Adiciona a nova página à fila
            break;
        } else {
            // Dá uma segunda chance: zera o bit `referenciado` e move para o final
            
            pagina_para_substituir->referenciado = 0;
            dequeue(q); // Remove do início
            enqueue(q, pagina_para_substituir); // Adiciona ao final
        }
    }
}

void subs_WS(RAM *ram, int currentProcess, int operacao, Pagina* novaPagina, WorkingSet *ws){
    Pagina* pagina_para_substituir = obter_ultima_pagina(ws);
    
    if(DEBUG_MODE)
    {
        printf("Inicio da substituicao: \n");
        imprimir_working_set(ws);
    }

    if(pagina_para_substituir != NULL)
    {
        int frame = pagina_para_substituir->ramIndex;

        remover_pagina_working_set(&ws, pagina_para_substituir);
        inserir_working_set(&ws, novaPagina);

        // Atualiza a tabela de paginação da página substituída 
        pagina_para_substituir->valido = 0;
        pagina_para_substituir->ramIndex = -1;
        pagina_para_substituir->referenciado = 0;
        pagina_para_substituir->modificado = 0;

        // Atualiza a RAM com a nova página
        ram->frames[frame] = novaPagina;
        novaPagina->valido = 1;
        novaPagina->ramIndex = frame;
        novaPagina->referenciado = 1;
        novaPagina->modificado = operacao;
        novaPagina->processo = currentProcess;
    }

    if(DEBUG_MODE)
    {
        printf("Fim da substituicao: \n");
        imprimir_working_set(ws);
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
    signal(SIGUSR1,SignalHandler2);
    GLOBAL_TODOS_PROCESSOS = 1;
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
            while(GLOBAL_TODOS_PROCESSOS != 1){}
            GLOBAL_TODOS_PROCESSOS = 0;
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
                        break;
                    case 1:
                        shm_P2[0] = numero;
                        shm_P2[1] = (operacao == 'R') ? 0 : 1;
                        kill(getppid(), SIGUSR2);
                        break;
                    case 2:
                        shm_P3[0] = numero;
                        shm_P3[1] = (operacao == 'R') ? 0 : 1;
                        kill(getppid(), SIGTERM);
                        break;
                    case 3:
                        shm_P4[0] = numero;
                        shm_P4[1] = (operacao == 'R') ? 0 : 1;
                        kill(getppid(), SIGTSTP);                 
                        break;
                    default:
                        break;
                }
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
        if (vetor->frames[i] == elemento) {
            return -2;
        }
        else if (vetor->frames[i] == NULL) {
            return i;
        }
    }
    return -1;
}

void SignalHandler2(int n){
    switch (n)
    {
        case SIGUSR1:
            GLOBAL_TODOS_PROCESSOS = 1;
            break;
    }
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
        ram->frames[i] = NULL;
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
        if (ram->frames[i] == NULL) {
            printf("%7d |   Vazia   |     ---     |        ---\n", i);
        } else {
            Pagina *pagina = ram->frames[i];
            printf("%7d |     %d     |     %d       |        %d\n", 
                   i, 
                   pagina->valido, 
                   pagina->modificado, 
                   pagina->processo);
        }
    }
    printf("\n");
}




void init_queue(Queue *q, const char *nome) {
    q->nome = strdup(nome); // Nome para identificar a fila (opcional)
    q->primeiro = q->ultimo = -1;
    for (int i = 0; i < NUM_FRAMES_RAM; i++) {
        q->paginas[i] = NULL;
    }
}


int is_queue_empty(Queue *q) {
    return (q->primeiro == -1);
}

void enqueue(Queue *q, Pagina *pagina) {
    int pos = (q->ultimo == -1) ? 0 : (q->ultimo + 1) % NUM_FRAMES_RAM;
    q->paginas[pos] = pagina;
    q->ultimo = pos;

    if (q->primeiro == -1) {
        q->primeiro = 0; // Inicializa o início da fila
    }
}

Pagina* dequeue(Queue *q) {
    if (is_queue_empty(q)) {
        printf("Erro: Fila vazia, não há páginas para remover.\n");
        return NULL;
    }
    Pagina *pagina = q->paginas[q->primeiro];
    q->paginas[q->primeiro] = NULL;

    if (q->primeiro == q->ultimo) {
        q->primeiro = q->ultimo = -1; // Reset quando a fila fica vazia
    } else {
        q->primeiro = (q->primeiro + 1) % NUM_FRAMES_RAM;
    }

    return pagina;
}

Pagina* peek(Queue *q) {
    if (is_queue_empty(q)) {
        return NULL;
    }
    return q->paginas[q->primeiro];
}

Pagina* obter_ultima_pagina(WorkingSet *ws) {
    if (ws == NULL) {
        printf("Erro: A lista está vazia.\n");
        return NULL;
    }

    WorkingSet *atual = ws;

    // Percorre até o último elemento da lista
    while (atual->proximo != NULL) {
        atual = atual->proximo;
    }

    return atual->pagina; // Retorna a página do último elemento
}


void inserir_working_set(WorkingSet **ws, Pagina *pagina) {
    int ocorrencias = contar_ocorrencias_pagina(*ws, pagina);
    if(ocorrencias > 0)
    {
        for (int i = 0; i < ocorrencias; i++)
        {
            remover_pagina_working_set(ws, pagina);
        }
        inserir_working_set(ws,pagina);
    } 
    else
    {
        WorkingSet *novo = (WorkingSet *)malloc(sizeof(WorkingSet));
        if (!novo) {
            perror("Erro ao alocar memória para WorkingSet");
            exit(1);
        }
        novo->pagina = pagina;
        novo->proximo = *ws;
        *ws = novo; // Atualiza a cabeça da lista
    }
}

Pagina* remover_pagina_working_set(WorkingSet **ws, Pagina *pagina_alvo) {
    if (*ws == NULL) {
        printf("Erro: A lista está vazia, nada para remover.\n");
        return NULL;
    }

    WorkingSet *atual = *ws;
    WorkingSet *anterior = NULL;

    // Percorre a lista para encontrar a página
    while (atual != NULL) {
        if (atual->pagina == pagina_alvo) { // Página encontrada
            if (anterior == NULL) {
                // A página está na cabeça da lista
                *ws = atual->proximo;
            } else {
                // A página está no meio ou no final
                anterior->proximo = atual->proximo;
            }

            Pagina *pagina_removida = atual->pagina;
            free(atual);
            return pagina_removida; // Retorna a página removida
        }

        // Avança para o próximo elemento
        anterior = atual;
        atual = atual->proximo;
    }

    printf("Página não encontrada no Working Set.\n");
    return NULL; // Página não encontrada
}



int contar_elementos_working_set(WorkingSet *ws) {
    int contador = 0;
    while (ws != NULL) {
        contador++;
        ws = ws->proximo;
    }
    return contador;
}

int contar_ocorrencias_pagina(WorkingSet *ws, Pagina *pagina) {
    int contador = 0;
    while (ws != NULL) {
        if (ws->pagina == pagina) {
            contador++;
        }
        ws = ws->proximo;
    }
    return contador;
}

void liberar_working_set(WorkingSet **ws) {
    while (*ws != NULL) {
        WorkingSet *removido = *ws;
        *ws = (*ws)->proximo;
        free(removido);
    }
}


void imprimir_working_set(WorkingSet *ws) {
    if (ws == NULL) {
        printf("O Working Set está vazio.\n");
        return;
    }

    printf("Páginas no Working Set (tabelaIndex):\n");
    while (ws != NULL) {
        printf("%d ", ws->pagina->tabelaIndex);
        ws = ws->proximo; // Avança para o próximo elemento
    }
    printf("\n");
}

