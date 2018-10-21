#pragma once

#ifndef ul_free
  #define ul_free free
#endif

struct ulist {
  struct ulist *next;
};

#define H(p) ((struct ulist *)(p))

#define UL_ADD(head, entry) {\
  struct ulist *p;\
  for (p = H(head); p->next; p = p->next);\
  p->next = H(entry);\
  H(entry)->next = NULL;\
}

#define UL_REMOVE(head, entry) {\
  struct ulist *p;\
  for (p = H(head); p->next; p = p->next) {\
    if (p->next == H(entry)) {\
      p->next = H(entry)->next;\
      ul_free(entry);\
      break;\
    }\
  }\
}

#define UL_FOREACH(head, iname) \
  for (struct ulist *iname = H(head)->next; iname; iname = iname->next)\

#define UL_CLEAR(head) {\
  struct ulist *p = H(head)->next, *tmp;\
  while (p) {\
    tmp = p;\
    p = p->next;\
    ul_free(tmp);\
  }\
}
