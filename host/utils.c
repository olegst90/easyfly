#include "utils.h"
#include <stdio.h>
#include <stdint.h>

int write_ppm(const uint8_t *buffer, int width, int height, const char *path)
{
    printf("saving %s\n", path);
  int i, j;
  FILE *fp = fopen(path, "wb"); 
  (void) fprintf(fp, "P6\n%d %d\n255\n", width, height);
  for (i = 0; i < height; ++i)
  {
    for (j = 0; j < width; ++j)
    {
      static unsigned char color[3];
      color[0] = buffer[i*width + j];  /* red */
      color[1] = buffer[i*width + j];  /* green */
      color[2] = buffer[i*width + j];  /* blue */
      (void) fwrite(color, 1, 3, fp);
    }
  }
  (void) fclose(fp);
  printf("...saved\n");
  return 0;
}

void DumpHex(const void* data, size_t size) {
    char ascii[17];
    size_t i, j;
    ascii[16] = '\0';
    for (i = 0; i < size; ++i) {
        printf("%02X ", ((unsigned char*)data)[i]);
        if (((unsigned char*)data)[i] >= ' ' && ((unsigned char*)data)[i] <= '~') {
            ascii[i % 16] = ((unsigned char*)data)[i];
        } else {
            ascii[i % 16] = '.';
        }
        if ((i+1) % 8 == 0 || i+1 == size) {
            printf(" ");
            if ((i+1) % 16 == 0) {
                printf("|  %s \n", ascii);
            } else if (i+1 == size) {
                ascii[(i+1) % 16] = '\0';
                if ((i+1) % 16 <= 8) {
                    printf(" ");
                }
                for (j = (i+1) % 16; j < 16; ++j) {
                    printf("   ");
                }
                printf("|  %s \n", ascii);
            }
       }
    }
}

