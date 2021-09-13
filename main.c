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

#define PORT 10007
#define VARIABLE_HEADER_LEN 10
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int connfd;
    pthread_t thread;
} thread_t;

enum control_packet_types {
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
};

enum connection_statuses {
    DISCONNECTED=0
};

const char flag_bits[] = {(char) -1,
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

const char hasVariableHeader[] = {
        -1,
        1,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0
};

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

void encodeLength(unsigned int x, char *result){
    char encodedByte;
    do{
        encodedByte = x % 128;
        x = x / 128;
        if( x>0 ){
            encodedByte |= 128;
        }else{
            result[0] = encodedByte;
            result = &result[1];
        }
    }while(x>0);
    result[0] = '\0';
}

int decodeLength(int connfd){
    int mult = 1;
    int value = 0;
    int iteration = 0;
    char encodedByte;
    do{
        if(read(connfd,&encodedByte,1)!=1){
            return -1;
        }
        value += (encodedByte & 127) * mult;
        mult *= 128;
        if(mult > 128*128*128){
            return -1;
        }
        iteration++;
    }while((encodedByte & 128) != 0);
    return value;
}

void printByteArray(char *arr, int size){
    int i;
    for(i=0;i<size;i++)
        printf("%X ",arr[i]);
    printf("\n");
}

void handleClient(int thread_index){

    /* Armazena linhas recebidas do cliente */
    char recvline[MAXLINE + 1];
    /* Armazena o tamanho da string lida do cliente */
    ssize_t n;
    int connfd = threads[thread_index].connfd;
    enum connection_statuses connection_status;
    connection_status = DISCONNECTED;

    int user_name_flag, password_flag, will_retain, will_qos, will_flag, clean_session, keep_alive;

    while(1){

        char fixed_header;

        if(read(connfd,&fixed_header,1) != 1){
            printf("connection %d closed by client\n",connfd);
            break;
        }

        int control_packet_type = ((0xF0 & fixed_header) >> 4);
        int control_flag_bits = (0xF & fixed_header);

        printf("fixed header %x, pct type %x, flag %x\n",fixed_header, control_packet_type,control_flag_bits);
        if(flag_bits[control_packet_type]!= control_flag_bits){
            printf("type doesn't match flag. closing connection %d\n", connfd);
            break;
        }

        int content_length = decodeLength(connfd);
        if(content_length == -1){
            printf("invalid content length. connection %d closed by client\n",connfd);
            break;
        }
        printf("message size = %d\n", content_length);

        char has_variable_header = hasVariableHeader[control_packet_type];
        char variable_header[VARIABLE_HEADER_LEN];
        if(has_variable_header){
            if(read(connfd,&variable_header,VARIABLE_HEADER_LEN)!=VARIABLE_HEADER_LEN){
                printf("connection %d closed by client\n",connfd);
                break;
            }
        }


        int payload_length = content_length - (has_variable_header?VARIABLE_HEADER_LEN:0);
        char payload[payload_length+1];

        printf("reading %d bytes from payload\n",payload_length);
        if(read(connfd,&payload,payload_length)!=payload_length){
            printf("connection %d closed by client\n",connfd);
            break;
        }

        payload[payload_length] = '\0';


        if(control_packet_type == CONNECT){


            if( variable_header[0]!= 0 ||
                variable_header[1]!= 4 ||
                variable_header[2]!= 'M' ||
                variable_header[3]!= 'Q' ||
                variable_header[4]!= 'T' ||
                variable_header[5]!= 'T' ||
                variable_header[6]!= 4 ||
                (variable_header[7] & 1) != 0){

                printf("invalid variable header\n");
                break;
            }

            user_name_flag = variable_header[7] & 0x80;
            password_flag = variable_header[7] & 0x40;
            will_retain = variable_header[7] & 0x20;
            will_qos = variable_header[7] & 0x18;
            will_flag = variable_header[7] & 0x04;
            clean_session = variable_header[7] & 0x02;
            keep_alive = (variable_header[8] << 4) + variable_header[9];

            if(will_flag){
                printf("unsuported will flag");
                break;
            }

            int id_idx = 2;
            int id_len = ((payload[0] << 4) + payload[1]);

            printf("len %d\n",id_len);






        }















        write(connfd, NULL, 0);

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
        fprintf(stderr,"Uso: %d <Porta>\n",PORT);
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
    servaddr.sin_port        = htons(PORT);
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

    printf("[Servidor no ar. Aguardando conexões na porta %d]\n",PORT);
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
