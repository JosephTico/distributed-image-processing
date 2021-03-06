#define _GNU_SOURCE
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
#include <unistd.h>
#include <stdatomic.h>
#include "fort.h"
#include "io_queue.h"

#define MAX_SOCKETS 4
#define CLIENT 3
#define NODE 4
#define MAX_NODES 256
#define MAX_IMAGE_FILENAME 512
#define CONCURRENT_IMAGES_PER_NODE 3
// Struct for nodes
typedef struct
{
  int socket;
  pthread_mutex_t lock;
  atomic_int current_jobs;
  int position;
  bool dead;
  const int *last_images[CONCURRENT_IMAGES_PER_NODE]
} ConnectionContainer;

// Global variables
ConnectionContainer *main_container[MAX_NODES];
sem_t sem;
pthread_mutex_t global_lock;
int socket_desc;
IoQueue image_queue;
IoQueue key_queue;
_Atomic int filecounter;
_Atomic int current_connection_count = 0;
_Atomic int image_queue_size = 0;

// Headers
void intHandler(int dummy);
void send_kill_signals();
void send_kill_signal(int socket, int i);
void *connection_handler(void *socket_desc_void);
void parse_node_command(int socket, ConnectionContainer *connection_container);
void setup_node(int socket);
int receive_image(int socket);
int get_available_node();
bool send_image_to_distributed_nodes(char *filename, int key, bool add_to_queue);
void send_message_to_node(int node, void *buffer, size_t size);
void send_image_to_node(int node, char *filename, int key);
char *process_image_in_queue();
void *queue_handler();
void append_image_to_queue(char *filename, int key);
void print_nodes_info();

// Handle SIGINT
void intHandler(int dummy)
{
  send_kill_signals();
  close(socket_desc);
  exit(0);
}

void send_kill_signals()
{
  printf("[Info] Sending kill signals to connected nodes...\n");
  for (size_t i = 0; i < current_connection_count; i++)
  {
    if (main_container[i]->dead)
      continue;
    send_kill_signal(main_container[i]->socket, i);
  }
}

void send_kill_signal(int socket, int i)
{
  printf("[Node #%i] Sending kill signal...\n", i);
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
  size_received = recv(socket, &connection_type, sizeof(int), MSG_WAITALL);
  if (size_received <= 0)
  {
    puts("[Warning] Node disconnected before setting-up");
    close(socket);
    return NULL;
  }

  if (connection_type == CLIENT)
  {
    puts("[Info] Received connection from CLIENT");
    receive_image(socket);
  }
  else if (connection_type == NODE)
  {
    puts("[Info] Received connection from NODE");
    setup_node(socket);
  }
  else
  {
    printf("[Warning] Received unhandled connection type: %i\n", connection_type);
  }
  return 0;
}

// Runs in loop to parse new received commands
void parse_node_command(int socket, ConnectionContainer *connection_container)
{
  char current_command;
  int size_received;
  printf("[Node #%i] Waiting for command...\n", connection_container->position);

  // Check if disconnected
  size_received = recv(socket, &current_command, sizeof(char), MSG_WAITALL);
  if (size_received <= 0)
  {
    printf("[Node #%i] Disconnected\n", connection_container->position);
    connection_container->dead = true;
    connection_container->current_jobs = 0;
    close(socket);
    return;
  }

  printf("[Node #%i] Command received: %c\n", connection_container->position, current_command);

  // Receiving number of jobs on node
  if (current_command == 'B')
  {
    int current_load;
    recv(socket, &current_load, sizeof(int), MSG_WAITALL);
    connection_container->current_jobs = current_load;
    printf("[Node #%i] Set number of current jobs to: %i\n", connection_container->position, connection_container->current_jobs);
  }
  // Completed image
  else if (current_command == 'D')
  {
    printf("[Node #%i]  Finished an image\n", connection_container->position);
    connection_container->current_jobs--;
    process_image_in_queue();
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
  ConnectionContainer *new_conn_ptr = malloc(sizeof(ConnectionContainer));
  // Set socket
  new_conn_ptr->socket = (uintptr_t)socket;
  // Create, set and init mutex lock
  if (pthread_mutex_init(&new_conn_ptr->lock, NULL) != 0)
  {
    printf("\n[Error] Mutex init failed for Node #%i\n", current_connection_count);
    return;
  }
  // Send initial signal
  pthread_mutex_lock(&new_conn_ptr->lock);
  char buffer = 'A';
  write(socket, (void *)&buffer, sizeof(char));
  pthread_mutex_unlock(&new_conn_ptr->lock);

  // Rest of container init
  new_conn_ptr->current_jobs = 0;
  new_conn_ptr->dead = false;
  new_conn_ptr->position = current_connection_count;
  main_container[current_connection_count++] = new_conn_ptr;

  // Parse any future command
  parse_node_command(socket, new_conn_ptr);
}

int receive_image(int socket)
{

  int recv_size = 0, size = 0, key = 0, read_size, write_size, packet_index = 1, stat;

  char imagearray[10241];
  FILE *image;

  //Find the keyof the image
  do
  {
    stat = read(socket, &key, sizeof(int));
  } while (stat < 0);

  //Find the size of the image
  do
  {
    stat = read(socket, &size, sizeof(int));
  } while (stat < 0);

  char buffer[] = "Got it";

  //Send our verification signal
  do
  {
    stat = write(socket, &buffer, sizeof(int));
  } while (stat < 0);

  int fcounter = 0;

  pthread_mutex_lock(&global_lock);
  fcounter = filecounter++;
  pthread_mutex_unlock(&global_lock);

  char *file_name_string;
  asprintf(&file_name_string, "processed_img_%d.png", fcounter);

  image = fopen(file_name_string, "w");

  if (image == NULL)
  {
    printf("[Error] Image file could not be opened\n");
    return -1;
  }

  //Loop while we have not received the entire file yet
  struct timeval timeout = {10, 0};

  fd_set fds;
  int buffer_fd;

  while (recv_size < size)
  {

    FD_ZERO(&fds);
    FD_SET(socket, &fds);

    buffer_fd = select(FD_SETSIZE, &fds, NULL, NULL, &timeout);

    if (buffer_fd < 0)
      printf("[Error] Bad file descriptor set.\n");

    if (buffer_fd == 0)
      printf("[Error] Buffer read timeout expired.\n");

    if (buffer_fd > 0)
    {
      do
      {
        read_size = read(socket, imagearray, 1024);
      } while (read_size < 0);

      //Write the currently read data into our image file
      write_size = fwrite(imagearray, 1, read_size, image);

      if (read_size != write_size)
      {
        printf("[Error] Read write failed\n");
      }

      //Increment the total number of bytes read
      recv_size += read_size;
      packet_index++;
    }
  }
  printf("[Info] Image %d received succesfully\n", fcounter);
  fclose(image);

  if (image_queue_size > 0)
  {
    append_image_to_queue(file_name_string, key);
  }
  else
  {
    send_image_to_distributed_nodes(file_name_string, key, true);
  }
  return 1;
}

int get_available_node()
{
  for (size_t i = 0; i < current_connection_count; i++)
  {
    if (!main_container[i]->dead && main_container[i]->current_jobs < CONCURRENT_IMAGES_PER_NODE)
      return i;
  }
  return -1;
}

bool send_image_to_distributed_nodes(char *filename, int key, bool add_to_queue)
{
  printf("[Info] Trying to send '%s' to nodes\n", filename);
  int node = get_available_node();

  if (node < 0)
  {
    if (add_to_queue)
      append_image_to_queue(filename, key);
    return false;
  }
  else
  {
    send_image_to_node(node, filename, key);
    return true;
  }
}

void send_message_to_node(int node, void *buffer, size_t size)
{
  pthread_mutex_lock(&main_container[node]->lock);
  int stat;
  stat = write(main_container[node]->socket, &buffer, size);
  if (stat < 0)
  {
    printf("[Node #%i] Error sending message: %s\n", node, strerror(errno));
  }
  pthread_mutex_unlock(&main_container[node]->lock);
}

void send_image_to_node(int node, char *filename, int key)
{
  main_container[node]->current_jobs++;

  pthread_mutex_lock(&main_container[node]->lock);

  char operation = 'I';

  printf("[Node #%i] Sending image '%s' with key '%i'\n", node, filename, key);  

  write(main_container[node]->socket, (void *)&operation, sizeof(char));
  write(main_container[node]->socket, (void *)filename, sizeof(char) * MAX_IMAGE_FILENAME);
  write(main_container[node]->socket, (void *)&key, sizeof(int));

  FILE *picture;
  int size, stat, read_size;
  char send_buffer[10240];

  picture = fopen(filename, "rb");
  fseek(picture, 0, SEEK_END);
  size = ftell(picture);
  fseek(picture, 0, SEEK_SET);
  write(main_container[node]->socket, (void *)&size, sizeof(int));

  while (!feof(picture))
  {
    //Read from the file into our send buffer
    read_size = fread(send_buffer, 1, sizeof(send_buffer) - 1, picture);

    //Send data through our socket
    write(main_container[node]->socket, send_buffer, read_size);

    //Zero out our send buffer
    bzero(send_buffer, sizeof(send_buffer));
  }

  fclose(picture);

  remove(filename);
  
  pthread_mutex_unlock(&main_container[node]->lock);
}

void append_image_to_queue(char *filename, int key)
{
  printf("TRYING TO APPEND TO QUEUE\n");
  io_queue_push(&image_queue, filename);
  io_queue_push(&key_queue, &key);
  image_queue_size++;
  printf("[Info] Appended '%s' to queue\n", filename);
}

char *process_image_in_queue()
{
  char out[MAX_IMAGE_FILENAME];
  int key;
  if (io_queue_has_front(&image_queue) == IO_QUEUE_RESULT_TRUE && image_queue_size > 0)
  {

    io_queue_front(&image_queue, &out);
    io_queue_front(&key_queue, &key);
    bool did_send = send_image_to_distributed_nodes(out, key, false);
    if (did_send)
    {
      io_queue_pop(&image_queue);
      io_queue_pop(&key_queue);
      image_queue_size--;
    }
  }
  return out;
}

void print_nodes_info()
{
  ft_table_t *table = ft_create_table();
  /* Set "header" type for the first row */
  ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_ROW_TYPE, FT_ROW_HEADER);

  ft_write_ln(table, "#", "Jobs");

  for (size_t i = 0; i < current_connection_count; i++)
  {
    if (main_container[i]->dead)
      continue;
    ft_printf_ln(table, "%li|%i", i, main_container[i]->current_jobs);
  }

  printf("%s\n", ft_to_string(table));
  ft_destroy_table(table);
}

void *queue_handler()
{
  print_nodes_info();
  printf("[Info] There are %i images in queue.\n", image_queue_size);
  process_image_in_queue();
  sleep(2);
  queue_handler();
  return NULL;
}

int main(int argc, char *argv[])
{
  signal(SIGINT, intHandler);
  sem_init(&sem, 0, MAX_SOCKETS);
  int new_socket, c;
  struct sockaddr_in server, client;
  io_queue_init(&image_queue, sizeof(char) * MAX_IMAGE_FILENAME);
  io_queue_init(&key_queue, sizeof(int));
  filecounter = 0;

  if (pthread_mutex_init(&global_lock, NULL) != 0)
  {
    printf("\n[Error] Global mutex init failed\n");
    return 1;
  }

  //Create socket
  socket_desc = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_desc == -1)
  {
    printf("[Error] Could not create socket");
  }

  //Prepare the sockaddr_in structure
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  const int port = 8889;
  server.sin_port = htons(port);

  //Bind
  if (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0)
  {
    printf("[Error] Can't start server on port %i, please make sure it's available.", port);
    return 1;
  }

  puts("[Welcome] Image distribution server started succesfully");

  //Listen
  listen(socket_desc, 3);

  //Accept and incoming connection
  puts("[Info] Waiting for incoming connections...");
  c = sizeof(struct sockaddr_in);

  pthread_t queue_thread_id;
  int queue_thread = pthread_create(&queue_thread_id, NULL, queue_handler, NULL);

  pthread_t thread_id;

  while ((new_socket = accept(socket_desc, (struct sockaddr *)&client, (socklen_t *)&c)))
  {
    puts("[Info] New connection accepted");

    fflush(stdout);

    if (new_socket < 0)
    {
      perror("[Error] Failed accepting a new connection");
      return 1;
    }

    int *new_socket_ptr;
    new_socket_ptr = (int *)malloc(sizeof(int));
    *new_socket_ptr = new_socket;

    int thread_create_result;
    sem_wait(&sem);
    thread_create_result = pthread_create(&thread_id, NULL, connection_handler, (void *)(uintptr_t)new_socket);
    sem_post(&sem);

    if (thread_create_result < 0)
    {
      free(new_socket_ptr);
      perror("[Error] Could not create thread");
      return 1;
    }
  }

  // close(socket_desc);
  fflush(stdout);
  close(socket_desc);

  return 0;
}