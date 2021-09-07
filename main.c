/* Por Prof. Daniel Batista <batista@ime.usp.br>
 * Em 4/4/2021
 * 
 * Um código simples de um servidor de eco a ser usado como base para
 * o EP1. Ele recebe uma linha de um cliente e devolve a mesma linha.
 * Teste ele assim depois de compilar:
 * 
 * ./mac5910-servidor-exemplo-ep1 8000
 * 
 * Com este comando o servidor ficará escutando por conexões na porta
 * 8000 TCP (Se você quiser fazer o servidor escutar em uma porta
 * menor que 1024 você precisará ser root ou ter as permissões
 * necessáfias para rodar o código com 'sudo').
 *
 * Depois conecte no servidor via telnet. Rode em outro terminal:
 * 
 * telnet 127.0.0.1 8000
 * 
 * Escreva sequências de caracteres seguidas de ENTER. Você verá que o
 * telnet exibe a mesma linha em seguida. Esta repetição da linha é
 * enviada pelo servidor. O servidor também exibe no terminal onde ele
 * estiver rodando as linhas enviadas pelos clientes.
 * 
 * Obs.: Você pode conectar no servidor remotamente também. Basta
 * saber o endereço IP remoto da máquina onde o servidor está rodando
 * e não pode haver nenhum firewall no meio do caminho bloqueando
 * conexões na porta escolhida.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#define LISTENQ 1
#define MAXDATASIZE 100
#define MAXLINE 4096
#define MAXCLIENTS 256

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int connfd;
    pthread_t thread;
} thread_t;

enum {
    CONNECT=1,
    CONNACK=2,
    PUBLISH=3,
    PUBACK=4,
    PUBREC=5,
    PUBREL=6,
    PUBCOMP=7,
    SUBSCRIBE=8,
    SUBACK=9,
    UNSUBSCRIBE=10,
    UNSUBACK=11,
    PINGREQ=12,
    PINGRESP=13,
    DISCONNECT=14
} control_packet_type;

const char flagBits[] = {(char) -1,
                         (char) 0,
                         (char) 0,
                         (char) 0,
                         (char) 0,
                         (char) 0,
                         (char) 2,
                         (char) 0,
                         (char) 2,
                         (char) 0,
                         (char) 2,
                         (char) 0,
                         (char) 0,
                         (char) 0,
                         (char) 0};

thread_t threads[MAXCLIENTS];

void initThreadSlots(){
    int i;
    for(i = 0; i<MAXCLIENTS; i++){
        threads[i].connfd = 0;
    }
}

int getNewThreadSlot(int connfd){
    int i;
    pthread_mutex_lock(&lock);
    for(i=0;i<MAXCLIENTS;i++){
        if(!threads[i].connfd){
            threads[i].connfd = connfd;
            break;
        }
    }
    pthread_mutex_unlock(&lock);
    if(i>=MAXCLIENTS){
        printf("unable to found available slot for connection %d\n",connfd);
        return -1;
    }else{
        printf("slot %d reserved for connection %d\n",i,connfd);
        return i;
    }
}

void releaseThreadSlot(int i){

    pthread_mutex_lock(&lock);
    threads[i].connfd = 0;
    pthread_mutex_unlock(&lock);
    printf("release slot %d\n",i);

}



void handleClient(int thread_index){

    /* Armazena linhas recebidas do cliente */
    char recvline[MAXLINE + 1];
    /* Armazena o tamanho da string lida do cliente */
    ssize_t n;
    int connfd = threads[thread_index].connfd;

    while ((n=read(connfd, recvline, MAXLINE)) > 0) {
        recvline[n]=0;
        if ((fputs(recvline,stdout)) == EOF) {
            printf("thread %d read EOF. closing connection", thread_index);
            break;
        }
        printf("value %x\n", recvline[0]);

        // RECEIVED NON_NULL MESSAGE IN RECVLINE









        write(connfd, recvline, strlen(recvline));
    }

    close(connfd);
    printf("closed connection %d\n",connfd);
    releaseThreadSlot(thread_index);
}



int main (int argc, char **argv) {

    initThreadSlots();

    /* Os sockets. Um que será o socket que vai escutar pelas conexões
     * e o outro que vai ser o socket específico de cada conexão */
    int listenfd, connfd;
    /* Informações sobre o socket (endereço e porta) ficam nesta struct */
    struct sockaddr_in servaddr;

    if (argc != 2) {
        fprintf(stderr,"Uso: %s <Porta>\n",argv[0]);
        fprintf(stderr,"Vai rodar um servidor de echo na porta <Porta> TCP\n");
        exit(1);
    }

    /* Criação de um socket. É como se fosse um descritor de arquivo.
     * É possível fazer operações como read, write e close. Neste caso o
     * socket criado é um socket IPv4 (por causa do AF_INET), que vai
     * usar TCP (por causa do SOCK_STREAM), já que o MQTT funciona sobre
     * TCP, e será usado para uma aplicação convencional sobre a Internet
     * (por causa do número 0) */
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket :(\n");
        exit(2);
    }

    /* Agora é necessário informar os endereços associados a este
     * socket. É necessário informar o endereço / interface e a porta,
     * pois mais adiante o socket ficará esperando conexões nesta porta
     * e neste(s) endereços. Para isso é necessário preencher a struct
     * servaddr. É necessário colocar lá o tipo de socket (No nosso
     * caso AF_INET porque é IPv4), em qual endereço / interface serão
     * esperadas conexões (Neste caso em qualquer uma -- INADDR_ANY) e
     * qual a porta. Neste caso será a porta que foi passada como
     * argumento no shell (atoi(argv[1]))
     */
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port        = htons(atoi(argv[1]));
    if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
        perror("bind :(\n");
        exit(3);
    }

    /* Como este código é o código de um servidor, o socket será um
     * socket passivo. Para isto é necessário chamar a função listen
     * que define que este é um socket de servidor que ficará esperando
     * por conexões nos endereços definidos na função bind. */
    if (listen(listenfd, LISTENQ) == -1) {
        perror("listen :(\n");
        exit(4);
    }

    printf("[Servidor no ar. Aguardando conexões na porta %s]\n",argv[1]);
    printf("[Para finalizar, pressione CTRL+c ou rode um kill ou killall]\n");



    /* O servidor no final das contas é um loop infinito de espera por
     * conexões e processamento de cada uma individualmente */
    for (;;) {
        /* O socket inicial que foi criado é o socket que vai aguardar
         * pela conexão na porta especificada. Mas pode ser que existam
         * diversos clientes conectando no servidor. Por isso deve-se
         * utilizar a função accept. Esta função vai retirar uma conexão
         * da fila de conexões que foram aceitas no socket listenfd e
         * vai criar um socket específico para esta conexão. O descritor
         * deste novo socket é o retorno da função accept. */
        if ((connfd = accept(listenfd, (struct sockaddr *) NULL, NULL)) == -1 ) {
            perror("accept :(\n");
            exit(5);
        }

        int slot = getNewThreadSlot(connfd);
        if(slot == -1){
            perror("All connection slots are busy.");
            close(connfd);
        }else{
            pthread_create(&threads[slot].thread, NULL, handleClient, slot);
        }

    }
}
