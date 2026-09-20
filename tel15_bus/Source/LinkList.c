#include "LinkList.h"
#include "log.h"
SLTNode* g_pDeepList;
SLTNode* g_pSectionList;




//打印链表
void SLTPrint(SLTNode** phead)
{
	SLTNode* cur = *phead;//定义指针指向头节点
	while (cur != NULL)//最后一个节点的next为空，cur等于空则说明遍历结束
	{
		LOG(LOG_DEBUG,"SLTPrint element is %08x deepth is %2x ",cur->data.u32Element, cur->data.u8Deepth);//访问数据并打印
		cur = cur->next;//对cur解引用拿到下一个节点的地址，然后赋值给cur，cur就指向了下一个节点
	}
}
 
//创建新节点
SLTNode* SLTBuyNode(DeepthPara n)
{
	SLTNode* newnode = (SLTNode*)malloc(sizeof(SLTNode));//动态开辟一个节点大小的内存
	if (newnode == NULL)//内存开辟失败，则直接退出程序
	{
		perror("malloc");
		exit(1);
	}
	newnode->data = n;//将数据赋值给节点的数据域
	newnode->next = NULL;//为了确保链表末尾为空指针，所以创建的所有节点默认next为空
	return newnode;//将节点返回
}
 
//尾插
void SLTPushBack(SLTNode** pphead, DeepthPara n)
{
	assert(pphead);
	
	SLTNode* newnode = SLTBuyNode(n);//创建新节点
	if (*pphead == NULL)//头指针为空说明链表为空
	{
		printf("SLTPushBack pphead is  null\n");
		//链表为空，此时插入第一个元素，需要将头指针指向新节点，
		//而在函数内修改头指针就要传入头指针的地址，也就是二级指针
		*pphead = newnode;
	}
	else//链表不为空的情况
	{
		printf("SLTPushBack pphead is not null\n");
		SLTNode* cur = *pphead;
		while (cur->next != NULL)//从头节点开始，循环遍历找到最后一个节点
		{
			cur = cur->next;
		}
		cur->next = newnode;//将新节点的地址赋值给最后一个节点的指针域
	}
}
 
//头插
void SLTPushFront(SLTNode** pphead, DeepthPara n)
{
	assert(pphead);//确保传入的不是空指针
	SLTNode* newnode = SLTBuyNode(n);//创建新节点
	newnode->next = *pphead;//使新节点的next指针指向原来的第一个节点
	*pphead = newnode;//头指针指向新节点
}
 
//尾删
void SLTPopBack(SLTNode** pphead)
{
	assert(pphead && *pphead);//确保传入的不是空指针并且链表不为空
	if ((*pphead)->next == NULL)//链表只有一个节点的情况
	{
		free((*pphead)->next);//释放该节点的空间
		*pphead = NULL;//改变了头节点的值，所以也要传二级指针
	}
	else//节点大于1的情况
	{
		SLTNode* prev = *pphead;
		while (prev->next->next != NULL)//循环遍历，使prev指向倒数第二个节点
		{
			prev = prev->next;
		}
		free(prev->next);//释放最后一个节点的空间
		prev->next = NULL;//将此时的最后一个节点的next制为空
	}
}
 
//头删
void SLTPopFront(SLTNode** pphead)
{
	assert(pphead && *pphead);
	SLTNode* next = (*pphead)->next;//保存第二个节点的地址/空指针（只有一个节点时）
	free(*pphead);//释放第一个节点的空间
	*pphead = next;//让头指针指向刚才保存的节点/空指针，也要传二级指针
}
 
//查找
SLTNode* SLTFind(SLTNode* phead, DeepthPara n)
{
	SLTNode* cur = phead;
	while (cur != NULL)//遍历链表的所有节点
	{
		if (cur->data.u8Deepth == n.u8Deepth)//匹配成功，返回该节点的地址
		{
			return cur;
		}
		cur = cur->next;
	}
	return NULL;//没有找到，返回空指针
}
 
//指定位置之前插入
void SLTInsert(SLTNode** pphead, SLTNode* pos, DeepthPara n)
{
	assert(pphead && pos);
	SLTNode* newnode = SLTBuyNode(n);
	if (*pphead == pos)//指定位置是头节点的情况
	{
		//进行头插
		newnode->next = *pphead;
		*pphead = newnode;
	}
	else
	{
		SLTNode* prev = *pphead;
		while (prev->next != pos)//遍历找到pos节点的前驱节点
		{
			prev = prev->next;
		}
		newnode->next = pos;//新节点的next指针指向pos
		prev->next = newnode;//前驱节点的next指针指向新节点
	}
}
 
//指定位置之后插入
void SLTInsertAfter(SLTNode* pos, DeepthPara n)
{
	assert(pos);//确保pos不为空指针
	SLTNode* newnode = SLTBuyNode(n);
	newnode->next = pos->next;//newnode的next指向后继节点
	pos->next = newnode;//前驱节点的next指向newnode
}
 
//删除指定位置节点
void SLTErase(SLTNode** pphead, SLTNode* pos)
{
	assert(pphead && pos && *pphead);//确保传入的不是空指针并且链表不为空
	if (pos == *pphead)//指定位置是头节点情况
	{
		//头删
		*pphead = pos->next;//先使头指针指向第二个节点
		free(pos);//释放掉pos节点
	}
	else
	{
		SLTNode* prev = *pphead;
		while (prev->next != pos)//循环遍历，找到pos的前驱节点
		{
			prev = prev->next;
		}
		prev->next = pos->next;//使前驱节点的next指针指向pos的后继节点
		free(pos);//释放掉pos节点
	}
	pos = NULL;//对野指针及时制空
}
 
//销毁链表
void SLTDestroy(SLTNode** pphead)
{
	assert(pphead);
	SLTNode* cur = *pphead;//从头节点开始遍历
	while (cur != NULL)
	{
		SLTNode* next = cur->next;//先记录下一个节点
		free(cur);//释放当前节点
		cur = next;//释放后，cur指向记录的节点
	}
	*pphead = NULL;//将头指针制空
}



//给一个数据，找到这个数据所在的节点，并用新数据修改
int SLTChangeData(SLTNode** pphead, DeepthPara OrgData,DeepthPara AfterData)
//因为不需要改变节点的地址，所以值传递即可 
//OrgData是查找的数据，AfterData是新数据，用来修改查找的数据                                              
{
	//printf("start SLTChangeData\n");
	SLTNode* cru = *pphead;
	
	while (cru != NULL)//如果没有节点，根本不会进入循环去找
	{
		printf("SLTChangeData cur is not null\n");
		
		if (cru->data.u8Deepth == OrgData.u8Deepth)
		{
			cru->data.u32Element = AfterData.u32Element;
			printf("cru->data.u8Deepth is %02x,cru->data.u32Element is %02x\n",cru->data.u8Deepth,cru->data.u32Element);
			break;//修改完数据后，就跳出循环
		}
		else
		{
			cru = cru->next;
		}
	}
 
	if (cru == NULL)//如果循环完单链表，没有找到要修改的那个数据
	{
		return ERROR;
	}
	else
	{
		return SUCCESS;
	}
}



//test

List *initLinkList() //链表初始化
{
    List *list = (List *)malloc(sizeof(List));//向堆区申请内存
    if (list == NULL)
    {
    	printf("ListInit Error!\n");
    	return NULL;
    }
        
    list->firstNode.data = NULL;//初始化
    list->firstNode.next = NULL;
    list->size = 0;
    return list;
}


Node *createSingleNode(void *data) //创建节点
{
    Node *node = (Node *)malloc(sizeof(Node));
    node->data = data;
    node->next = NULL;
    return node;
}


//单链表有表头头插法
Node *insertSingleNodeByHead(void *data, List *list)
{
    if (data == NULL || list == NULL)//空指针判断
        return NULL;
    Node *node = createSingleNode(data);
    node->next = list->firstNode.next;
    list->firstNode.next = node;
    list->size++;//链表容量+1
    return node;
}

//头删
void deleteSingleNodeByHead(Node* ppHeadNode)
{
	Node* next = (*ppHeadNode).next;//保存第二个节点的地址/空指针（只有一个节点时）
	free(ppHeadNode);//释放第一个节点的空间
	ppHeadNode = next;//让头指针指向刚才保存的节点/空指针，也要传二级指针
}



//单链表尾插法
Node *insertSingleNodeByTail(void *data, List *list) 
{
    if (data == NULL || list == NULL)
        return NULL;
    Node *node = createSingleNode(data);
    Node *pointer = &(list->firstNode);//拿到首节点的地址
    while (pointer->next)
        pointer = pointer->next;
    pointer->next = node;//此时pointer是最后一个节点
    node->next = NULL;//node成为最后一个节点
    list->size++;
    return node;
}


Node *insertSingleNodeByIndex(void *data, int index, List *list)//根据索引index插入数据data至链表中
{
	int i = 0;
    if (data == NULL || list == NULL)
        return NULL;
    if (index < 0 || index > list->size) // 0索引为无有效数据的firstNode
        index = list->size;              //非法索引时，统一尾插处理
    Node *node = createSingleNode(data);
    Node *firstN = &list->firstNode;//拿到首节点地址
    for (i = 0; i < index; i++)
        firstN = firstN->next;
    node->next = firstN->next;
    firstN->next = node;
    list->size++;
    return node;
}



//返回第一个节点
void* FirstListNodeData(List * list)
{
	return list->firstNode.next->data;
}




//查找
int LinkListFind(List* list, void* data)
{
	if (list == NULL)
	{
		return -1;
	}
 
	if (data == NULL)
	{
		return -1;
	}
 
	Node* pCurrentNode = list->firstNode.next->next;
 
	int i = 0;
	while (pCurrentNode != NULL)
	{
		if (pCurrentNode->data == data)
		{
			break;
		}
		i++;
		pCurrentNode = pCurrentNode->next;
	}
 
	if (i == list->size-1)
	{
		return -1;
	}
	else
	{
		return i;
	}
}


//获取链表长度
int Size_LinkList(List* list)
{
	return list->size;
}




//释放链表内存
void LinkListFree(List* list)
{
	if (list == NULL)
	{
		return;
	}
	
	//辅助指针变量，或者理解为辅助结点
	Node* pCurrentNode = list->firstNode.next;
	while (pCurrentNode != NULL)
	{
		//记录下一个节点
		Node* pNext = pCurrentNode->next;
		free(pCurrentNode);
		pCurrentNode = pNext;
	}
 
	//释放链表内存
	list->size = 0;
	free(list);
}






