#include "Common.hpp"
#include <iostream>
#include <memory>
#include <mutex>

//对于Page Cache也要设置为单例，对于Central Cache获取span的时候
//每次都是从同一个page数组中获取span
#define Lazy

class PageCache; // 前向声明
typedef std::shared_ptr<PageCache> SPtr;

class PageCache
{
private:
	//NPAGES是129，但是使用128个数据元素，也就是下标从1开始到128分别为1页到128页
	SpanList _pagelist[NPAGES];

private:
	//PageCache() = default;
	PageCache() {
		std::cout << "Page Cache 被构造了" << std::endl;
	}
	PageCache(const PageCache&) = delete;
	PageCache& operator=(const PageCache&) = delete;

#ifdef Lazy
	static std::mutex _mutex_Page;
	static SPtr _inst_ptr; //静态变量也要初始化。。。全局静态变量，这里加volatile好像不行啊，java中可以
#else
	static PageCache _inst;
#endif // !Lazy

	std::mutex _mtx;
	std::unordered_map<PageID, Span*> _id_span_map;


public:
	~PageCache() {
		std::cout << "Page Cache 被析构了" << std::endl;
	}
	

#ifdef Lazy
	////懒汉模式
	//static PageCache* GetInstance() {
	//	static PageCache _inst;
	//	return &_inst;
	//}

	static SPtr GetInstance() {
		if (_inst_ptr == nullptr) {
			std::lock_guard<std::mutex> lock(_mutex_Page);
			if (_inst_ptr == nullptr) {
				/*_inst_ptr = std::unique_ptr<PageCache>(new PageCache);*/ // 是不行的，因为unique_ptr的复制构造函数和复制赋值构造函数已被删除且无法调用
				_inst_ptr = std::shared_ptr<PageCache>(new PageCache);
			}
		}
		return _inst_ptr;
	}

#else
	/*饿汉模式*/
	static PageCache* GetInstance()
	{
		return &_inst;
	}
#endif // Lazy

	
	Span* AllocBigPageObj(size_t size);
	void FreeBigPageObj(void* ptr, Span* span);

	//从系统申请span或者大于要申请的npage的Pagespan中申请
	Span* NewSpan(size_t npage);

	Span* _NewSpan(size_t npage);

	// 获取从对象到span的映射
	Span* MapObjectToSpan(void* obj);

	//从CentralCache归还span到Page
	void RelaseToPageCache(Span* span);


};


