#include <stdio.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <errno.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define NODE_CONNECTION_TYPE 4
struct image_threads_arguments
{
  char *filename;
  int key;
  char *output;
};

void get_pixel(stbi_uc *image, size_t imageWidth, size_t x, size_t y, stbi_uc *r, stbi_uc *g, stbi_uc *b)
{
  *r = image[4 * (y * imageWidth + x) + 0];
  *g = image[4 * (y * imageWidth + x) + 1];
  *b = image[4 * (y * imageWidth + x) + 2];
}

void set_pixel(stbi_uc *image, size_t imageWidth, size_t x, size_t y, stbi_uc r, stbi_uc g, stbi_uc b)
{
  image[4 * (y * imageWidth + x) + 0] = r;
  image[4 * (y * imageWidth + x) + 1] = g;
  image[4 * (y * imageWidth + x) + 2] = b;
}

void *process_image(void *arguments_input)
{
  struct image_threads_arguments *args = arguments_input;

  int width, height;
  stbi_uc *image = stbi_load(args->filename, &width, &height, NULL, 4);
  printf("w: %i\n", width);
  printf("h: %i\n", height);

  stbi_uc r, g, b;

  for (int x = 0; x < width; x++)
  {
    for (int y = 0; y < height; y++)
    {
      get_pixel(image, width, x, y, &r, &g, &b);

      stbi_uc r_new, g_new, b_new;
      r_new = r ^ args->key;
      g_new = g ^ args->key;
      b_new = b ^ args->key;

      set_pixel(image, width, x, y, r_new, g_new, b_new);
    }
  }

  stbi_write_png(args->output, width, height, 4, image, 4 * width);
  return NULL;
}

int send_message(int socket, void *buffer, size_t size)
{
  int stat;

  do
  {
    stat = write(socket, &buffer, size);
  } while (stat < 0);

  return stat;
}

void parse_command(int socket, int current_image_count)
{
  char current_command = 'Z';
  int stat;
  puts("WAITING FOR COMMAND");
  stat = read(socket, &current_command, sizeof(char), MSG_WAITALL);
  if (stat <= 0)
  {
    printf("Disconnected");
    close(socket);
    return;
  }
  printf("COMMAND RECEIVED: %c\n", current_command);

  puts("PArsing command");
  if (current_command == 'A')
  {
    puts("Received A command, sending current image count\n");
    printf("Image count: %i\n", current_image_count);
    send_message(socket, 'B', sizeof(char));
    puts("SEG1");
    send_message(socket, current_image_count, sizeof(int));
    puts("SEG2");
  }
  else
  {
    puts("Received invalid command");
  }

  parse_command(socket, current_image_count);
}

int main()
{
  // printf() displays the string inside quotation
  printf("Hello, World!\n");

  int socket_desc;
  int connection_id = NODE_CONNECTION_TYPE;
  struct sockaddr_in server;

  //Create socket
  socket_desc = socket(AF_INET, SOCK_STREAM, 0);

  if (socket_desc == -1)
  {
    printf("Could not create socket");
  }

  memset(&server, 0, sizeof(server));
  server.sin_addr.s_addr = inet_addr("127.0.0.1");
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

  puts("Connected\n");

  //Send client type
  printf("Sending Connection Identifier (4)\n");
  write(socket_desc, (void *)&connection_id, sizeof(int));

  int size, stat;

  int current_image_count = 92;

  do
  { //Read while we get errors that are due to signals.
    puts("EJECUTANDO CONDICIONAL");
    puts("INICIANDO ESPERA");
    parse_command(socket_desc, current_image_count);
  } while (stat < 0);

  struct image_threads_arguments *arguments = (struct image_threads_arguments *)malloc(sizeof(struct image_threads_arguments));
  arguments->filename = "received_image.png";
  arguments->key = 2145;
  arguments->output = "output2.png";

  pthread_t thread_id;
  printf("Before Thread\n");
  pthread_create(&thread_id, NULL, process_image, (void *)arguments);
  pthread_join(thread_id, NULL);
  printf("After Thread\n");

  close(socket_desc);

  return 0;
}
