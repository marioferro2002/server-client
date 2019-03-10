#define _BSD_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/select.h>

/* program flow and execution defined variables */
#define M 4
#define N 3
#define P 8
#define Q 3
#define R 3
#define S 5
#define T 2
#define U 3

/* global variables */
bool debug_mode = false;
FILE *network_dev_config_file = NULL;
struct DeviceData device_data;
struct ServerData server_data;
struct Sockets sockets;
char *client_state = NULL;
pthread_t tids[2] = {(pthread_t) NULL, (pthread_t) NULL};

/* Simulates PDU for signup and keep in touch with server purposes */
struct Package {
    unsigned char type;
    char dev_name[7];
    char mac_address[13];
    char dev_random_num[7];
    char data[50];
};

/* Simulates PDU for send-conf and get-conf purposes */
struct PackageForCommands {
    unsigned char type;
    char dev_name[7];
    char mac_address[13];
    char dev_random_num[7];
    char data[150];
};

/* device = machine where the client is running */
struct DeviceData {
    char dev_name[9];
    char dev_mac[13];
    char dev_random_num[7];
};

struct ServerData {
    char *server_name_or_address;
};

struct Sockets {
    int udp_socket;
    int udp_port;
    struct timeval udp_timeout;
    struct sockaddr_in udp_addr_server;

    int tcp_socket;
    int tcp_port;
    struct timeval tcp_timeout;
    struct sockaddr_in tcp_addr_server;
};

/* functions declaration */
void change_client_state(char *new_state);
void end_handler(int signal);

void *keep_in_touch_with_server();

void *manage_command_line_input();

void parse_argv(int argc, const char *argv[]);
void parse_and_save_software_config_file_data(FILE *software_config_file);

void print_accepted_commands();
void print_message(char *to_print);

void save_register_ack_data(struct Package package_received);
void setup_UDP_socket();

void setup_TCP_socket();

void send_package_via_udp_to_server(struct Package package_to_send, char *currentFunction);

void signup_on_server();
int get_waiting_time_after_sent(int reg_reqs_sent);

char *read_from_stdin(int max_chars_to_read);
unsigned char get_packet_type_from_string();

struct Package construct_alive_inf_package();
struct Package construct_register_request_package();
struct Package receive_package_via_udp_from_server();

/* input: ./client -c <software_config_file> -d 
          -f <network_dev_config_file>       */
int main(int argc, const char *argv[]) {
    signal(SIGINT, end_handler);
    parse_argv(argc, argv);
    setup_UDP_socket();
    signup_on_server();
    setup_TCP_socket();
    /* simultaneously read from command line and keep in touch with
       server to make sure the server is alive */
    pthread_create(&tids[0], NULL, keep_in_touch_with_server, NULL);
    pthread_create(&tids[1], NULL, manage_command_line_input, NULL);
    // join when obtained 'quit' command from command line
    pthread_join(tids[1], NULL);
    tids[1] = (pthread_t) NULL;
    end_handler(SIGINT);

    return 0;
}

/* functions implementation */

void end_handler(int signal) {
    if (signal == SIGINT) {
        write(2, "\nExiting client...\n", 20);
        for (int i = 0; i < 2; i++) {
            if (tids[i] != ((pthread_t) NULL)) {
                pthread_cancel(tids[i]);
            }
        }
        close(sockets.tcp_socket);
        close(sockets.udp_socket);
        /* TO DO - free pointers */
        exit(0);
    }
}

void parse_argv(int argc, const char *argv[]) {
    FILE *software_config_file = NULL;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0) {
            if (argc > i) { software_config_file = fopen(argv[i + 1], "r"); }
        } else if (strcmp(argv[i], "-d") == 0) {
            debug_mode = true;
            print_message("INFO -> Debug mode enabled\n");
        } else if (strcmp(argv[i], "-f") == 0) {
            if (argc > i) { network_dev_config_file = fopen(argv[i + 1], "r"); }
        }
    }

    if (debug_mode) { print_message("DEBUG -> Read command line input\n"); }

    if (software_config_file == NULL) {
        software_config_file = fopen("client.cfg", "r");
    }
    if (network_dev_config_file == NULL) { // open default
        network_dev_config_file = fopen("boot.cfg", "r");
    }
    parse_and_save_software_config_file_data(software_config_file);

    if (debug_mode) { print_message("DEBUG -> Read data from configuration files\n"); }
}

void parse_and_save_software_config_file_data(FILE *software_config_file) {
    char line[70];
    char delim[] = " \n";
    char *token;

    /* read line by line */
    while (fgets(line, 70, software_config_file)) {
        token = strtok(line, delim);

        if (strcmp(token, "Nom") == 0) {
            token = strtok(NULL, delim);
            strcpy(device_data.dev_name, token);
        } else if (strcmp(token, "MAC") == 0) {
            token = strtok(NULL, delim);
            strcpy(device_data.dev_mac, token);
        } else if (strcmp(token, "Server") == 0) {
            token = strtok(NULL, delim);
            server_data.server_name_or_address = malloc(strlen(token) + 1);
            strcpy(server_data.server_name_or_address, token);
        } else if (strcmp(token, "Server-port") == 0) {
            sockets.udp_port = atoi(strtok(NULL, delim));
        }
    }
}

void change_client_state(char *new_state) {
    if (client_state == NULL || strcmp(client_state, new_state) != 0) {
        client_state = malloc(sizeof(new_state));
        strcpy(client_state, new_state);
        char message[50];
        sprintf(message, "INFO -> Client state changed to: %s\n", client_state);
        print_message(message);
    }
}

void print_message(char *to_print) {
    time_t now;
    struct tm *now_tm;
    int hour, minutes, secs;

    now = time(NULL);
    now_tm = localtime(&now);
    hour = now_tm->tm_hour;
    minutes = now_tm->tm_min;
    secs = now_tm->tm_sec;
    printf("%d:%d:%d - %s", hour, minutes, secs, to_print);
    fflush(stdout); /* print immediately */
}

unsigned char get_packet_type_from_string(char *string) {
    unsigned char packet_type;

    /* signup process packet types */
    if (strcmp(string, "REGISTER_REQ") == 0) {
        packet_type = (unsigned char) 0x00;
    } else if (strcmp(string, "REGISTER_ACK") == 0) {
        packet_type = (unsigned char) 0x01;
    } else if (strcmp(string, "REGISTER_NACK") == 0) {
        packet_type = (unsigned char) 0x02;
    } else if (strcmp(string, "REGISTER_REJ") == 0) {
        packet_type = (unsigned char) 0x03;
        /* keep in touch packet types */
    } else if (strcmp(string, "ALIVE_INF") == 0) {
        packet_type = (unsigned char) 0x10;
    } else if (strcmp(string, "ALIVE_ACK") == 0) {
        packet_type = (unsigned char) 0x11;
    } else if (strcmp(string, "ALIVE_NACK") == 0) {
        packet_type = (unsigned char) 0x12;
    } else if (strcmp(string, "ALIVE_REJ") == 0) {
        packet_type = (unsigned char) 0x13;
    } else {
        packet_type = (unsigned char) 0x09;
    }

    return packet_type;
}

char *get_packet_string_from_type(unsigned char type) {
    char *packet_string;

    /* signup process packet types */
    if (type == (unsigned char) 0x00) {
        packet_string = "REGISTER_REQ";
    } else if (type == (unsigned char) 0x01) {
        packet_string = "REGISTER_ACK";
    } else if (type == (unsigned char) 0x02) {
        packet_string = "REGISTER_NACK";
    } else if (type == (unsigned char) 0x03) {
        packet_string = "REGISTER_REJ";
        /* keep in touch packet types */
    } else if (type == (unsigned char) 0x10) {
        packet_string = "ALIVE_INF";
    } else if (type == (unsigned char) 0x11) {
        packet_string = "ALIVE_ACK";
    } else if (type == (unsigned char) 0x12) {
        packet_string = "ALIVE_NACK";
    } else if (type == (unsigned char) 0x13) {
        packet_string = "ALIVE_REJ";
    } else {
        packet_string = "ERROR";
    }

    return packet_string;
}

void signup_on_server() {
    for (int reg_processes_without_ack_received = 0; reg_processes_without_ack_received < Q;
         reg_processes_without_ack_received++) {
        change_client_state("DISCONNECTED");
        for (int register_reqs_sent = 0; register_reqs_sent < P; register_reqs_sent++) {
            struct Package register_req;
            register_req = construct_register_request_package();
            send_package_via_udp_to_server(register_req, "SIGNUP");
            change_client_state("WAIT_REG");
            struct Package package_received;
            package_received = receive_package_via_udp_from_server(
                    get_waiting_time_after_sent(register_reqs_sent));
            printf("sleeping %ld secs and %ld usecs\n", sockets.udp_timeout.tv_sec, sockets.udp_timeout.tv_usec);
            sleep(sockets.udp_timeout.tv_sec);
            usleep(sockets.udp_timeout.tv_usec);
            if (package_received.type == get_packet_type_from_string("REGISTER_REJ")) {
                change_client_state("DISCONNECTED");
                exit(1);
            } else if (package_received.type == get_packet_type_from_string("REGISTER_NACK")) {
                break;
            } else if (package_received.type == get_packet_type_from_string("REGISTER_ACK")) {
                change_client_state("REGISTERED");
                save_register_ack_data(package_received);
                return;
            } /* else: NO_ANSWER -> Keep trying to contact server, keep looping */
            if (debug_mode) {
                print_message("DEBUG -> No answer received for REGISTER_REQ\n");
                print_message("DEBUG -> Trying to reach server again...\n");
            }
        }
        sleep(S);
        if (debug_mode) {
            char message[75];
            printf("\n");
            sprintf(message, "DEBUG -> Starting new signup process. Current tries: %d / %d\n",
                    reg_processes_without_ack_received + 1, Q);
            print_message(message);
        }

    }
    print_message("ERROR -> Could not contact server during SIGNUP\n");
    print_message("ERROR -> Maximum tries to contact server without REGISTER_ACK received reached\n");
    exit(1);
}

void setup_UDP_socket() {
    struct hostent *ent;
    struct sockaddr_in addr_cli;

    /* get server identity */
    ent = gethostbyname(server_data.server_name_or_address);
    if (!ent) {
        print_message("ERROR -> Can't find server on trying to setup UDP socket\n");
        exit(1);
    }

    /* create INET+DGRAM socket -> UDP */
    sockets.udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockets.udp_socket < 0) {
        print_message("ERROR -> Could not create UDP socket\n");
        exit(1);
    }

    /* fill the structure with the addresses where we will bind the client (any local address) */
    memset(&addr_cli, 0, sizeof(struct sockaddr_in));
    addr_cli.sin_family = AF_INET;
    addr_cli.sin_addr.s_addr = htonl(INADDR_ANY);
    addr_cli.sin_port = htons(0);

    /* bind */
    if (bind(sockets.udp_socket, (struct sockaddr *) &addr_cli, sizeof(struct sockaddr_in)) < 0) {
        print_message("ERROR -> Could not bind UDP socket\n");
        exit(1);
    }

    /* fill the structure of the server's address where we will send the data */
    memset(&sockets.udp_addr_server, 0, sizeof(struct sockaddr_in));
    sockets.udp_addr_server.sin_family = AF_INET;
    sockets.udp_addr_server.sin_addr.s_addr = (((struct in_addr *) ent->h_addr_list[0])->s_addr);
    sockets.udp_addr_server.sin_port = htons(sockets.udp_port);

}

struct Package construct_register_request_package() {
    struct Package register_req;

    /* fill Package */
    register_req.type = get_packet_type_from_string("REGISTER_REQ");
    strcpy(register_req.dev_name, device_data.dev_name);
    strcpy(register_req.mac_address, device_data.dev_mac);
    strcpy(register_req.dev_random_num, "000000");
    strcpy(register_req.data, "");

    return register_req;
}

void send_package_via_udp_to_server(struct Package package_to_send, char *currentFunction) {
    int a = sendto(sockets.udp_socket, &package_to_send, sizeof(package_to_send), 0,
                   (struct sockaddr *) &sockets.udp_addr_server, sizeof(sockets.udp_addr_server));
    char message[150];
    if (a < 0) {
        sprintf(message, "ERROR -> Could not send package via UDP socket during %s\n", currentFunction);
        print_message(message);
    } else if (debug_mode) {
        sprintf(message,
                "DEBUG -> Sent %s;\n"
                "\t\t\tBytes:%lu,\n"
                "\t\t\tname:%s,\n "
                "\t\t\tmac:%s,\n"
                "\t\t\trand num:%s,\n"
                "\t\t\tdata:%s\n\n",
                get_packet_string_from_type(package_to_send.type), sizeof(package_to_send),
                package_to_send.dev_name, package_to_send.mac_address, package_to_send.dev_random_num,
                package_to_send.data);
        print_message(message);
    }
}

int get_waiting_time_after_sent(int reg_reqs_sent) { /* note: reg_reqs_sent starts at 0 */
    if (reg_reqs_sent >= N - 1) {
        int times = 2 + (reg_reqs_sent + 1 - N);
        if (times > M) {
            times = M;
        }
        return times * T;
    }
    return T;
}


struct Package receive_package_via_udp_from_server(int max_timeout) {
    fd_set rfds;
    char *buf = malloc(sizeof(struct Package));
    struct Package *package_received = malloc(sizeof(struct Package));

    FD_ZERO(&rfds); /* clears set */
    FD_SET(sockets.udp_socket, &rfds); /* add socket descriptor to set */
    sockets.udp_timeout.tv_sec = max_timeout;
    sockets.udp_timeout.tv_usec = 0;
    /* if any data is in socket */
    if (select(sockets.udp_socket + 1, &rfds, NULL, NULL, &sockets.udp_timeout) > 0) {
        /* receive from socket */
        int a;
        a = recvfrom(sockets.udp_socket, buf, sizeof(struct Package), 0, (struct sockaddr *) 0, (socklen_t *) 0);
        if (a < 0) {
            print_message("ERROR -> Could not receive from UDP socket\n");
        } else {
            package_received = (struct Package *) buf;
            if (debug_mode) {
                char message[200];
                sprintf(message,
                        "DEBUG -> Received %s;\n"
                        "\t\t\t    Bytes:%lu,\n"
                        "\t\t\t    name:%s,\n "
                        "\t\t\t    mac:%s,\n"
                        "\t\t\t    rand num:%s,\n"
                        "\t\t\t    data:%s\n\n",
                        get_packet_string_from_type((unsigned char) (*package_received).type),
                        sizeof(*package_received), (*package_received).dev_name,
                        (*package_received).mac_address, (*package_received).dev_random_num,
                        (*package_received).data);
                print_message(message);
            }
        }
    }
    return *(package_received);
}

/* Saves REG_ACK data from server which will be used to open a new
   TCP connection on setup_TCP_socket function */
void save_register_ack_data(struct Package package_received) {
    strcpy(device_data.dev_random_num, package_received.dev_random_num);
    sockets.tcp_port = atoi(package_received.data);
}

void setup_TCP_socket() {
    struct hostent *ent;

    /* gets server identity */
    ent = gethostbyname(server_data.server_name_or_address);
    if (!ent) {
        print_message("ERROR -> Can't find server on trying to setup TCP socket\n");
        exit(1);
    }

    /* create INET+STREAM socket -> TCP */
    sockets.tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (sockets.tcp_socket < 0) {
        print_message("ERROR -> Could not create TCP socket\n");
        exit(1);
    }

    /* fill the structure of the server's address where we will send the data */
    memset(&sockets.tcp_addr_server, 0, sizeof(struct sockaddr_in));
    sockets.tcp_addr_server.sin_family = AF_INET;
    sockets.tcp_addr_server.sin_addr.s_addr = (((struct in_addr *) ent->h_addr_list[0])->s_addr);
    sockets.tcp_addr_server.sin_port = htons(sockets.tcp_port);

    if (connect(sockets.tcp_socket, (struct sockaddr *) &sockets.tcp_addr_server,
                sizeof(sockets.tcp_addr_server)) < 0) {
        print_message("ERROR -> Could not connect TCP socket\n");
        exit(1);
    }

}

void *manage_command_line_input() {
    while (1) {
        int max_chars_to_read = 50;
        char *command = read_from_stdin(max_chars_to_read);
        if (strcmp(command, "\0") == 0) { continue; }
        if (strcmp(command, "quit") == 0) {
            pthread_exit(NULL);
            /* TO DO
            }  else if (strcmp(command, "send-conf") == 0) {

            } else if (strcmp(command, "get-conf") == 0) {
            */
        } else {
            char message[150];
            sprintf(message, "ERROR -> %s is not an accepted command\n", command);
            print_message(message);
            print_accepted_commands();
        }
    }
}

char *read_from_stdin(int max_chars_to_read) {
    char buffer[max_chars_to_read];
    if (fgets(buffer, max_chars_to_read, stdin) != NULL) {
        buffer[strcspn(buffer, "\n")] = '\0';
    }
    char *buffer_pointer = malloc(max_chars_to_read);
    strcpy(buffer_pointer, buffer);
    return buffer_pointer;
}

void print_accepted_commands() {
    print_message("INFO -> Accepted commands are: \n");
    printf("\t\t   quit -> finishes client\n");
    printf("\t\t   send-conf -> sends conf file to server via TCP\n");
    printf("\t\t   get-conf -> receives conf file from server via TCP\n");
}

void *keep_in_touch_with_server() {
    int alives_inf_sent_without_answer = 0;
    while (1) {
        struct Package alive_inf = construct_alive_inf_package();
        send_package_via_udp_to_server(alive_inf, "KEEP IN TOUCH");
        struct Package server_answer = receive_package_via_udp_from_server(R);
        printf("sleeping %ld secs and %ld usecs\n", sockets.udp_timeout.tv_sec, sockets.udp_timeout.tv_usec);
        sleep(sockets.udp_timeout.tv_sec);
        usleep(sockets.udp_timeout.tv_usec);

        if (server_answer.type == get_packet_type_from_string("ALIVE_ACK")) {
            change_client_state("ALIVE");
            alives_inf_sent_without_answer = 0;
        } else if (server_answer.type == get_packet_type_from_string("ALIVE_REJ")) {
            alives_inf_sent_without_answer = 0;
            if (strcmp(client_state, "ALIVE") == 0) {
                print_message("INFO -> Potential identity breach: Got ALIVE_REJ package when state was ALIVE\n");
                pthread_cancel(tids[1]);
                change_client_state("DISCONNECTED");
                signup_on_server();
                break;
            }
        } else { /* no answer */
            alives_inf_sent_without_answer++;
            if (debug_mode) {
                char message[150];
                sprintf(message, "DEBUG -> Have not received ALIVE_ACK. Current tries %d / %d\n",
                        alives_inf_sent_without_answer, U);
                print_message(message);
            }
            if (alives_inf_sent_without_answer == U) {
                print_message("Maximum tries to contact server without ALIVE_ACK received reached\n");
                pthread_cancel(tids[1]);
                change_client_state("DISCONNECTED");
                signup_on_server();
                break;
            }
        }
    }
    return NULL;
}

struct Package construct_alive_inf_package() {
    struct Package alive_inf;

    /* fill Package */
    alive_inf.type = get_packet_type_from_string("ALIVE_INF");
    strcpy(alive_inf.dev_name, device_data.dev_name);
    strcpy(alive_inf.mac_address, device_data.dev_mac);
    strcpy(alive_inf.dev_random_num, device_data.dev_random_num);
    strcpy(alive_inf.data, "");

    return alive_inf;
}
