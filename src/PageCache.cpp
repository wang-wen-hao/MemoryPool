#include "PageCache.hpp"
#ifndef Lazy
//���ھ�̬�ĳ�Ա����������������г�ʼ��
PageCache PageCache::_inst;
#else
SPtr PageCache::_inst_ptr = nullptr; // �����˳�ʼ��
std::mutex PageCache::_mutex_Page;
#endif // !Lazy

//��������룬ֱ�Ӵ�ϵͳ
Span* PageCache::AllocBigPageObj(size_t size)
{
	assert(size > MAXBYTES);

	size = ClassSize::_RoundUp(size, PAGE_SHIFT); //����
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
	if (npage < NPAGES) //�൱�ڻ���С��128ҳ
	{
		span->_objsize = 0;
		RelaseToPageCache(span);
	}
	else
	{
		_id_span_map.erase(span->_pageid); // �Ҿ�������Ӧ�øĳ�id
		delete span;
		VirtualFree(ptr, 0, MEM_RELEASE);
	}
}

Span* PageCache::NewSpan(size_t npage)
{
	//��������ֹ����߳�ͬʱ��PageCache���������64k���ڴ棬��仰���ԣ�npage>=1������>=4
	std::unique_lock<std::mutex> lock(_mtx);
	//if (npage >= NPAGES)
	//{
	//	void* ptr = SystemAlloc(npage);
	//	Span* span = new Span();
	//	// ���� void * ptr��һ����ַΪ0x007c0000����ô����12λ�����0x007c0��ת��Ϊ10������1984
	//	// ������PAGE_SHIFT�ĺô��ǣ�����������ҳ����ô���ǵ�_pageidҲ�����������ģ�������һҳpageid����1985
	//	span->_pageid = (PageID)ptr >> PAGE_SHIFT; 
	//	span->_npage = npage; // ���������npage���԰ɣ���Ӧ����128��ϵͳ����ľ�������128ҳ����������ԭ���Ƿ����ͷŵ�ʱ���ж�
	//	span->_objsize = npage << PAGE_SHIFT; //����12λ����λ�Ǳ��أ�ʵ����û��ô��

	//	//����ֻ��Ҫ������Ĵ���ڴ��ĵ�һ��ҳ�Ų����ȥ�ͺ��ˡ�����Ϊʲô��
	//	//�ѵ�����Ϊ������������ҳ�ϲ������Բ���Ҫÿҳ����ǣ�
	//	_id_span_map[span->_pageid] = span;

	//	return span;
	//}

	assert(npage < NPAGES); // ����

	//��ϵͳ�������64KС��128ҳ���ڴ��ʱ����Ҫ��span��objsize����һ������Ϊ���ͷŵ�ʱ����кϲ�
	Span* span = _NewSpan(npage);
	//������Ƕ���һ����PageCache����span��ʱ������¼�������span����Ҫ�ָ��ʱ��
	//span->_objsize = span->_npage << PAGE_SHIFT;
	return span;
}

//��PageCache����ȡһ��span������CentralCache
Span* PageCache::_NewSpan(size_t npage)
{
	//�������page��span��Ϊ�գ���ֱ�ӷ���һ��span
	if (!_pagelist[npage].Empty())
	{
		return _pagelist[npage].PopFront();
	}

	//�������npage��spanΪ�գ����������������span�ǲ���Ϊ�յģ�������ǿյľͽ����и���span
	for (size_t i = npage + 1; i < NPAGES; i++)
	{
		if (!_pagelist[i].Empty())
		{
			//�����и�
			Span* span = _pagelist[i].PopFront();
			Span* split = new Span();

			//ҳ�ţ���span�ĺ�������и�
			split->_pageid = span->_pageid + span->_npage - npage;
			//ҳ��
			split->_npage = npage;
			span->_npage = span->_npage - npage; //span��_pageidû�䣬������ʼ��id

			//���·ָ������ҳ��ӳ�䵽�µ�span��
			// Ϊʲôǰ��ֻ��Ҫ������Ĵ���ڴ��ĵ�һ��ҳ�Ų����ȥ�ͺ��ˣ�
			for (size_t i = 0; i < npage; i++)
			{
				_id_span_map[split->_pageid + i] = split;
			}

			_pagelist[span->_npage].PushFront(span); //��Ϊspan->_npage��С�ˣ�����Ҫ���ġ�

			return split;
		}
	}

	//������Ҳ���ǣ�PageCache����Ҳû�д��������npage��ҳ��Ҫȥϵͳ�����ڴ棬Ҳ����˵���ǿյģ���
	//���ڴ�ϵͳ�����ڴ棬һ������128ҳ���ڴ棬�����Ļ������Ч�ʣ�һ�����빻����ҪƵ������
	//void* ptr = SystemAlloc(npage);
	// ������˵��SpanList��û�к��ʵ�span,ֻ����ϵͳ����128ҳ���ڴ�
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, (NPAGES - 1) * (1 << PAGE_SHIFT), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	//  brk
#endif

	Span* largespan = new Span();
	largespan->_pageid = (PageID)(ptr) >> PAGE_SHIFT;
	largespan->_npage = NPAGES - 1;
	_pagelist[NPAGES - 1].PushFront(largespan);

	//����ϵͳ�����ҳ��ӳ�䵽ͬһ��span
	for (size_t i = 0; i < largespan->_npage; i++)
	{
		_id_span_map[largespan->_pageid + i] = largespan;
	}

	return _NewSpan(npage); //�⻹�����˵ݹ飿����ʵ��Ȼǰ�涼���ˣ��ǿ���ֱ�Ӿ����������������ˣ�����Ҫ������
}


Span* PageCache::MapObjectToSpan(void* obj)
{

	//return _id_span_map[(PageID)(obj)]; // ����д��զ������

	//ȡ�����ڴ��ҳ��
	PageID pageid = (PageID)(obj) >> PAGE_SHIFT;
	
	auto it = _id_span_map.find(pageid);

	/***********************************************************************************/
	assert(it != _id_span_map.end()); // ������ʱ����bug�����Ҳ�һ�¡�����
	/***********************************************************************************/

	//���ص�������ڴ��ַҳ��Ϊ�Ǹ���span���ó�����
	return it->second;
}

//��CentralCache��span�黹��PageCache
// �����Span* �ɲ����Ըĳ�����ָ���أ�
void PageCache::RelaseToPageCache(Span* span)
{
	std::unique_lock<std::mutex> lock(_mtx);
	//���ͷŵ��ڴ��Ǵ���128ҳ
	if (span->_npage >= NPAGES)
	{
		// ��Ҫ�ͷŵ��й�ϣ���key-value���������ҳ
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

	//�ҵ����spanǰ���span
	auto previt = _id_span_map.find(span->_pageid - 1);

	while (previt != _id_span_map.end())
	{
		Span* prevspan = previt->second;
		//�ж�ǰ���span�ļ����ǲ���0
		if (prevspan->_usecount != 0)
		{
			break;
		}

		//�ж�ǰ���span���Ϻ����span��û�г���NPAGES
		if (prevspan->_npage + span->_npage >= NPAGES)
		{
			break;
		}

		//���кϲ�
		_pagelist[prevspan->_npage].Erase(prevspan);
		prevspan->_npage += span->_npage;
		delete(span); // ΪʲôҪ����������ֱ��ɾ��prevspan?�������ø�span��ֵ�˰�
		span = prevspan;
		//Ϊʲô������-1�����ϲ���-_npage�أ���Ϊ�����˴���128ҳ�ģ�������ÿһҳ��������map����ô��ǰ��_pageid-1�����ڲ�ͬ��span�ˣ�
		//������Ѱ�ҵ�ʱ��_pageid+_npage�����ڲ�ͬ��span
		//��ô���ڻ�ʣһ�����⣬����128ҳ��ûһҳһҳ�Ķ�Ӧ���᲻����Ӱ�죿
		previt = _id_span_map.find(span->_pageid - 1); 
	}

	//�ҵ����span�����span
	auto nextvit = _id_span_map.find(span->_pageid + span->_npage);

	while (nextvit != _id_span_map.end())
	{
		Span* nextspan = nextvit->second;
		//�ж�ǰ���span�ļ����ǲ���0
		if (nextspan->_usecount != 0)
		{
			break;
		}

		//�ж�ǰ���span���Ϻ����span��û�г���NPAGES
		if (nextspan->_npage + span->_npage >= NPAGES)
		{
			break;
		}

		//���кϲ�,�������span��span����ɾ�����ϲ���ǰ���span��
		_pagelist[nextspan->_npage].Erase(nextspan);
		span->_npage += nextspan->_npage;
		delete(nextspan);

		nextvit = _id_span_map.find(span->_pageid + span->_npage);
	}

	//���ϲ��õ�ҳ��ӳ�䵽�µ�span��
	for (size_t i = 0; i < span->_npage; i++)
	{
		_id_span_map[span->_pageid + i] = span;
	}

	//��󽫺ϲ��õ�span���뵽span����
	_pagelist[span->_npage].PushFront(span);
}
