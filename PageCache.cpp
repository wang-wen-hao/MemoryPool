#include "PageCache.hpp"
#ifndef Lazy
//对于静态的成员变量必须在类外进行初始化
PageCache PageCache::_inst;
#else
SPtr PageCache::_inst_ptr = nullptr; // 别忘了初始化
std::mutex PageCache::_mutex_Page;
#endif // !Lazy

//大对象申请，直接从系统
Span* PageCache::AllocBigPageObj(size_t size)
{
	assert(size > MAXBYTES);

	size = ClassSize::_RoundUp(size, PAGE_SHIFT); //对齐
	size_t npage = size >> PAGE_SHIFT;
	if (npage < NPAGES)
	{
		Span* span = NewSpan(npage);
		span->_objsize = size;
		return span;
	}
	else
	{
		void* ptr = VirtualAlloc(0, npage << PAGE_SHIFT,
			MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

		if (ptr == nullptr)
			throw std::bad_alloc();

		Span* span = new Span;
		span->_npage = npage;
		span->_pageid = (PageID)ptr >> PAGE_SHIFT;
		span->_objsize = npage << PAGE_SHIFT;

		_id_span_map[span->_pageid] = span;

		return span;
	}
}

void PageCache::FreeBigPageObj(void* ptr, Span* span)
{
	size_t npage = span->_objsize >> PAGE_SHIFT;
	if (npage < NPAGES) //相当于还是小于128页
	{
		span->_objsize = 0;
		RelaseToPageCache(span);
	}
	else
	{
		_id_span_map.erase(span->_pageid); // 我觉得这里应该改成id
		delete span;
		VirtualFree(ptr, 0, MEM_RELEASE);
	}
}

Span* PageCache::NewSpan(size_t npage)
{
	//加锁，防止多个线程同时到PageCache中申请大于64k的内存，这句话不对，npage>=1而不是>=4
	std::unique_lock<std::mutex> lock(_mtx);
	//if (npage >= NPAGES)
	//{
	//	void* ptr = SystemAlloc(npage);
	//	Span* span = new Span();
	//	// 例如 void * ptr是一个地址为0x007c0000，那么右移12位变成了0x007c0，转换为10进制是1984
	//	// 用右移PAGE_SHIFT的好处是，这样连续的页，那么他们的_pageid也是整数连续的，比如下一页pageid就是1985
	//	span->_pageid = (PageID)ptr >> PAGE_SHIFT; 
	//	span->_npage = npage; // 问题这里的npage不对吧，不应该是128吗？系统申请的就申请了128页。这样做的原因是方便释放的时候判断
	//	span->_objsize = npage << PAGE_SHIFT; //左移12位，单位是比特，实际上没那么大

	//	//这里只需要将申请的大的内存块的第一个页号插入进去就好了。。。为什么？
	//	//难道是因为它不会与其他页合并，所以不需要每页都标记？
	//	_id_span_map[span->_pageid] = span;

	//	return span;
	//}

	assert(npage < NPAGES); // 断言

	//从系统申请大于64K小于128页的内存的时候，需要将span的objsize进行一个设置为了释放的时候进行合并
	Span* span = _NewSpan(npage);
	//这个就是对于一个从PageCache申请span的时候，来记录申请这个span的所要分割的时候
	//span->_objsize = span->_npage << PAGE_SHIFT;
	return span;
}

//从PageCache出拿取一个span用来给CentralCache
Span* PageCache::_NewSpan(size_t npage)
{
	//如果对于page的span不为空，则直接返回一个span
	if (!_pagelist[npage].Empty())
	{
		return _pagelist[npage].PopFront();
	}

	//如果对于npage的span为空，接下来检测比他大的span是不是为空的，如果不是空的就进行切割大的span
	for (size_t i = npage + 1; i < NPAGES; i++)
	{
		if (!_pagelist[i].Empty())
		{
			//进行切割
			Span* span = _pagelist[i].PopFront();
			Span* split = new Span();

			//页号，从span的后面进行切割
			split->_pageid = span->_pageid + span->_npage - npage;
			//页数
			split->_npage = npage;
			span->_npage = span->_npage - npage; //span的_pageid没变，还是起始的id

			//将新分割出来的页都映射到新的span上
			// 为什么前面只需要将申请的大的内存块的第一个页号插入进去就好了？
			for (size_t i = 0; i < npage; i++)
			{
				_id_span_map[split->_pageid + i] = split;
			}

			_pagelist[span->_npage].PushFront(span); //因为span->_npage变小了，所有要更改。

			return split;
		}
	}

	//到这里也就是，PageCache里面也没有大于申请的npage的页，要去系统申请内存，也就是说都是空的？？
	//对于从系统申请内存，一次申请128页的内存，这样的话，提高效率，一次申请够不需要频繁申请
	//void* ptr = SystemAlloc(npage);
	// 到这里说明SpanList中没有合适的span,只能向系统申请128页的内存
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, (NPAGES - 1) * (1 << PAGE_SHIFT), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	//  brk
#endif

	Span* largespan = new Span();
	largespan->_pageid = (PageID)(ptr) >> PAGE_SHIFT;
	largespan->_npage = NPAGES - 1;
	_pagelist[NPAGES - 1].PushFront(largespan);

	//将从系统申请的页都映射到同一个span
	for (size_t i = 0; i < largespan->_npage; i++)
	{
		_id_span_map[largespan->_pageid + i] = largespan;
	}

	return _NewSpan(npage); //这还搞起了递归？但其实既然前面都空了，那可以直接就向着申请后的申请了，不需要遍历了
}


Span* PageCache::MapObjectToSpan(void* obj)
{

	//return _id_span_map[(PageID)(obj)]; // 这种写法咋不好吗？

	//取出该内存的页号
	PageID pageid = (PageID)(obj) >> PAGE_SHIFT;
	
	auto it = _id_span_map.find(pageid);

	/***********************************************************************************/
	assert(it != _id_span_map.end()); // 这里有时候会出bug，等我查一下。。。
	/***********************************************************************************/

	//返回的是这个内存地址页号为那个的span中拿出来的
	return it->second;
}

//将CentralCache的span归还给PageCache
// 这里的Span* 可不可以改成智能指针呢？
void PageCache::RelaseToPageCache(Span* span)
{
	std::unique_lock<std::mutex> lock(_mtx);
	//当释放的内存是大于128页
	if (span->_npage >= NPAGES)
	{
		// 需要释放的有哈希表的key-value，和申请的页
		void* ptr = (void*)(span->_pageid << PAGE_SHIFT);
		_id_span_map.erase(span->_pageid);
#ifdef _WIN32
		//SystemFree(ptr);
		VirtualFree(ptr, 0, MEM_RELEASE);
#else

#endif
		delete span;
		return;
	}

	//找到这个span前面的span
	auto previt = _id_span_map.find(span->_pageid - 1);

	while (previt != _id_span_map.end())
	{
		Span* prevspan = previt->second;
		//判断前面的span的计数是不是0
		if (prevspan->_usecount != 0)
		{
			break;
		}

		//判断前面的span加上后面的span有没有超出NPAGES
		if (prevspan->_npage + span->_npage >= NPAGES)
		{
			break;
		}

		//进行合并
		_pagelist[prevspan->_npage].Erase(prevspan);
		prevspan->_npage += span->_npage;
		delete(span); // 为什么要这样？不如直接删除prevspan?这样不用给span赋值了啊
		span = prevspan;
		//为什么这里是-1但向后合并就-_npage呢？因为，除了大于128页的，其他的每一页都会做好map，那么当前的_pageid-1后，属于不同的span了，
		//而往后寻找的时候，_pageid+_npage才属于不同的span
		//那么现在还剩一个问题，大于128页的没一页一页的对应，会不会受影响？
		previt = _id_span_map.find(span->_pageid - 1); 
	}

	//找到这个span后面的span
	auto nextvit = _id_span_map.find(span->_pageid + span->_npage);

	while (nextvit != _id_span_map.end())
	{
		Span* nextspan = nextvit->second;
		//判断前面的span的计数是不是0
		if (nextspan->_usecount != 0)
		{
			break;
		}

		//判断前面的span加上后面的span有没有超出NPAGES
		if (nextspan->_npage + span->_npage >= NPAGES)
		{
			break;
		}

		//进行合并,将后面的span从span链中删除，合并到前面的span上
		_pagelist[nextspan->_npage].Erase(nextspan);
		span->_npage += nextspan->_npage;
		delete(nextspan);

		nextvit = _id_span_map.find(span->_pageid + span->_npage);
	}

	//将合并好的页都映射到新的span上
	for (size_t i = 0; i < span->_npage; i++)
	{
		_id_span_map[span->_pageid + i] = span;
	}

	//最后将合并好的span插入到span链中
	_pagelist[span->_npage].PushFront(span);
}
