#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define CLIENT_TYPE 3

void send_image(int socket)
{

    FILE *picture;
    int size, read_size, stat, packet_index;
    char send_buffer[10240], read_buffer[256];
    packet_index = 1;
    int connection_id = CLIENT_TYPE;

    picture = fopen("lenna.png", "rb");
    printf("Getting Picture Size\n");

    if (picture == NULL)
    {
        printf("Error Opening Image File");
    }

    fseek(picture, 0, SEEK_END);
    size = ftell(picture);
    fseek(picture, 0, SEEK_SET);
    printf("Total Picture size: %i\n", size);

    //Send client type
    printf("Sending Connection Identifier (3)\n");
    write(socket, (void *)&connection_id, sizeof(int));

    //Send Picture Size
    printf("Sending Picture Size\n");
    write(socket, (void *)&size, sizeof(int));

    //Send Picture as Byte Array
    printf("Sending Picture as Byte Array\n");

    do
    { //Read while we get errors that are due to signals.
        stat = read(socket, &read_buffer, 255);
        printf("Bytes read: %i\n", stat);
    } while (stat < 0);

    printf("Received data in socket\n");
    printf("Socket data: %s\n", read_buffer);

    while (!feof(picture))
    {
        //while(packet_index = 1){
        //Read from the file into our send buffer
        read_size = fread(send_buffer, 1, sizeof(send_buffer) - 1, picture);

        //Send data through our socket
        do
        {
            stat = write(socket, send_buffer, read_size);
        } while (stat < 0);

        printf("Packet Number: %i\n", packet_index);
        printf("Packet Size Sent: %i\n", read_size);
        printf(" \n");
        printf(" \n");

        packet_index++;

        //Zero out our send buffer
        bzero(send_buffer, sizeof(send_buffer));
    }
}

int main(int argc, char *argv[])
{

    if(argc >= 3){
        int i;
        for ( i = 0; i < argc; i++)
        {
            printf("arg %d:%s \n", i, argv[i]);
        }
    }

    int socket_desc;
    struct sockaddr_in server;

    //Create socket
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);

    if (socket_desc == -1)
    {
        printf("Could not create socket");
    }
    printf("P1\n");
    memset(&server, 0, sizeof(server));
    server.sin_addr.s_addr = inet_addr("127.0.0.1");
    server.sin_family = AF_INET;
    server.sin_port = htons(8889);
    printf("P2\n");
    //Connect to remote server
    if (connect(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        printf("Error: %s\n", strerror(errno));
        close(socket_desc);
        puts("Connect Error");
        return 1;
    }
    printf("P3\n");
    puts("Connected\n");

    send_image(socket_desc);

    close(socket_desc);

    return 0;
}