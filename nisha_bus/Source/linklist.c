#include "linklist.h"

// 创建新节点
Node *create_node(void *data, size_t data_size)
{
    Node *new_node = (Node *)malloc(sizeof(Node));
    if (!new_node)
        return NULL;

    new_node->data = malloc(data_size);
    if (!new_node->data)
    {
        free(new_node);
        return NULL;
    }

    memcpy(new_node->data, data, data_size);
    new_node->data_size = data_size;
    new_node->next = NULL;
    return new_node;
}

// 在链表尾部添加节点
bool append_node(Node **head, void *data, size_t data_size)
{
    Node *new_node = create_node(data, data_size);
    if (!new_node)
        return false;

    if (*head == NULL)
    {
        *head = new_node;
    }
    else
    {
        Node *current = *head;
        while (current->next != NULL)
        {
            current = current->next;
        }
        current->next = new_node;
    }
    return true;
}

// 从链表中删除节点（需要自定义比较函数）
void delete_node(Node **head, void *data, int (*compare)(void *, void *))
{
    Node *current = *head;
    Node *prev = NULL;

    while (current != NULL)
    {
        if (compare(current->data, data))
        {
            if (prev == NULL)
            {
                *head = current->next;
            }
            else
            {
                prev->next = current->next;
            }
            free(current->data);
            free(current);
            return;
        }
        prev = current;
        current = current->next;
    }
}

// 遍历链表并执行操作（自定义处理函数）
void traverse_list(Node *head, void (*process)(void *))
{
    Node *current = head;
    while (current != NULL)
    {
        process(current->data);
        current = current->next;
    }
}


// 释放整个链表
void free_list(Node *head)
{
    Node *temp;
    while (head != NULL)
    {
        temp = head;
        head = head->next;
        free(temp->data);
        free(temp);
    }
}
