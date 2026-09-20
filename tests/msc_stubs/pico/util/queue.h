#ifndef TEST_QUEUE_H
#define TEST_QUEUE_H
typedef struct {int unused;} queue_t;
void queue_add_blocking(queue_t *,const void *);
void queue_remove_blocking(queue_t *,void *);
#endif
