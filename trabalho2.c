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
#define NUM_FRAMES 16



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

int main(int argc, char *argv[]){

    if (argc < 3) {  // Verifica se foi passado pelo menos um argumento além do nome do programa
        printf("Erro: Passe Qual Algoritimo Usar (NRU/ 2nCH/ LRU/ WS) e o Numero de rodadas.\n");
        return 1;
    }

    char* Algoritimo_cmd = (argv[1]); 
    int num_rodadas_cmd = atoi((argv[2])); //converte pra numero

    int shared_mem =  shmget (IPC_PRIVATE, 2*sizeof(int), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR); //Shm 
    int *tabela_paginaçao = (int*)shmat(shared_mem,0,0); //Conecta o pai com shm

    if( fork() == 0){
        TodosProcessos(tabela_paginaçao,num_rodadas_cmd);
    }

    return 0;
}
