#include "lua.hpp"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include <list>
#include <vector>
#include <string>
#include <algorithm>
#include <limits>

#ifdef _WINDOWS
#include <boost/unordered_map.hpp>
#define MAP_CLASS_NAME boost::unordered_map
#define stricmp _stricmp
#else
#include <unordered_map>
#define MAP_CLASS_NAME std::unordered_map
#define stricmp strcasecmp
#endif

enum LuaRegistryFixedValue {
	kLuaRegVal_FFINew = -99990,
	kLuaRegVal_FFISizeof,
	kLuaRegVal_tostring,
	kLuaRegVal_ngx_re_match,
};

static inline size_t hashString(const char *str)
{
	const size_t seed = 131;
	size_t hash = 0;
	while (*str)
		hash = hash * seed + (*str++);

	return hash;
}
static inline size_t hashString(const char *str, uint32_t leng)
{
	const size_t seed = 131, seed2 = 131 * 131, seed3 = 131 * 131 * 131, seed4 = 131 * 131 * 131 * 131;
	uint32_t i = 0;
	size_t hash = 0;

	if (((uintptr_t)str & 0x3) == 0)
	{
		//如果地址是4的整倍数对齐的话...
		for (uint32_t t = leng >> 2 << 2; i < t; i += 4)
		{
			uint32_t v = *(uint32_t*)&str[i];
			hash = hash * seed4 + (v & 0xFF) * seed3 + ((v >> 8) & 0xFF) * seed2 + ((v >> 16) & 0xFF) * seed + (v >> 24);
		}
	}

	while (i < leng)
		hash = hash * seed + str[i ++];

	return hash;
}

template <typename T> static inline T alignbytes(T v)
{
#ifdef REEME_64
	return v + 7 >> 3 << 3;
#else
	return v + 3 >> 2 << 2;
#endif
}

//////////////////////////////////////////////////////////////////////////
union double2int
{
	double	dval;
	int32_t	i32;
} ;

static inline int32_t dtoi(double val)
{
	double2int u;
	u.dval = val + 6755399441055744.0;
	return u.i32;
}

//////////////////////////////////////////////////////////////////////////
struct StringPtrKey
{
	const char*		pString;
	size_t			nHashID;

	inline StringPtrKey()
		: pString(0)
		, nHashID(0)
	{
	}
	inline StringPtrKey(const char* name)
		: pString(name)
		, nHashID(hashString(name))
	{
	}
	inline StringPtrKey(const std::string& name)
	{
		pString = name.c_str();
		nHashID = hashString(pString);
	}
	inline StringPtrKey(const char* name, size_t len)
		: pString(name)
		, nHashID(hashString(name, len))
	{
	}

	inline operator size_t () const { return nHashID; }
	inline operator const char* () const { return pString; }
	inline void reset() { pString = 0; nHashID = 0; }
	inline const StringPtrKey& operator = (const char* s) { pString = s; nHashID = 0; return *this; }
	inline bool operator == (const StringPtrKey& key) const { return nHashID == key.nHashID && strcmp(pString, key.pString) == 0; }
	inline bool operator == (const std::string& str) const { return strcmp(pString, str.c_str()) == 0; }

	inline void copyto(std::string& str) const
	{
		str = pString;
	}
};
inline size_t hash_value(const StringPtrKey& k)
{
	if (k.nHashID == 0)
		return hashString(k.pString);
	return k.nHashID;
}
static inline bool operator < (const StringPtrKey& s1, const StringPtrKey& s2)
{
	return strcmp(s1.pString, s2.pString) < 0;
}
static inline bool operator > (const StringPtrKey& s1, const StringPtrKey& s2)
{
	return strcmp(s1.pString, s2.pString) > 0;
}
static inline int compare(const StringPtrKey& s1, const StringPtrKey& s2)
{
	return strcmp(s1.pString, s2.pString);
}

#ifndef _MSC_VER
namespace std {
	template <> struct hash<StringPtrKey>
	{
		size_t operator()(StringPtrKey const& k) const
		{
			if (k.nHashID == 0)
				return hashString(k.pString);
			return k.nHashID;
		}
	};

	template <> struct hash<StringPtrKeyL>
	{
		size_t operator()(StringPtrKeyL const& k) const
		{
			if (k.nHashID == 0)
				return hashString(k.pString);
			return k.nHashID;
		}
	};
}
#endif

//////////////////////////////////////////////////////////////////////////
#define TSORT_CUTOFF 8

template <typename T> struct defswap
{
	void operator () (char *a, char *b)
	{
		char tmp;
		if (a != b)
		{
			uint32_t width = sizeof(T);
			while (width -- > 0)
			{
				tmp = *a;
				*a ++ = *b;
				*b ++ = tmp;
			}
		}
	}
};

template <typename T> struct structswap
{
	char		tmp[sizeof(T)];

	void operator () (char *a, char *b)
	{
		if (a != b)
		{
			memcpy(tmp, a, sizeof(T));
			memcpy(a, b, sizeof(T));
			memcpy(b, tmp, sizeof(T));
		}
	}
};

template <typename T> struct greater
{
	int32_t operator () (const T& a, const T& b) { return a - b; }
};
template <typename T> struct less
{
	int32_t operator () (const T& a, const T& b) { return -(a - b); }
};

template <typename T, class fnComper, class fnSwap>
void tsort(T *base, uint32_t num, fnComper comp, fnSwap s)
{
	char *lo, *hi;
	char *mid;
	char *l, *h;
	uint32_t size;
	char *lostk[30], *histk[30];
	int stkptr;

	if (num < 2)
		return;

	stkptr = 0;

	lo = (char*)base;
	hi = (char *)base + sizeof(T) * (num - 1);
recurse:
	size = (hi - lo) / sizeof(T) + 1;

	if (size <= TSORT_CUTOFF)
	{
		char *p, *max;
		char *phi = hi, *plo = lo;

		while (phi > plo)
		{
			max = plo;
			for (p = plo + sizeof(T); p <= phi; p += sizeof(T))
			{
				if (comp(*((const T*)p), *((const T*)max)) > 0)
					max = p;
			}
			s(max, phi);
			phi -= sizeof(T);
		}
	}
	else
	{
		mid = lo + (size / 2) * sizeof(T);
		s(mid, lo);

		l = lo;
		h = hi + sizeof(T);

		for (;;)
		{
			do { l += sizeof(T); } while (l <= hi && comp(*((const T*)l), *((const T*)lo)) <= 0);
			do { h -= sizeof(T); } while (h > lo && comp(*((const T*)h), *((const T*)lo)) >= 0);
			if (h < l) break;
			s(l, h);
		}

		s(lo, h);

		if (h - 1 - lo >= hi - l)
		{
			if (lo + sizeof(T) < h)
			{
				lostk[stkptr] = lo;
				histk[stkptr] = h - sizeof(T);
				++ stkptr;
			}

			if (l < hi)
			{
				lo = l;
				goto recurse;
			}
		}
		else
		{
			if (l < hi)
			{
				lostk[stkptr] = l;
				histk[stkptr] = hi;
				++ stkptr;
			}

			if (lo + sizeof(T) < h)
			{
				hi = h - sizeof(T);
				goto recurse;
			}
		}
	}

	-- stkptr;
	if (stkptr >= 0)
	{
		lo = lostk[stkptr];
		hi = histk[stkptr];
		goto recurse;
	}
}

//////////////////////////////////////////////////////////////////////////
template <typename T> class TList;

template <typename T> class TListNode
{
	friend class TList<T>;
protected:
	typedef TListNode<T> Node;
	typedef TList<T> List;

	List			*m_pOwningList;
	Node			*m_pNext, *m_pPrevious;

public:
	inline TListNode()
		: m_pNext(NULL), m_pPrevious(NULL), m_pOwningList(NULL)
	{}

	inline T* next() const { return static_cast<T*>(m_pNext); }
	inline T* previous() const { return static_cast<T*>(m_pPrevious); }
} ;

template <typename T> class TList
{
protected:
	typedef TListNode<T> Node;

	Node			*m_pFirstNode, *m_pLastNode;
	size_t			m_nodesCount;

public:
	inline TList()
		: m_pFirstNode(NULL), m_pLastNode(NULL), m_nodesCount(0)
	{}

	void prepend(T* p)
	{
		assert(p);

		Node* n = static_cast<Node*>(p);
		assert(n->m_pOwningList == 0);

		if (m_pFirstNode)
		{
			m_pFirstNode->m_pPrevious = n;
			n->m_pNext = m_pFirstNode;
			m_pFirstNode = n;
		}
		else
		{
			m_pFirstNode = m_pLastNode = n;
			n->m_pNext = NULL;
		}

		n->m_pPrevious = NULL;
		n->m_pOwningList = this;
		m_nodesCount ++;
	}

	bool append(T* node)
	{
		Node* n = static_cast<Node*>(node);
		if (!n || n->m_pOwningList)
			return false;

		if (m_pLastNode)
		{
			m_pLastNode->m_pNext = n;
			n->m_pPrevious = m_pLastNode;
			m_pLastNode = n;
		}
		else
		{
			m_pFirstNode = m_pLastNode = n;
			n->m_pPrevious = NULL;
		}

		n->m_pNext = NULL;
		n->m_pOwningList = this;

		m_nodesCount ++;
		return true;
	}

	bool append(TList<T>& list)
	{
		m_nodesCount += list.size();

		Node* n = list.m_pFirstNode, *nn;
		while(n)
		{
			nn = n->m_pNext;

			if (m_pLastNode)
			{
				m_pLastNode->m_pNext = n;
				n->m_pPrevious = m_pLastNode;
				m_pLastNode = n;
			}
			else
			{
				m_pFirstNode = m_pLastNode = n;
				n->m_pPrevious = NULL;
			}

			n->m_pNext = NULL;
			n->m_pOwningList = this;

			n = nn;
		}

		list.m_pFirstNode = list.m_pLastNode = NULL;
		list.m_nodesCount = 0;

		return true;
	}

	bool remove(T* node)
	{
		Node* n = static_cast<Node*>(node);
		if (!n || n->m_pOwningList != this)
			return false;

		Node* pprev = n->m_pPrevious, *nnext = n->m_pNext;

		if (pprev)
			pprev->m_pNext = nnext;
		if (nnext)
			nnext->m_pPrevious = pprev;

		if (n == m_pFirstNode)
			m_pFirstNode = nnext;
		if (n == m_pLastNode)
			m_pLastNode = pprev;

		n->m_pPrevious = n->m_pNext = NULL;
		n->m_pOwningList = NULL;
		m_nodesCount --;

		return true;
	}

	void clear()
	{
		m_pFirstNode = m_pLastNode = 0;
		m_nodesCount = 0;
	}

	T* popFirst()
	{
		Node* n = m_pFirstNode;
		if (n)
		{
			m_pFirstNode = n->m_pNext;
			if (m_pFirstNode)
				m_pFirstNode->m_pPrevious = 0;
			if (m_pLastNode == n)
				m_pLastNode = m_pFirstNode;

			n->m_pPrevious = n->m_pNext = NULL;
			n->m_pOwningList = NULL;
			m_nodesCount --;
		}

		return static_cast<T*>(n);
	}

	T* popLast()
	{
		Node* pNode = m_pLastNode;
		if (pNode)
		{
			m_pLastNode = pNode->m_pPrevious;
			if (m_pLastNode)
				m_pLastNode->m_pNext = 0;
			if (m_pFirstNode == pNode)
				m_pFirstNode = m_pLastNode;

			pNode->m_pPrevious = pNode->m_pNext = NULL;
			pNode->m_pOwningList = NULL;

			m_nodesCount --;
		}

		return static_cast<T*>(pNode);
	}

	void insertBefore(T *p, T *before)
	{
		assert(p);

		Node* n = static_cast<Node*>(p);
		assert(n->m_pOwningList == 0);

		if (before)
		{
			assert(before->m_pOwningList == this);

			n->m_pNext = before;
			Node* after = ((Node*)before)->m_pPrevious;
			n->m_pPrevious = after;
			if (after) after->m_pNext = n;
			else m_pFirstNode = n;
			((Node*)before)->m_pPrevious = n;
			n->m_pOwningList = this;

			m_nodesCount ++;
		}
		else
		{
			prepend(p);
		}
	}

	void insertAfter(T *p, T *after)
	{
		assert(p);

		Node* n = static_cast<Node*>(p);
		assert(n->m_pOwningList == 0);

		if (after)
		{
			assert(after->m_pOwningList == this);

			n->m_pPrevious = after;
			Node* before = ((Node*)after)->m_pNext;
			n->m_pNext = before;
			if (before) before->m_pPrevious = n;
			else m_pLastNode = n;
			((Node*)after)->m_pNext = n;
			n->m_pOwningList = this;

			m_nodesCount ++;
		}
		else
		{
			append(p);
		}
	}

	inline T* first() const { return static_cast<T*>(m_pFirstNode); }
	inline T* last() const { return static_cast<T*>(m_pLastNode); }

	inline size_t size() const { return m_nodesCount; }
};

//////////////////////////////////////////////////////////////////////////
class TMemNode : public TListNode<TMemNode>
{
public:
	size_t		used;
	size_t		total;

	inline operator char* () { return (char*)(this + 1); }
	inline operator const char* () { return (const char*)(this + 1); }

	inline operator void* () { return (void*)(this + 1); }
	inline operator const void* () { return (const void*)(this + 1); }
};

const size_t TMEMNODESIZE = 8192 - sizeof(TMemNode);

class TMemList : public TList<TMemNode>
{
public:
	inline TMemList() {}
	~TMemList()
	{
		TMemNode* n;
		while((n = popFirst()) != 0)
			free(n);
	}

	TMemNode* newNode(size_t size = TMEMNODESIZE)
	{
		TMemNode* n = (TMemNode*)malloc(size + sizeof(TMemNode));
		new (n) TMemNode();
		n->total = size;
		n->used = 0;

		append(n);
		return n;
	}
	TMemNode* wrapNode(char* buf, size_t fixedBufSize)
	{
		assert(fixedBufSize >= sizeof(TMemNode) + 16);

		TMemNode* n = (TMemNode*)buf;
		new (n) TMemNode();
		n->total = fixedBufSize - sizeof(TMemNode);
		n->used = 0;

		append(n);
		return n;
	}
	void addChar(char ch)
	{
		TMemNode* n = (TMemNode*)m_pLastNode;
		if (n->used >= n->total)
			n = newNode();

		char* ptr = (char*)(n + 1);
		ptr[n->used ++] = ch;
	}
	void addChar2(char ch1, char ch2)
	{
		TMemNode* n = (TMemNode*)m_pLastNode;
		if (n->used + 1 >= n->total)
			n = newNode();

		char* ptr = (char*)(n + 1);
		size_t used = n->used;
		ptr[used] = ch1;
		ptr[used + 1] = ch2;
		n->used += 2;
	}
	void addString(const char* s, size_t len)
	{
		TMemNode* n = (TMemNode*)m_pLastNode;
		char* ptr = (char*)(n + 1);

		size_t copy = std::min(n->total - n->used, len);
		memcpy(ptr + n->used, s, copy);
		len -= copy;
		n->used += copy;

		if (len > 0)
		{
			// 剩下的直接一次分配够
			n = newNode(std::max(TMEMNODESIZE, len));
			ptr = (char*)(n + 1);

			memcpy(ptr + n->used, s + copy, len);
			n->used += len;
		}
	}
	char* reserve(size_t len)
	{
		char* ptr;
		TMemNode* n = (TMemNode*)m_pLastNode;

		// 最后一个节点剩下的空间不够需要保留的话，就直接分配一个全新的至少有len这么大的
		if (n->used + len < n->total)
		{
			ptr = (char*)(n + 1);
			ptr += n->used;
		}
		else
		{
			n = newNode(std::max(TMEMNODESIZE, len));
			ptr = (char*)(n + 1);
		}

		n->used += len;
		return ptr;
	}

	char* joinAll(size_t* pnTotal = 0)
	{
		size_t total = 0;
		TMemNode* n = first();

		while (n)
		{
			total += n->used;
			n = n->next();
		}

		char* dst = (char*)malloc(total), *ptr = dst;
		while ((n = popFirst()) != NULL)
		{
			memcpy(ptr, (char*)(n + 1), n->used);
			ptr += n->used;
			free(n);
		}

		if (pnTotal)
			*pnTotal = total;
		return dst;
	}
} ;

//////////////////////////////////////////////////////////////////////////

#include "crtopt.h"
#include "json.h"
#include "lua_utf8str.h"
#include "lua_string.h"
#include "lua_table.h"

#pragma comment(lib, "lua51")

extern "C" __declspec(dllexport) 
#ifdef _WINDOWS
int luaopen_strext(lua_State* L)
#else
int luaopen_libstrext(lua_State* L)
#endif
{
	int top = lua_gettop(L);

	luaext_string(L);
	luaext_table(L);
	luaext_utf8str(L);

	lua_settop(L, top);

	// 缓存一些函数方便C来调用
	lua_getglobal(L, "require");
	lua_pushliteral(L, "ffi");
	lua_pcall(L, 1, 1, 0);

	lua_getfield(L, -1, "new");
	lua_rawseti(L, LUA_REGISTRYINDEX, kLuaRegVal_FFINew);

	lua_getfield(L, -1, "sizeof");
	lua_rawseti(L, LUA_REGISTRYINDEX, kLuaRegVal_FFISizeof);

	lua_getglobal(L, "tostring");
	lua_rawseti(L, LUA_REGISTRYINDEX, kLuaRegVal_tostring);

	lua_getglobal(L, "ngx");
	lua_getfield(L, -1, "re");
	lua_getfield(L, -1, "match");
	lua_rawseti(L, LUA_REGISTRYINDEX, kLuaRegVal_ngx_re_match);

	lua_settop(L, top);

	return 1;
}