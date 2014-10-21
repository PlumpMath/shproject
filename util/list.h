/*
 * Circular doubly-linked list with sentinel.
 */
#ifndef _UTIL_LIST_H
#define _UTIL_LIST_H

#include <assert.h>
#include <stddef.h>


struct list_node {
    struct list_node* next;
    struct list_node* prev;
};


#define LIST_ITEM(list_node, type, list_member) \
    (type*)((char*)(list_node) - offsetof(type, list_member))  // cheers Linus


#define LIST_FOR_EACH(node, list) \
    for (node = list->next; node != list; node = node->next)


static inline void list_init(struct list_node* list) {
    list->next = list;
    list->prev = list;
}


static inline int list_empty(struct list_node* list) {
    assert(list->next != NULL && list->prev != NULL);
    return list->next == list;
}


static inline int list_in_list(struct list_node* node) {
    return node->next != NULL || node->prev != NULL;
}


static inline struct list_node* list_front(struct list_node* list) {
    assert(list->next != NULL && list->prev != NULL);
    return list->next;
}


static inline struct list_node* list_back(struct list_node* list) {
    assert(list->next != NULL && list->prev != NULL);
    return list->prev;
}


static inline void list_insert(struct list_node* after, struct list_node* item) {
    assert(after->next != NULL && after->prev != NULL);
    assert(after != item);
    item->next = after->next;
    item->prev = after;
    after->next = item;
    item->next->prev = item;
}


static inline void list_remove(struct list_node* item) {
    assert(item->next != NULL && item->prev != NULL);
    item->prev->next = item->next;
    item->next->prev = item->prev;
    item->prev = NULL;
    item->next = NULL;
}


static inline void list_push_front(struct list_node* list, struct list_node* entry) {
    return list_insert(list, entry);
}


static inline void list_push_back(struct list_node* list, struct list_node* entry) {
    return list_insert(list_back(list), entry);
}


static inline struct list_node* list_pop_front(struct list_node* list) {
    assert(!list_empty(list));
    struct list_node* front = list->next;
    list_remove(front);
    return front;
}


static inline struct list_node* list_pop_back(struct list_node* list) {
    assert(!list_empty(list));
    struct list_node* back = list->prev;
    list_remove(back);
    return back;
}

#endif
