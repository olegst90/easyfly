#include <stdio.h>
#include <string.h>
#include <errno.h>

int main(int argc, char **argv) {
    int res = -1, i = 0, j = 0;
    unsigned char mask = 0xff;

    if (argc != 4 && argc != 5) {
        fprintf(stderr, "usage: csvdec <input> <width>x<height> <output> [<hex mask>]\n");
        return -1;
    }

    FILE *fin = fopen(argv[1], "r");
    if (!fin) {
        fprintf(stderr, "Can't open %s: %s\n", argv[1], strerror(errno));
        return -1;
    }

    FILE *fout = fopen(argv[3], "w");
    if (!fout) {
        fprintf(stderr, "Can't open %s: %s\n", argv[3], strerror(errno));
        return -1;
    }

    int width, height;

    if (sscanf(argv[2], "%dx%d", &width, &height) != 2) {
        fprintf(stderr, "Invalid arg: %s\n", argv[2]);
        res = -1;
        goto exit;
    }

    if (argc == 5 && sscanf(argv[4], "%hhx", &mask) != 1) {
        fprintf(stderr, "Invalid arg: %s\n", argv[4]);
        res = -1;
        goto exit;
    }

    fprintf(fout, "P6\n%d %d\n255\n", width, height);

    // skip first line
    char header[256];
    fgets(header, sizeof(header), fin);
    for (i = 0; i < height; ++i)
    {
        for (j = 0; j < width; ++j)
        {
            static unsigned char color[3];
            static int d;

            if (fscanf(fin, "%*[^,],'%d'", &d) != 1) {
                fprintf(stderr, "could not read next pixel\n");
                res = -1;
                goto exit;
            }
            d &= mask;
            color[0] = (unsigned char)d;
            color[1] = (unsigned char)d;
            color[2] = (unsigned char)d;
            fwrite(color, 1, 3, fout);
        }
    }

exit:
    fclose(fin);
    fclose(fout);
    return res;
}
