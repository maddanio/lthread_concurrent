#pragma once

/* Sudhanshu Patel sudhanshuptl13@gmail.com */
/* Min lthread_min_heap_t implementation in c   */

#include<stdlib.h>

struct lthread_min_heap_node
{
    void* data;
    uint64_t key;
};
typedef struct lthread_min_heap_node lthread_min_heap_node_t;

struct lthread_min_heap
{
    lthread_min_heap_node_t* arr;
    size_t count;
    size_t capacity;
};
typedef struct lthread_min_heap lthread_min_heap_t;

inline lthread_min_heap_t* lthread_min_heap_create(size_t capacity);
inline void lthread_min_heap_insert(lthread_min_heap_t* h, lthread_min_heap_node_t node);
inline lthread_min_heap_node_t lthread_min_heap_pop(lthread_min_heap_t *h);
inline void _lthread_min_heap_heapify_bottom_top(lthread_min_heap_t *h, size_t index);
inline void _lthread_min_heap_heapify_top_bottom(lthread_min_heap_t* h, size_t parent_node);

inline lthread_min_heap_t* lthread_min_heap_create(size_t capacity)
{
    lthread_min_heap_t *h = (lthread_min_heap_t*) malloc(sizeof(lthread_min_heap_t));
    if(h == NULL)
        return NULL;
    h->count=0;
    h->capacity = capacity;
    h->arr = malloc(capacity * sizeof(lthread_min_heap_node_t));
    if (h->arr == NULL)
    {
        free(h);
        return NULL;
    }
    return h;
}

inline void lthread_min_heap_insert(lthread_min_heap_t* h, lthread_min_heap_node_t node)
{
    if( h->count == h->capacity)
    {
        h->capacity += (h->capacity + 1) / 2;
        h->arr = realloc(h->arr, h->capacity * sizeof(lthread_min_heap_node_t));
    }
    h->arr[h->count] = node;
    _lthread_min_heap_heapify_bottom_top(h, h->count);
    h->count++;
}

inline lthread_min_heap_node_t lthread_min_heap_pop(lthread_min_heap_t *h)
{
    lthread_min_heap_node_t pop;
    pop = h->arr[0];
    h->arr[0] = h->arr[--h->count];
    _lthread_min_heap_heapify_top_bottom(h, 0);
    return pop;
}

inline void _lthread_min_heap_heapify_bottom_top(lthread_min_heap_t *h, size_t index)
{
    while (index != 0)
    {
        size_t parent_node = (index - 1) / 2;
        if (h->arr[parent_node].key <= h->arr[index].key)
            return;
        lthread_min_heap_node_t temp = h->arr[parent_node];
        h->arr[parent_node] = h->arr[index];
        h->arr[index] = temp;
        index = parent_node;
    }
}

inline void _lthread_min_heap_heapify_top_bottom(lthread_min_heap_t* h, size_t parent_node)
{
    for (;;)
    {
        size_t left = parent_node * 2 + 1;
        size_t right = parent_node * 2 + 2;
        size_t min = parent_node;
        if(left < h->count && h->arr[left].key < h->arr[parent_node].key)
            min = left;
        if(right < h->count && h->arr[right].key < h->arr[min].key)
            min = right;
        if (min == parent_node)
            return;
        lthread_min_heap_node_t temp = h->arr[min];
        h->arr[min] = h->arr[parent_node];
        h->arr[parent_node] = temp;
        _lthread_min_heap_heapify_top_bottom(h, min);
    }
}
