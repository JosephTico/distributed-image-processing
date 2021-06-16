#include <stdio.h>
#include <pthread.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

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

int main()
{
  // printf() displays the string inside quotation
  printf("Hello, World!\n");

  struct image_threads_arguments *arguments = (struct image_threads_arguments *)malloc(sizeof(struct image_threads_arguments));
  arguments->filename = "received_image.png";
  arguments->key = 2145;
  arguments->output = "output2.png";

  pthread_t thread_id;
  printf("Before Thread\n");
  pthread_create(&thread_id, NULL, process_image, (void *)arguments);
  pthread_join(thread_id, NULL);
  printf("After Thread\n");

  return 0;
}
