/* Por Giovanni Oliveira <giovanni@ime.usp.br>
 * Em 19/09/2021
 *
 * Um código simples de um servidor de MQTT v3.3.1 implementando exclusivamente QoS = 0 (no máximo, entregar uma vez). Recebe como argumento a porta em que o servidor deve escutar.
 *
 * ./mqtt-broker 1883
 *
 */

/* Referência:
 * https://edisciplinas.usp.br/pluginfile.php/6485438/mod_assign/introattachment/0/mac5910-servidor-exemplo-ep1.c
 * Por Prof. Daniel Batista <batista@ime.usp.br>
 * Em 4/4/2021
 *
 * Um código simples de um servidor de eco a ser usado como base para
 * o EP1. Ele recebe uma linha de um cliente e devolve a mesma linha.
 */


#ifndef MQTT_BROKER_MAIN_H
#define MQTT_BROKER_MAIN_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include "../lib/set.h"

#define _GNU_SOURCE
#define LISTENQ 1
#define MAXCLIENTS 256

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// MQTT packet types
enum control_packet_types {
    CONNECT=1,
//    CONNACK=2,
    PUBLISH=3,
//    PUBACK=4,
//    PUBREC=5,
//    PUBREL=6,
//    PUBCOMP=7,
    SUBSCRIBE=8,
//    SUBACK=9,
//    UNSUBSCRIBE=10,
//    UNSUBACK=11,
    PINGREQ=12,
//    PINGRESP=13,
    DISCONNECT=14
};

// MQTT packet type flags
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

// MQTT packet variable header length
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

// MQTT connection statuses
enum connection_statuses {
    DISCONNECTED=0,
    CONNECTED=1,
};

// MQTT connection struct
typedef struct {
    int connfd;
    pthread_t thread;
    char *id;
    enum connection_statuses connstat;
    simple_set subscriptions;
} thread_t;

thread_t threads[MAXCLIENTS];

// number of connected clients
int client_count;

int main (int argc, char **argv);

//---------------------------------------------------------------------------//
// MAIN CLIENT HANDLER                                                       //
//---------------------------------------------------------------------------//

// thread code to handle new MQTT connection request
void handleClient(int thread_index);

//---------------------------------------------------------------------------//
// PROTOCOL ACTIONS                                                          //
//---------------------------------------------------------------------------//

// sends message to subscribed node
void mqtt_send_message(int thread_from, int thread_to, char *topic_name, char *message);

// publishes message to all subscribed nodes
void mqtt_publish(int thread_from, char *topic_name, char *message);

// subscribes a node into a topic
void mqtt_subscribe(char *topic_name, int thread_index);

// disconnects a node
void mqtt_disconnect(int thread_index);

//---------------------------------------------------------------------------//
// AUXILIARY FUNCTIONS                                                       //
//---------------------------------------------------------------------------//

// init thread slots with null value
void initThreadSlots();

// gets a new thread slot for connection `connfd`
// returns slot index in case of success
// returns -1 in case of fail
int getNewThreadSlot(int connfd);

// releases thread slot with index i
void releaseThreadSlot(int i);

// read bytes from `connfd` to get length encoded in MQTT-fashioned-way
// return the content length, consuming lenth codes from `connfd`
int decodeLength(int connfd);

#endif //MQTT_BROKER_MAIN_H
