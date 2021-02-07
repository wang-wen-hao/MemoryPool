#include "ThreadCache.hpp"
#include "CentralCache.hpp"

//从中心缓存拿取数据
//每一次取批量的数据，因为每次到CentralCache申请内存的时候是需要加锁的，所以一次就多申请一些内存块，
//防止每次到CentralCache去内存块的时候,多次加锁造成效率问题
//byte要小于64k的，大于64k不会调用到这个函数
void* ThreadCache::FetchFromCentralCache(size_t index, size_t byte)
{
	//拿取的内存放在这个自由链表上
	FreeList* freelist = &_freelist[index];

	//每次从中心缓存取出10个这样大小的内存块
	//size_t num = 10;

	//不是每次申请10个，而是进行慢增长的过程
	//单个对象越小，申请内存块的数量越多
	//单个对象越大，申请内存块的数量越小
	// 感觉这里的逻辑，没必要用min( , )，直接按照freelist->maxSize()拿就行，如果byte很大的情况，就能用到前一项了吧
	size_t num_to_move = min(ClassSize::NumMoveSize(byte), freelist->MaxSize()); // 刚开始就是1吧，毕竟取了min


	//这里不对_maxsize进行增加，每次申请的都是一样的内存块数量
	//假如申请的是16byte的时候每次申请的时候都是申请1个内存块

	//start，end分别表示取出来的内存的开始地址和结束地址
	//取出来的内存是一个连续的内存
	void* start, *end;

	//fetchnum表示实际取出来的内存的个数，fetchnum有可能小于num，表示中心缓存没有那么多大小的内存块
	size_t fetchnum = CentralCache::GetInstance()->FetchRangeObj(start, end, num_to_move, byte);
	if (fetchnum > 1)
	{
		freelist->PushRange(NEXT_OBJ(start), end, fetchnum - 1); // 这里就是把n-1放到freelist中，那一个拿来用
	}

	if (num_to_move == freelist->MaxSize())
	{
		freelist->SetMaxSize(num_to_move + 1); // 把maxsize + 1，缓慢增长，越拿越多了，这样对吗？注意size != maxsize
	}


	////只给自由链表中放入(fetchnum - 1)个内存块，这是因为第一个内存块已经被使用了
	////对于中心缓存已经将内存块对象一个一个链接好了，就只需要将这一大块内存插入到自由链表就可以了
	//freelist->PushRange(NEXT_OBJ(start), end, fetchnum - 1);

	return start;
}

//从自由链表数组的自由链表上拿取内存
void* ThreadCache::Allocate(size_t byte)
{
	assert(byte < MAXBYTES); // byte要小于64k的
	//size是要拿取多少个字节的内存
	byte = ClassSize::RoundUp(byte);
	//index是自由链表的数组的下标
	size_t index = ClassSize::Index(byte);

	//可以确定要拿取的size是处于自由链表数组的哪一个自由链表
	FreeList* freelist = &_freelist[index];

	//判断链表是否为空
	//不为空，直接取内存
	if (!freelist->Empty())
	{
		//也就是从自由链表中拿内存
		return freelist->Pop();
	}
	//自由链表是空的要从中心堆中拿取内存，一次取多个防止重复取
	else 
	{
		return FetchFromCentralCache(index, byte);
	}
}

// byte是小于64k的
void ThreadCache::Deallocate(void* ptr, size_t byte)
{
	//向自由链表的数组中的自由链表归还内存
	//assert(byte <= MAXBYTES);
	size_t index = ClassSize::Index(byte);
	
	FreeList* freelist = &_freelist[index];

	freelist->Push(ptr);

	//当自由链表的数量超过一次从central cache申请的内存块的数量时
	//开始回收内存块到中心缓存
	// 到底一次从central cache中申请的内存块数量是多少呢？
	// 什么情况下会调用？
	if (freelist->Size() >= 2 * freelist->MaxSize())
	{
		
		ListTooLong(freelist, byte);
	}

	//当自由链表中的数量内存块的数量大于2倍的每次从Page申请的内存时，释放
	/*if (freelist->Size() >= ClassSize::NumMoveSize(byte) * 2)
	{
		ListTooLong(freelist, byte);
	}*/

	// thread cache总的字节数超过2M，则开始释放
	// 这种情况应该是还没实现
}

void ThreadCache::ListTooLong(FreeList* freelist, size_t byte)
{
	void* start = freelist->Clear();

	//将从start开始的内存归还给中心缓存
	CentralCache::GetInstance()->ReleaseListToSpans(start, byte);
}



