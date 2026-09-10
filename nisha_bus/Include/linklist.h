#ifndef __LINKLIST_H__
#define __LINKLIST_H__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// 链表节点结构体
typedef struct Node
{
    void *data;        // 存储任意类型的数据指针
    size_t data_size;  // 数据大小（字节数）
    struct Node *next; // 指向下一个节点的指针
} Node;

// 比较函数模板
typedef int (*CompareFunc)(void *, void *);
// 数据处理函数模板
typedef void (*ProcessFunc)(void *);

Node *create_node(void *data, size_t data_size);
bool append_node(Node **head, void *data, size_t data_size);
void delete_node(Node **head, void *data, int (*compare)(void *, void *));
void traverse_list(Node *head, void (*process)(void *));
void free_list(Node *head);
#endif // !
