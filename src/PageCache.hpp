#include "Common.hpp"
#include <iostream>
#include <memory>
#include <mutex>

//����Page CacheҲҪ����Ϊ����������Central Cache��ȡspan��ʱ��
//ÿ�ζ��Ǵ�ͬһ��page�����л�ȡspan
#define Lazy

class PageCache; // ǰ������
typedef std::shared_ptr<PageCache> SPtr;

class PageCache
{
private:
	//NPAGES��129������ʹ��128������Ԫ�أ�Ҳ�����±��1��ʼ��128�ֱ�Ϊ1ҳ��128ҳ
	SpanList _pagelist[NPAGES];

private:
	//PageCache() = default;
	PageCache() {
		std::cout << "Page Cache ��������" << std::endl;
	}
	PageCache(const PageCache&) = delete;
	PageCache& operator=(const PageCache&) = delete;

#ifdef Lazy
	static std::mutex _mutex_Page;
	static SPtr _inst_ptr; //��̬����ҲҪ��ʼ��������ȫ�־�̬�����������volatile�����а���java�п���
#else
	static PageCache _inst;
#endif // !Lazy

	std::mutex _mtx;
	std::unordered_map<PageID, Span*> _id_span_map;


public:
	~PageCache() {
		std::cout << "Page Cache ��������" << std::endl;
	}
	

#ifdef Lazy
	////����ģʽ
	//static PageCache* GetInstance() {
	//	static PageCache _inst;
	//	return &_inst;
	//}

	static SPtr GetInstance() {
		if (_inst_ptr == nullptr) {
			std::lock_guard<std::mutex> lock(_mutex_Page);
			if (_inst_ptr == nullptr) {
				/*_inst_ptr = std::unique_ptr<PageCache>(new PageCache);*/ // �ǲ��еģ���Ϊunique_ptr�ĸ��ƹ��캯���͸��Ƹ�ֵ���캯���ѱ�ɾ�����޷�����
				_inst_ptr = std::shared_ptr<PageCache>(new PageCache);
			}
		}
		return _inst_ptr;
	}

#else
	/*����ģʽ*/
	static PageCache* GetInstance()
	{
		return &_inst;
	}
#endif // Lazy

	
	Span* AllocBigPageObj(size_t size);
	void FreeBigPageObj(void* ptr, Span* span);

	//��ϵͳ����span���ߴ���Ҫ�����npage��Pagespan������
	Span* NewSpan(size_t npage);

	Span* _NewSpan(size_t npage);

	// ��ȡ�Ӷ���span��ӳ��
	Span* MapObjectToSpan(void* obj);

	//��CentralCache�黹span��Page
	void RelaseToPageCache(Span* span);


};


