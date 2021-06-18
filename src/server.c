#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/ioctl.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <stdint.h>

#define MAX_SOCKETS 4
#define CLIENT 3
#define NODE 4

// Struct for nodes
typedef struct
{
  int socket;
  pthread_mutex_t *lock;
  int current_jobs;
  int position;
  bool dead;
} ConnectionContainer;

// Global variables
int current_connection_count = 0;
ConnectionContainer *main_container[256];
sem_t sem;
pthread_mutex_t lock;
int filecounter;
int socket_desc;

int intHandler(int dummy)
{
  send_kill_signals();
  close(socket_desc);
  exit(0);
}

void send_kill_signals()
{
  printf("Sending kill signals to connected nodes...\n");
  for (size_t i = 0; i < current_connection_count; i++)
  {
    if (main_container[i]->dead)
      continue;
    send_kill_signal(main_container[i]->socket, i);
  }
}

void send_kill_signal(int socket, int i)
{
  printf("Killing node #%i...\n", i);
  printf("KILLING SOCKET %i\n", socket);
  shutdown(socket, SHUT_WR);
  close(socket);
}

// Main function to handle new connections
void *connection_handler(void *socket_desc_void)
{
  int socket = (uintptr_t)socket_desc_void;
  // Initialize variables
  int connection_type = 0, size_received;

  // Get received message (connection ID)
  size_received = read(socket, &connection_type, sizeof(int), MSG_WAITALL);
  if (size_received <= 0)
  {
    puts("DISCONNECTED");
    close(socket);
    return;
  }

  if (connection_type == CLIENT)
  {
    puts("Received connection from CLIENT");
    receive_image(socket);
  }
  else if (connection_type == NODE)
  {
    puts("Received connection from NODE");
    setup_node(socket);
  }
  else
  {
    printf("Received unhandled connection type: %i\n", connection_type);
  }
  return 0;
}

// Runs in loop to parse new received commands
void parse_node_command(int socket, ConnectionContainer *connection_container)
{
  char current_command;
  int size_received;

  printf("[Node #%i] Jobs No: %i\n", connection_container->position, connection_container->current_jobs);

  printf("[Node #%i] Waiting for command...\n", connection_container->position);

  // Check if disconnected
  size_received = read(socket, &current_command, sizeof(char), MSG_WAITALL);
  if (size_received <= 0)
  {
    printf("[Node #%i]  Disconnected\n", connection_container->position);
    connection_container->dead = true;
    close(socket);
    return;
  }

  printf("[Node #%i] Command received: %c\n", connection_container->position, current_command);

  // Receiving CURRENT NODE LOAD
  if (current_command == 'B')
  {
    int current_load;
    read(socket, &current_load, sizeof(int), MSG_WAITALL);
    connection_container->current_jobs = current_load;
  }
  else
  {
    printf("[Node #%i] Unhandled command received.\n", connection_container->position);
  }

  parse_node_command(socket, connection_container);
}

void setup_node(int socket)
{
  // Creates a new container and a pointer to it
  ConnectionContainer new_conn;
  ConnectionContainer *new_conn_ptr = &new_conn;
  // Set socket
  new_conn.socket = (uintptr_t)socket;
  // Create, set and init mutex lock
  new_conn.lock = pthread_mutex_lock;
  pthread_mutex_t *plock;
  plock = malloc(sizeof(pthread_mutex_t));
  new_conn_ptr->lock = plock;
  if (pthread_mutex_init(new_conn.lock, NULL) != 0)
  {
    printf("\n mutex init failed\n");
    return 1;
  }
  // Rest of container init
  new_conn.current_jobs = 0;
  new_conn.dead = false;
  new_conn.position = current_connection_count;
  main_container[current_connection_count++] = new_conn_ptr;

  // Send initial signal
  char buffer = 'A';
  int stat;

  do
  {
    stat = write(socket, (void *)&buffer, sizeof(char));
  } while (stat < 0);

  parse_node_command(socket, new_conn_ptr);
}

int receive_image(int socket)
{

  int recv_size = 0, size = 0, read_size, write_size, packet_index = 1, stat;

  char imagearray[10241];
  FILE *image;

  //Find the size of the image
  do
  {
    stat = read(socket, &size, sizeof(int));
  } while (stat < 0);

  // printf("Packet received.\n");
  // printf("Packet size: %i\n", stat);
  // printf("Image size: %i\n", size);
  // printf(" \n");

  char buffer[] = "Got it";

  //Send our verification signal
  do
  {
    stat = write(socket, &buffer, sizeof(int));
  } while (stat < 0);

  printf("Reply sent\n");
  printf(" \n");

  int fcounter = 0;

  pthread_mutex_lock(&lock);
  fcounter = filecounter++;
  pthread_mutex_unlock(&lock);

  char *file_name_string;
  asprintf(&file_name_string, "received_image_%d.png", fcounter);

  image = fopen(file_name_string, "w");

  if (image == NULL)
  {
    printf("Error has occurred. Image file could not be opened\n");
    return -1;
  }

  //Loop while we have not received the entire file yet

  struct timeval timeout = {10, 0};

  fd_set fds;
  int buffer_fd;

  while (recv_size < size)
  {
    //while(packet_index < 2){

    FD_ZERO(&fds);
    FD_SET(socket, &fds);

    buffer_fd = select(FD_SETSIZE, &fds, NULL, NULL, &timeout);

    if (buffer_fd < 0)
      printf("error: bad file descriptor set.\n");

    if (buffer_fd == 0)
      printf("error: buffer read timeout expired.\n");

    if (buffer_fd > 0)
    {
      do
      {
        read_size = read(socket, imagearray, 1024);
      } while (read_size < 0);

      // printf("Packet number received: %i\n", packet_index);
      // printf("Packet size: %i\n", read_size);

      //Write the currently read data into our image file
      write_size = fwrite(imagearray, 1, read_size, image);
      // printf("Written image size: %i\n", write_size);

      if (read_size != write_size)
      {
        printf("error in read write\n");
      }

      //Increment the total number of bytes read
      recv_size += read_size;
      packet_index++;
      // printf("Total received image size: %i\n", recv_size);
      // printf(" \n");
      // printf(" \n");
    }
  }
  printf("image %d done\n", fcounter);
  fclose(image);
  // printf("Image successfully Received!\n");
  return 1;
}

int main(int argc, char *argv[])
{
  signal(SIGINT, intHandler);
  sem_init(&sem, 0, MAX_SOCKETS);

  filecounter = 0;
  if (pthread_mutex_init(&lock, NULL) != 0)
  {
    printf("\n mutex init failed\n");
    return 1;
  }

  int new_socket, c;
  struct sockaddr_in server, client;

  //Create socket
  socket_desc = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_desc == -1)
  {
    printf("Could not create socket");
  }

  //Prepare the sockaddr_in structure
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons(8889);

  //Bind
  if (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0)
  {
    puts("bind failed");
    return 1;
  }

  puts("bind done");

  //Listen
  listen(socket_desc, 3);

  //Accept and incoming connection
  puts("Waiting for incoming connections...");
  c = sizeof(struct sockaddr_in);

  pthread_t thread_id;

  while ((new_socket = accept(socket_desc, (struct sockaddr *)&client, (socklen_t *)&c)))
  {
    puts("Connection accepted");

    fflush(stdout);

    if (new_socket < 0)
    {
      perror("Accept Failed");
      return 1;
    }

    int *new_socket_ptr;
    new_socket_ptr = (int *)malloc(sizeof(int));
    *new_socket_ptr = new_socket;
    printf("newsocket %d\n", new_socket);
    printf("newsocket_ptr %d\n", new_socket_ptr);

    int thread_create_result;
    sem_wait(&sem);
    thread_create_result = pthread_create(&thread_id, NULL, connection_handler, (void *)(uintptr_t)new_socket);
    sem_post(&sem);

    if (thread_create_result < 0)
    {
      free(new_socket_ptr);
      perror("could not create thread");
      return 1;
    }
  }

  // close(socket_desc);
  fflush(stdout);
  close(socket_desc);

  return 0;
}