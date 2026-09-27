#ifndef TEST_QUEUE_H
#define TEST_QUEUE_H
#include <stdbool.h>
typedef struct {int unused;} queue_t;
void queue_add_blocking(queue_t *,const void *);
void queue_remove_blocking(queue_t *,void *);
bool queue_try_add(queue_t *,const void *);
bool queue_try_remove(queue_t *,void *);
#endif
