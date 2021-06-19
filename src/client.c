#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

#include <semaphore.h>


#define CLIENT_TYPE 3
#define MAX_SOCKETS 4

sem_t sem;


typedef struct
{
    const char *ip;
    const char *filename;
    int key;
} request_data_s;

int validate_params(const char *ip, const char *filename, const char *key, const char *num);

void sent_to_server_thread(void *request_data);

int sent_to_server(const char *ip, const char *filename, int key);
void send_image(int socket, const char *filename, int key);

void sent_to_server_thread(void *request_data)
{
    request_data_s *data = (request_data_s *)request_data;
    sem_wait(&sem);
    printf("DEBUG THE KEY 1 IS: %i\n", data->key);
    sent_to_server(data->ip, data->filename, data->key);
    sem_post(&sem);
}

int validate_params(const char *ip, const char *filename, const char *key, const char *num)
{

    if (access(filename, F_OK) != 0)
    {
        printf("image file doesn't exist\n");
        return 1;
    }
    if (atol(num) == 0)
    {
        printf("quantity number is zero or NaN\n");
        return 1;
    }
    if (atol(key) == 0)
    {
        printf("key number is zero or NaN\n");
        return 1;
    }
    //validad direccion ip
    return 0;
}

int sent_to_server(const char *ip, const char *filename, int key)
{
    int socket_desc;
    struct sockaddr_in server;
    //Create socket
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc == -1)
    {
        printf("Could not create socket");
    }
    memset(&server, 0, sizeof(server));
    server.sin_addr.s_addr = inet_addr(ip);
    server.sin_family = AF_INET;
    server.sin_port = htons(8889);
    //Connect to remote server
    if (connect(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        printf("Error: %s\n", strerror(errno));
        close(socket_desc);
        puts("Connect Error");
        return 1;
    }
    send_image(socket_desc, filename, key);
    close(socket_desc);
    return 0;
}

void send_image(int socket, const char *filename, int key)
{

    FILE *picture;
    int size, read_size, stat, packet_index;
    char send_buffer[10240], read_buffer[256];
    packet_index = 1;
    int connection_id = CLIENT_TYPE;

    picture = fopen(filename, "rb");
    printf("Getting Picture Size\n");

    if (picture == NULL)
    {
        printf("Error Opening Image File");
    }

    fseek(picture, 0, SEEK_END);
    size = ftell(picture);
    fseek(picture, 0, SEEK_SET);

    //Send client type
    write(socket, (void *)&connection_id, sizeof(int));

    printf("WILL SEND KEY: %i\n", key);
    //Send client type
    write(socket, (void *)&key, sizeof(int));

    //Send Picture Size
    write(socket, (void *)&size, sizeof(int));

    do
    { //Read while we get errors that are due to signals.
        stat = read(socket, &read_buffer, 255);
    } while (stat < 0);

    while (!feof(picture))
    {
        //Read from the file into our send buffer
        read_size = fread(send_buffer, 1, sizeof(send_buffer) - 1, picture);

        //Send data through our socket
        do
        {
            stat = write(socket, send_buffer, read_size);
        } while (stat < 0);

        packet_index++;

        //Zero out our send buffer
        bzero(send_buffer, sizeof(send_buffer));
    }
    printf("ok \n");
}

int main(int argc, char *argv[])
{
    sem_init(&sem, 0, MAX_SOCKETS);

    if (argc >= 3)
    {
        int i;
        for (i = 0; i < argc; i++)
        {
            printf("arg %d :%s \n", i, argv[i]);
        }
    }
    else
    {
        printf("not enough arguments");
        return 1;
    }
    const char *ip = argv[1];
    const char *filename = argv[2];
    const char *key = argv[3];
    const char *num = argv[4];

    if (validate_params(ip, filename, key, num))
    {
        return 1;
    }
    else
    {
        int i;
        int num_int = atol(num);
        int key_int = atol(key);

        request_data_s data;
        data.filename = filename;
        data.ip = ip;
        data.key = key_int;
        pthread_t th_ids[num_int];
        int rc;
        for (i = 0; i < num_int; i++)
        {
            // sleep(1);
            rc = pthread_create(&th_ids[i], NULL, sent_to_server_thread, (void *)&data);
            if (rc)
            {
                printf("Error:unable to create thread, %d\n", rc);
                exit(-1);
            }
            printf("thread create %d \n", i);
            // sent_to_server(ip, filename);
        }
        void *ret;
        int j;
        for (j = 0; j< num_int; j++)
        {
            printf("num_int %d of %d\n",j+1,num_int);
            pthread_join(th_ids[j], &ret);
            printf("thread join %d \n", j);
        }
    }

    // atol(argv[3]);

    return 0;
}