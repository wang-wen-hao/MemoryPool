# MemoryPool  

## 一、项目要解决的问题  
* 内存碎片问题   
* 多线程场景获取内存的竞争问题   
## 二、高并发内存池设计
### 2.1主要组成部分
* thread cache：  
线程缓存是每个线程独有的，用于小于64k的内存的分配，线程从这里申请内存不需要加锁，每个线程独享一个cache，这也就是这个并发线程池高效的地方。为了避免加锁带来的效率，在Thread Cache中使用thread local storage保存每个线程本地的Thread Cache的指针，这样Thread Cache在申请释放内存是不需要锁的。因为每一个线程都拥有了自己唯一的一个全局变量。
* Central cache：  
中心缓存是所有线程所共享，thread cache是按需要从Central cache中获取的对象。 Central cache周期性的回收thread cache中的对象，避免一个线程占用了太多的内存，而其他线程的内存吃紧。达到内存分配在多个线程中更均衡的按需调度的目的。Central cache是存在竞争的，所以从这里取内存对象是需要加锁。
* Page cache：  
页缓存是在Central cache缓存上面的一层缓存，存储的内存是以页为单位存储及分配的，Central cache没有内存对象(Span)时，从Page cache分配出一定数量的page，并切割成定长大小的小块内存，分配给Central cache。Page cache会回收Central cache满足条件的Span(使用计数为0)对象，并且合并相邻的页，组成更大的页，缓解内存碎片的问题。
### 2.2ThreadCache类
* 2.2.1Thread Cache申请内存  
* 2.2.2Thread Cache释放内存  
### 2.3Central Cache设计
**2.3.1span与spanlist**  
```c
1. // span结构  
2.	  
3.	// 对于span是为了对于thread cache还回来的内存进行管理  
4.	// 一个span中包含了内存块  
5.	typedef size_t PageID;  
6.	struct Span  
7.	{  
8.	    PageID _pageid = 0;   //起始页号(一个span包含多个页)  
9.	    size_t _npage = 0;    //页的数量  
10.	    Span* _next = nullptr; // 维护双向span链表  
11.	    Span* _prev = nullptr;  
12.	  
13.	    void* _objlist = nullptr; //对象自由链表  
14.	    size_t _objsize = 0;      //记录该span上的内存块的大小  
15.	    size_t _usecount = 0;     //使用计数  
16.	};  

```
spanlist，设计为一个双向链表，插入删除效率较高。  
**2.3.2 Central Cache申请内存**  
**2.3.3 Central Cache释放内存**  
### 2.4PageCache设计
**2.4.1申请内存**  
**2.4.2释放内存**  
### 2.5单例模式的使用方式
* 2.5.1饿汉模式  
* 2.5.2懒汉模式  
### 2.6杂项
#### 2.6.1 页号的标记技巧PageID  
```c
1.	void* ptr = SystemAlloc(npage);  
2.	Span* span = new Span();  
3.	span->_pageid = (PageID)ptr >> PAGE_SHIFT;   
```