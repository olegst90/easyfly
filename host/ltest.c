#include <stdio.h>
#include <stdlib.h>
#include "../esp8285/ulist.h"

struct s {
  struct ulist *entry;
  int a;
};

int main() {
    struct s head = {0};
    struct s *entry = (struct s*)malloc(sizeof(*entry));
    entry->a = 1;
    UL_ADD(&head, entry);

    entry = (struct s*)malloc(sizeof(*entry));
    entry->a = 2;

    UL_ADD(&head, entry);

    entry = (struct s*)malloc(sizeof(*entry));
    entry->a = 3;

    UL_ADD(&head, entry);

    entry = (struct s*)malloc(sizeof(*entry));
    entry->a = 4;

    UL_ADD(&head, entry);
    
    UL_FOREACH(&head, i) {
        printf("a %d\n", ((struct s *)i)->a);
    }

    UL_FOREACH(&head, i) {
        if (((struct s*)i)->a == 1) {
            printf("removing...\n");
            UL_REMOVE(&head, i);
        }
    }
    UL_FOREACH(&head, i) {
        printf("a %d\n", ((struct s *)i)->a);
    }
    UL_CLEAR(&head);
    return 0;
}
