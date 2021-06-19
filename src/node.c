#include <stdio.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <stdatomic.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define NODE_CONNECTION_TYPE 4
#define MAX_IMAGE_FILENAME 512

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

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

void process_image(char *filename, int key, char *output)
{

  int width, height;
  printf("PROCESSING FILENAME: %s\n", filename);
  stbi_uc *image = stbi_load(filename, &width, &height, NULL, 4);
  printf("w: %i\n", width);
  printf("h: %i\n", height);

  stbi_uc r, g, b;

  for (int x = 0; x < width; x++)
  {
    for (int y = 0; y < height; y++)
    {
      get_pixel(image, width, x, y, &r, &g, &b);

      stbi_uc r_new, g_new, b_new;
      r_new = r ^ key;
      g_new = g ^ key;
      b_new = b ^ key;

      set_pixel(image, width, x, y, r_new, g_new, b_new);
    }
  }

  stbi_write_png(filename, width, height, 4, image, 4 * width);
  return NULL;
}

int send_message(int socket, void *buffer, size_t size)
{
  int stat;

  stat = write(socket, &buffer, size);

  if (stat < 0)
  {
    printf("Error sending message: %s\n", strerror(errno));
  }

  return stat;
}

void parse_command(int socket, int current_image_count)
{
  char current_command = 'Z';
  int stat;
  puts("WAITING FOR COMMAND");
  stat = recv(socket, &current_command, sizeof(char), MSG_WAITALL);
  if (stat <= 0)
  {
    printf("Disconnected from server.\n");
    close(socket);
    return;
  }
  printf("COMMAND RECEIVED: %c\n", current_command);

  puts("Parsing command");
  if (current_command == 'A')
  {
    puts("Received A command, sending current image count\n");
    printf("Image count: %i\n", current_image_count);
    send_message(socket, (void *)'B', sizeof(char));
    send_message(socket, current_image_count, sizeof(int));
  }
  else if (current_command == 'I')
  {
    puts("Received a new image");
    current_image_count++;

    // Receive filename
    char filename[MAX_IMAGE_FILENAME];
    recv(socket, &filename, sizeof(char) * MAX_IMAGE_FILENAME, MSG_WAITALL);

    // Receive key
    int key;
    recv(socket, &key, sizeof(int), MSG_WAITALL);

    printf("Received filename: %s, key: %i\n", filename, key);

    // Receive and process the actual image
    receive_image(socket, filename, key);

    // Send (D)one message
    send_message(socket, (void *)'D', sizeof(char));
    current_image_count--;
  }
  else
  {
    puts("Received invalid command");
  }

  parse_command(socket, current_image_count);
}

int receive_image(int socket, char *file_name_string, int key)
{

  int recv_size = 0, size = 0, read_size, write_size, packet_index = 1, stat;

  char imagearray[10241];
  FILE *image;

  //Find the size of the image
  stat = recv(socket, &size, sizeof(int), MSG_WAITALL);
  if (stat <= 0) {
    printf("Error receiving message: %s\n", strerror(errno));
    return -1;
  }

  printf("OBTAINED SIZE: %i\n", size);
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
    {
      printf("[Error] Bad file descriptor set.\n");
      return;
    }

    if (buffer_fd == 0)
    {
      printf("[Error] Buffer read timeout expired.\n");
      return;
    }

    int toRead = MIN(size - recv_size, 1024);

    if (buffer_fd > 0)
    {
      do
      {
        read_size = read(socket, imagearray, toRead);
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
  printf("[Info] Image %s received succesfully\n", file_name_string);
  printf("DEBUG. RECEIVED: %i\n", recv_size);
  fclose(image);
  process_image(file_name_string, key, "testabc.png");
  return 1;
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

  _Atomic int current_image_count = 0;

  parse_command(socket_desc, current_image_count);


  // pthread_t thread_id;
  // printf("Before Thread\n");
  // pthread_create(&thread_id, NULL, process_image, (void *)arguments);
  // pthread_join(thread_id, NULL);
  // printf("After Thread\n");

  close(socket_desc);

  return 0;
}
