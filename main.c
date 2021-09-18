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
#include "lib/set.h"

#define LISTENQ 1
#define MAXDATASIZE 100
#define MAXLINE 4096
#define MAXCLIENTS 256

#define PORT 1883
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;



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

const unsigned char flag_bits[] = {(unsigned char) -1,
                         (unsigned char) 0,
                         (unsigned char) 0,
                         (unsigned char) 0,
                         (unsigned char) 0,
                         (unsigned char) 0,
                         (unsigned char) 2,
                         (unsigned char) 0,
                         (unsigned char) 2,
                         (unsigned char) 0,
                         (unsigned char) 2,
                         (unsigned char) 0,
                         (unsigned char) 0,
                         (unsigned char) 0,
                         (unsigned char) 0};

const unsigned char variableHeaderLen[] = {
        -1,
        10, // CONNECT
        0,
        2, // PUBLISH
        0,
        0,
        0,
        0,
        2,
        0,
        0,
        0,
        0,
        0,
        0
};

enum connection_statuses {
    DISCONNECTED=0,
    CONNECTED=1,

};

typedef struct {
    int connfd;
    pthread_t thread;
    char *id;
    enum connection_statuses connstat;
    simple_set subscriptions;
} thread_t;

thread_t threads[MAXCLIENTS];
int client_count;

void initThreadSlots(){
    int i;
    for(i = 0; i<MAXCLIENTS; i++){
        threads[i].connfd = 0;
        set_init(&threads[i].subscriptions);
    }
    client_count = 0;
}

int getNewThreadSlot(int connfd){
    if(client_count>=MAXCLIENTS){
        printf("unable to found available slot for connection %d\n",connfd);
        return -1;
    }else{
        int i;
        pthread_mutex_lock(&lock);
        for(i=0;i<MAXCLIENTS;i++){
            if(!threads[i].connfd){
                threads[i].connfd = connfd;
                break;
            }
        }
        client_count++;
        pthread_mutex_unlock(&lock);
        printf("slot %d reserved for connection %d\n",i,connfd);
        return i;
    }
}

void releaseThreadSlot(int i){

    pthread_mutex_lock(&lock);
    threads[i].connfd = 0;
    threads[i].connstat = DISCONNECTED;
    free(threads[i].id);
    set_clear(&threads[i].subscriptions);
    client_count--;
    pthread_mutex_unlock(&lock);
    printf("release slot %d\n",i);

}

void encodeLength(unsigned int x, unsigned char *result){
    unsigned char encodedByte;
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
    unsigned char encodedByte;
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

void printByteArray(unsigned char *arr, int size){
    int i;
    for(i=0;i<size;i++)
        printf("%X ",arr[i]);
    printf("\n");
}

void mqtt_publish(char *topic_name, char *message){
    printf("PUB %s %s",topic_name,message);
    int i, cnt;
    for(i=0,cnt=0;i<MAXCLIENTS && cnt<client_count;i++){
        puts("swipe");
        printf("%d %d \n",cnt,client_count);
        thread_t t = threads[i];
        if(t.connfd){
            cnt++;
            if(set_contains(&t.subscriptions,topic_name)){
                printf("MESSAGE to conn %d;;; topic %s;;; message %s\n",i,topic_name,message);
            }else{
                puts("set doesnt contain");
            }
        }
    }

}

void mqtt_subscribe(char *topic_name, int thread_index){
    printf("SUB %s %d\n",topic_name, thread_index);
    set_add(& (threads[thread_index].subscriptions),topic_name);
}

void mqtt_disconnect(int thread_index){
    printf("DISC %d\n",thread_index);
}

void handleClient(int thread_index){

    /* Armazena linhas recebidas do cliente */
    unsigned char recvline[MAXLINE + 1];
    /* Armazena o tamanho da string lida do cliente */
    ssize_t n;
    thread_t this_thread = threads[thread_index];
    this_thread.connstat = DISCONNECTED;
    int connfd = this_thread.connfd;

    int user_name_flag, password_flag, will_retain, will_qos, will_flag, clean_session, keep_alive;

    while(1){

        unsigned char fixed_header;

        if(read(connfd,&fixed_header,1) != 1){
            printf("connection %d closed by client\n",connfd);
            break;
        }

        int control_packet_type = ((0xF0 & fixed_header) >> 4);
        int control_flag_bits = (0xF & fixed_header);


        printf("\nNEW MQTT PACK: fixed header %x\n",fixed_header);
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

        unsigned char variable_header_len = variableHeaderLen[control_packet_type];
        unsigned char variable_header[variable_header_len];
        if(variable_header_len){
            if(read(connfd,variable_header,variable_header_len)!=variable_header_len){
                printf("connection %d closed by client\n",connfd);
                break;
            }
        }


        int payload_length = content_length - variable_header_len;
        unsigned char payload[payload_length+1];

        printf("reading %d bytes from payload\n",payload_length);
        if(read(connfd,payload,payload_length)!=payload_length){
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

            if(this_thread.id)
                free(this_thread.id);
            this_thread.id = malloc(id_len+1);
            //TODO check malloc ( and consider null id ? )
            memcpy(this_thread.id,&payload[id_idx],id_len);
            this_thread.id[id_len] = '\0';

            printf("Received CONNECT for node %d with ID=%s. Accepting connection\n",thread_index,this_thread.id);

            unsigned char connack_response[4];
            connack_response[0] = 0x20;
            connack_response[1] = 0x02;
            //TODO implement session cache
            connack_response[2] = 0x00;
            connack_response[3] = 0x00; // accepted connection

            write(connfd, connack_response, 4);
            this_thread.connstat = CONNECTED;

            printf("MQTT connection established with %s\n",this_thread.id);

        }else if(this_thread.connstat == CONNECTED && control_packet_type == PUBLISH){

            int topic_name_len = (variable_header[0] << 4)+variable_header[1];
            char topic_name[topic_name_len+1];
            memcpy(topic_name,payload,topic_name_len);
            topic_name[topic_name_len] = '\0';


            int message_len = payload_length-topic_name_len;
            char message[message_len+1];
            memcpy(message,&payload[topic_name_len],message_len);
            message[message_len] = '\0';

            //no response needed for QoS=0
            write(connfd, NULL, 0);

            mqtt_publish(topic_name,message);

        } else if (this_thread.connstat == CONNECTED && control_packet_type == DISCONNECT){

            mqtt_disconnect(thread_index);

        } else if (this_thread.connstat == CONNECTED && control_packet_type == SUBSCRIBE){

            int payload_idx = 0;
            int sub_cnt = 0;
            while(payload_idx < (payload_length-1)){
                int topic_name_len = (payload[payload_idx] << 4)+payload[payload_idx+1];
                char topic_name[topic_name_len+1];
                memcpy(&topic_name,&payload[payload_idx+2],topic_name_len);
                topic_name[topic_name_len] = '\0';
                if(payload[2+topic_name_len] != 0){
                    printf("subscription with qos non-supported QoS=%x\n",payload[2+topic_name_len] );
                    write(connfd,NULL,0);
                    break;
                }
                mqtt_subscribe(topic_name,thread_index);
                payload_idx+= topic_name_len+3;
                sub_cnt++;
            }

            unsigned char suback[sub_cnt+4];
            suback[0] = 0x90;
            suback[1] = sub_cnt+2;
            suback[2] = variable_header[0];
            suback[3] = variable_header[1];
            int i;
            for(i=0; i<sub_cnt; i++){
                suback[i+4] = 0;
            }

            write(connfd,suback,sub_cnt+4);
        } else if (this_thread.connstat == CONNECTED && control_packet_type == PINGREQ){
            printf("PINGREQ received from %d\n",thread_index);
            unsigned char pingresp[2];
            pingresp[0] = 0xD0; //pingresp
            pingresp[1] = 0x00;
            write(connfd, pingresp, 2);
        }

    }

    close(connfd);
    printf("\nclosed connection %d\n",connfd);
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
