#include "main.h"

int main(int argc, char **argv) {

    initThreadSlots();

    /* Os sockets. Um que será o socket que vai escutar pelas conexões
     * e o outro que vai ser o socket específico de cada conexão */
    int listenfd, connfd;
    /* Informações sobre o socket (endereço e porta) ficam nesta struct */
    struct sockaddr_in servaddr;

    if (argc != 2) {
        fprintf(stderr, "Usage: mqtt-broker [port]\n");
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
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(atoi(argv[1]));
    if (bind(listenfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) == -1) {
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

    printf("[Server running on port %d]\n", atoi(argv[1]));
    printf("[Press CTRL+c to finish]\n");



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
        if ((connfd = accept(listenfd, (struct sockaddr *) NULL, NULL)) == -1) {
            perror("accept :(\n");
            exit(5);
        }

        int slot = getNewThreadSlot(connfd);
        if (slot == -1) {
            perror("All connection slots are busy.");
            close(connfd);
        } else {
            pthread_create(&threads[slot].thread, NULL, (void *) handleClient, slot);
        }

    }
}





//---------------------------------------------------------------------------//
// MAIN CLIENT HANDLER                                                       //
//---------------------------------------------------------------------------//

void handleClient(int thread_index) {

    /* Armazena o tamanho da string lida do cliente */
    thread_t this_thread = threads[thread_index];
    this_thread.connstat = DISCONNECTED;
    int connfd = this_thread.connfd;

    int will_flag;
//    int user_name_flag, password_flag, will_retain, will_qos, clean_session, keep_alive;

    while (1) {

        unsigned char fixed_header;

        if (read(connfd, &fixed_header, 1) != 1) {
            printf("connection %d closed by client\n", connfd);
            break;
        }

        int control_packet_type = ((0xF0 & fixed_header) >> 4);
        int control_flag_bits = (0xF & fixed_header);


        printf("\nNEW MQTT PACK: fixed header %x\n", fixed_header);
        if (flag_bits[control_packet_type] != control_flag_bits) {
            printf("type doesn't match flag. closing connection %d\n", connfd);
            break;
        }

        int content_length = decodeLength(connfd);
        if (content_length == -1) {
            printf("invalid content length. connection %d closed by client\n", connfd);
            break;
        }
        printf("message size = %d\n", content_length);

        unsigned char variable_header_len = variableHeaderLen[control_packet_type];
        unsigned char variable_header[variable_header_len];
        if (variable_header_len) {
            if (read(connfd, variable_header, variable_header_len) != variable_header_len) {
                printf("connection %d closed by client\n", connfd);
                break;
            }
        }


        int payload_length = content_length - variable_header_len;
        unsigned char payload[payload_length + 1];

        printf("reading %d bytes from payload\n", payload_length);
        if (read(connfd, payload, payload_length) != payload_length) {
            printf("connection %d closed by client\n", connfd);
            break;
        }

        payload[payload_length] = '\0';


        if (control_packet_type == CONNECT) {

            if (variable_header[0] != 0 ||
                variable_header[1] != 4 ||
                variable_header[2] != 'M' ||
                variable_header[3] != 'Q' ||
                variable_header[4] != 'T' ||
                variable_header[5] != 'T' ||
                variable_header[6] != 4 ||
                (variable_header[7] & 1) != 0) {

                printf("invalid variable header\n");
                break;
            }

//            user_name_flag = variable_header[7] & 0x80;
//            password_flag = variable_header[7] & 0x40;
//            will_retain = variable_header[7] & 0x20;
//            will_qos = variable_header[7] & 0x18;
            will_flag = variable_header[7] & 0x04;
//            clean_session = variable_header[7] & 0x02;
//            keep_alive = (variable_header[8] << 4) + variable_header[9];

            if (will_flag) {
                printf("unsuported will flag");
                break;
            }

            int id_idx = 2;
            int id_len = ((payload[0] << 4) + payload[1]);

            if (this_thread.id)
                free(this_thread.id);
            this_thread.id = malloc(id_len + 1);
            //TODO check malloc ( and consider null id ? )
            memcpy(this_thread.id, &payload[id_idx], id_len);
            this_thread.id[id_len] = '\0';

            printf("Received CONNECT for node %d with ID=%s. Accepting connection\n", thread_index, this_thread.id);

            unsigned char connack_response[4];
            connack_response[0] = 0x20;
            connack_response[1] = 0x02;
            //TODO implement session cache
            connack_response[2] = 0x00;
            connack_response[3] = 0x00; // accepted connection

            write(connfd, connack_response, 4);
            this_thread.connstat = CONNECTED;

            printf("MQTT connection established with %s\n", this_thread.id);

        } else if (this_thread.connstat == CONNECTED && control_packet_type == PUBLISH) {

            int topic_name_len = (variable_header[0] << 4) + variable_header[1];
            char topic_name[topic_name_len + 1];
            memcpy(topic_name, payload, topic_name_len);
            topic_name[topic_name_len] = '\0';


            int message_len = payload_length - topic_name_len;
            char message[message_len + 1];
            memcpy(message, &payload[topic_name_len], message_len);
            message[message_len] = '\0';

            //no response needed for QoS=0
            write(connfd, NULL, 0);

            mqtt_publish(thread_index, topic_name, message);

        } else if (this_thread.connstat == CONNECTED && control_packet_type == DISCONNECT) {

            mqtt_disconnect(thread_index);

        } else if (this_thread.connstat == CONNECTED && control_packet_type == SUBSCRIBE) {

            int payload_idx = 0;
            int sub_cnt = 0;
            while (payload_idx < (payload_length - 1)) {
                int topic_name_len = (payload[payload_idx] << 4) + payload[payload_idx + 1];
                char topic_name[topic_name_len + 1];
                memcpy(&topic_name, &payload[payload_idx + 2], topic_name_len);
                topic_name[topic_name_len] = '\0';
                if (payload[2 + topic_name_len] != 0) {
                    printf("subscription with qos non-supported QoS=%x\n", payload[2 + topic_name_len]);
                    write(connfd, NULL, 0);
                    break;
                }
                mqtt_subscribe(topic_name, thread_index);
                payload_idx += topic_name_len + 3;
                sub_cnt++;
            }

            unsigned char suback[sub_cnt + 4];
            suback[0] = 0x90;
            suback[1] = sub_cnt + 2;
            suback[2] = variable_header[0];
            suback[3] = variable_header[1];
            int i;
            for (i = 0; i < sub_cnt; i++) {
                suback[i + 4] = 0;
            }

            write(connfd, suback, sub_cnt + 4);
        } else if (this_thread.connstat == CONNECTED && control_packet_type == PINGREQ) {
            printf("PINGREQ received from %d\n", thread_index);
            unsigned char pingresp[2];
            pingresp[0] = 0xD0; //pingresp
            pingresp[1] = 0x00;
            write(connfd, pingresp, 2);
        }

    }

    close(connfd);
    printf("\nclosed connection %d\n", connfd);
    releaseThreadSlot(thread_index);
}


//---------------------------------------------------------------------------//
// PROTOCOL ACTIONS                                                          //
//---------------------------------------------------------------------------//
void mqtt_send_message(int thread_from, int thread_to, char *topic_name, char *message) {
    printf("MESSAGE from slot %d;;; to slot %d;;; topic %s;;; message %s\n", thread_from, thread_to, topic_name,
           message);
    unsigned long topic_name_len = strlen(topic_name);
    unsigned long message_len = strlen(message);
    unsigned long response_len = 4 + topic_name_len + message_len;
    unsigned char response[response_len];
    response[0] = 0x30;
    response[1] = response_len - 2;
    response[2] = (unsigned char) topic_name_len >> 4;
    response[3] = (unsigned char) topic_name_len & 0xFF;
    memcpy(&response[4], topic_name, topic_name_len);
    memcpy(&response[4 + topic_name_len], message, message_len);

    write(threads[thread_to].connfd, response, response_len);
}

void mqtt_publish(int thread_from, char *topic_name, char *message) {
    printf("PUB topic: %s;;; message: %s\n", topic_name, message);
    int i, cnt;
    for (i = 0, cnt = 0; i < MAXCLIENTS && cnt < client_count; i++) {
        thread_t t = threads[i];
        if (t.connfd) {
            cnt++;
            if (set_contains(&t.subscriptions, topic_name) == SET_TRUE) {
                mqtt_send_message(thread_from, i, topic_name, message);
            }
        }

    }
}

void mqtt_subscribe(char *topic_name, int thread_index) {
    printf("SUB %s %d\n", topic_name, thread_index);
    set_add(&(threads[thread_index].subscriptions), topic_name);
}

void mqtt_disconnect(int thread_index) {
    printf("DISC %d\n", thread_index);
}





//---------------------------------------------------------------------------//
// AUXILIARY FUNCTIONS                                                       //
//---------------------------------------------------------------------------//

void initThreadSlots() {
    int i;
    for (i = 0; i < MAXCLIENTS; i++) {
        threads[i].connfd = 0;
        set_init(&threads[i].subscriptions);
    }
    client_count = 0;
}

int getNewThreadSlot(int connfd) {
    if (client_count >= MAXCLIENTS) {
        printf("unable to found available slot for connection %d\n", connfd);
        return -1;
    } else {
        int i;
        pthread_mutex_lock(&lock);
        for (i = 0; i < MAXCLIENTS; i++) {
            if (!threads[i].connfd) {
                threads[i].connfd = connfd;
                break;
            }
        }
        client_count++;
        pthread_mutex_unlock(&lock);
        printf("slot %d reserved for connection %d\n", i, connfd);
        return i;
    }
}

void releaseThreadSlot(int i) {

    pthread_mutex_lock(&lock);
    threads[i].connfd = 0;
    threads[i].connstat = DISCONNECTED;
    free(threads[i].id);
    set_clear(&threads[i].subscriptions);
    client_count--;
    pthread_mutex_unlock(&lock);
    printf("release slot %d\n", i);

}

int decodeLength(int connfd) {
    int mult = 1;
    int value = 0;
    int iteration = 0;
    unsigned char encodedByte;
    do {
        if (read(connfd, &encodedByte, 1) != 1) {
            return -1;
        }
        value += (encodedByte & 127) * mult;
        mult *= 128;
        if (mult > 128 * 128 * 128) {
            return -1;
        }
        iteration++;
    } while ((encodedByte & 128) != 0);
    return value;
}
