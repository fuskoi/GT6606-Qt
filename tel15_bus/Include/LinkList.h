#ifndef _LINKLIST_H
#define _LINKLIST_H



#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "WebSeriver.h"
 
//定义出路高度数据的单链表的节点
typedef struct SListNode
{
	DeepthPara data;//数据域
	struct SListNode* next;//指针域
}SLTNode;



typedef struct ReportData
{
	NotouchSpeedData stAutoNSCData;
	AutoMudsData stAutoMudsData;
	AutoADCPData stAutoADCPData;
	AutoRotatorData stAutoRotatorData;
		
}ReportData;

//定义上报数据的单链表的节点
typedef struct SLTReportNode
{
	ReportData stReportData;//数据域
	struct SLTReportNode* next;//指针域
}SLTReportNode;

 
//打印链表
void SLTPrint(SLTNode** phead);
 
//创建新节点
SLTNode* SLTBuyNode(DeepthPara n);
 
//尾插
void SLTPushBack(SLTNode** pphead, DeepthPara n);
 
//头插
void SLTPushFront(SLTNode** pphead, DeepthPara n);
 
//尾删
void SLTPopBack(SLTNode** pphead);
 
//头删
void SLTPopFront(SLTNode** pphead);
 
//查找
SLTNode* SLTFind(SLTNode* phead, DeepthPara n);
 
//指定位置之前插入
void SLTInsert(SLTNode** pphead, SLTNode* pos, DeepthPara n);
 
//指定位置之后插入
void SLTInsertAfter(SLTNode* pos, DeepthPara n);
 
//删除指定位置节点
void SLTErase(SLTNode** pphead, SLTNode* pos);
 
//销毁链表
void SLTDestroy(SLTNode** pphead);

int SLTChangeData(SLTNode** pphead, DeepthPara OrgData,DeepthPara AfterData);

extern SLTNode* g_pDeepList;
extern SLTNode* g_pSectionList;


//test


typedef struct Node //链表节点定义
{
    void *data;        //节点的数据域，存址
    struct Node *next; //指针域，保存下一个节点的地址，指向下一个节点
} Node;

typedef struct LinkList //链表
{
    Node firstNode; //链表的首个节点
    int size;       //链表长度
} List;


extern List *g_pstAutoNCSList;
extern List *g_pstAutoMudsList;
extern List *g_pstAutoADCPList;
extern List *g_pstAutoRotatorList;
List *initLinkList(); //链表初始化
Node *createSingleNode(void *data); //创建节点
Node *insertSingleNodeByHead(void *data, List *list);//单链表有表头头插法
void deleteSingleNodeByHead(Node* ppHeadNode);//头
Node *insertSingleNodeByTail(void *data, List *list);//尾插
Node *insertSingleNodeByIndex(void *data, int index, List *list);//根据索引index插入数据data至链表中
void* FirstListNodeData(List * list);//返回第一个节点
int LinkListFind(List* list, void* data);//查找
int Size_LinkList(List* list);//获取链表长度
void LinkListFree(List* list);//释放链表内存





#endif
