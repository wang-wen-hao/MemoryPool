/*
	������Դ�ľ��⣬����ThreadCache��ĳ����Դ��ʣ��ʱ�򣬿��Ի���ThreadCache�ڲ����ڴ�
	�Ӷ����Է����������ThreadCache
	ֻ��һ�����Ļ��棬�������е��߳�����ȡ�ڴ��ʱ��Ӧ����һ�����Ļ���
	���Զ������Ļ������ʹ�õ���ģʽ�����д������Ļ������
	�������Ļ�����˵Ҫ����
*/

#pragma once 

#include "Common.hpp"
#include <iostream>
//#include "PageCache.hpp" //����include����Ȼ���ض���

#define Lazy

class CentralCache
{
public:
	~CentralCache() {
		std::cout << "Central Cache ��������" << std::endl;
	}
	
#ifdef Lazy
	//����ģʽ�����þ�̬�ֲ�����
	static CentralCache* GetInstance() {
		static CentralCache _inst;
		return &_inst;
	}
#else
	static CentralCache* GetInstance()
	{
		return &_inst;
	}
#endif

	//�����Ļ����ȡһ���������ڴ��thread cache
	size_t FetchRangeObj(void*& start, void*& end, size_t num, size_t byte);

	//��span�����������ó���bytes��ȵ�span�������ڸ������в���һ�������ڴ���span
	Span* GetOneSpan(SpanList* spanlist, size_t bytes);

	//��ThreadCache�е��ڴ��黹��CentralCache
	void ReleaseListToSpans(void* start, size_t byte);

private:
	SpanList _spanlist[NLISTS];//���Ļ����span��������飬Ĭ�ϴ�С��	NLISTS : 240

private:
	//���캯��Ĭ�ϻ���Ҳ�����޲�������
	//CentralCache() = default;
	CentralCache() {
		std::cout << "Central Cache ��������" << std::endl;
	}
	CentralCache(const CentralCache&)  = delete;
	CentralCache& operator=(const CentralCache&)  = delete;
#ifndef Lazy
	//����һ������
	static CentralCache _inst;
#endif // !Lazy

	
};