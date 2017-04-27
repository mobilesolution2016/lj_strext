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

#define USE_RE2
#ifdef USE_RE2
#	include <re2/re2.h>
#	include <re2/set.h>
#endif

#include "gbk2unicode.c"
#include "unicode2gbk.c"

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
};

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
};

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
		while (n)
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
		while ((n = popFirst()) != 0)
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
};

//////////////////////////////////////////////////////////////////////////
// 从双字中取单字节
#define B0(a) (a & 0xFF)
#define B1(a) (a >> 8 & 0xFF)
#define B2(a) (a >> 16 & 0xFF)
#define B3(a) (a >> 24 & 0xFF)

static inline char GetB64Char(int32_t index)
{
	const char szBase64Table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	if (index >= 0 && index < 64)
		return szBase64Table[index];

	return '=';
}

static uint8_t B64Indices[128] = { 0 };
struct B64IndexInit
{
	B64IndexInit()
	{
		for (uint8_t ch = 0; ch < 128; ++ ch)
		{
			if (ch >= 'A' && ch <= 'Z')
				B64Indices[ch] = ch - 'A';
			else if (ch >= 'a' && ch <= 'z')
				B64Indices[ch] = ch - 'a' + 26;
			else if (ch >= '0' && ch <= '9')
				B64Indices[ch] = ch - '0' + 52;
			else if (ch == '+')
				B64Indices[ch] = 62;
			else if (ch == '/')
				B64Indices[ch] = 63;
		}
	}
} _g_B64IndexInit;

// 编码后的长度一般比原文多占1/3的存储空间，请保证base64code有足够的空间
size_t Base64Encode(const void* src, size_t src_len, std::string& buf)
{
	size_t i, len = 0, begin = buf.length();
	unsigned char* psrc = (unsigned char*)src;

	buf.resize(begin + src_len + (src_len >> 1));
	char* p64 = const_cast<char*>(buf.c_str()) + begin;

	for (i = 0; src_len > 3 && i < src_len - 3; i += 3)
	{
		unsigned ulTmp = *(unsigned*)psrc;
		int32_t b0 = GetB64Char((B0(ulTmp) >> 2) & 0x3F);
		int32_t b1 = GetB64Char((B0(ulTmp) << 6 >> 2 | B1(ulTmp) >> 4) & 0x3F);
		int32_t b2 = GetB64Char((B1(ulTmp) << 4 >> 2 | B2(ulTmp) >> 6) & 0x3F);
		int32_t b3 = GetB64Char((B2(ulTmp) << 2 >> 2) & 0x3F);
		*((unsigned*)p64) = b0 | b1 << 8 | b2 << 16 | b3 << 24;
		len += 4;
		p64 += 4;
		psrc += 3;
	}

	// 处理最后余下的不足3字节的饿数据
	if (i < src_len)
	{
		ptrdiff_t rest = src_len - i;
		unsigned long ulTmp = 0;
		for (int32_t j = 0; j < rest; ++j)
			*(((unsigned char*)&ulTmp) + j) = *psrc++;

		p64[0] = GetB64Char((B0(ulTmp) >> 2) & 0x3F);
		p64[1] = GetB64Char((B0(ulTmp) << 6 >> 2 | B1(ulTmp) >> 4) & 0x3F);
		p64[2] = rest > 1 ? GetB64Char((B1(ulTmp) << 4 >> 2 | B2(ulTmp) >> 6) & 0x3F) : '=';
		p64[3] = rest > 2 ? GetB64Char((B2(ulTmp) << 2 >> 2) & 0x3F) : '=';
		p64 += 4;
		len += 4;
	}

	buf.resize(begin + len);
	return len;
}


// 解码后的长度一般比原文少用占1/4的存储空间，请保证buf有足够的空间
size_t Base64Decode(const void* base64code, size_t src_len, std::string& buf)
{
	size_t i, len = 0, begin = buf.length();
	unsigned char* psrc = (unsigned char*)base64code;

	buf.resize(begin + src_len);
	char* pBuf = const_cast<char*>(buf.c_str()) + begin;

	for (i = 0; i < src_len - 4 && src_len >= 4; i += 4)
	{
		uint32_t ulTmp = *(uint32_t*)psrc;

		uint32_t b0 = (B64Indices[(uint8_t)B0(ulTmp)] << 2 | B64Indices[(uint8_t)B1(ulTmp)] << 2 >> 6) & 0xFF;
		uint32_t b1 = (B64Indices[(uint8_t)B1(ulTmp)] << 4 | B64Indices[(uint8_t)B2(ulTmp)] << 2 >> 4) & 0xFF;
		uint32_t b2 = (B64Indices[(uint8_t)B2(ulTmp)] << 6 | B64Indices[(uint8_t)B3(ulTmp)] << 2 >> 2) & 0xFF;

		*((uint32_t*)pBuf) = b0 | b1 << 8 | b2 << 16;
		psrc += 4;
		pBuf += 3;
		len += 3;
	}

	// 处理最后余下的不足4字节的饿数据
	if (i < src_len)
	{
		ptrdiff_t rest = src_len - i;
		unsigned long ulTmp = 0;
		for (int32_t j = 0; j < rest; ++j)
			*(((uint8_t*)&ulTmp) + j) = *psrc++;

		uint32_t b0 = (B64Indices[(uint8_t)B0(ulTmp)] << 2 | B64Indices[(uint8_t)B1(ulTmp)] << 2 >> 6) & 0xFF;
		*pBuf++ = b0;
		len ++;

		if ('=' != B1(ulTmp) && '=' != B2(ulTmp))
		{
			uint32_t b1 = (B64Indices[(uint8_t)B1(ulTmp)] << 4 | B64Indices[(uint8_t)B2(ulTmp)] << 2 >> 4) & 0xFF;
			*pBuf++ = b1;
			len ++;
		}

		if ('=' != B2(ulTmp) && '=' != B3(ulTmp))
		{
			uint32_t b2 = (B64Indices[(uint8_t)B2(ulTmp)] << 6 | B64Indices[(uint8_t)B3(ulTmp)] << 2 >> 2) & 0xFF;
			*pBuf++ = b2;
			len ++;
		}
	}

	buf.resize(begin + len);
	return len;
}

size_t urlDecode(char* str, size_t leng)
{
	if (!str)
		return 0;

	if (leng == -1)
		leng = strlen(str);
	if (leng == 0)
		return 0;

	uint8_t ch1, ch2;
	char* read = str, *write = str, ch;
	while (read - str < leng)
	{
		ch = *read++;
		if (ch == '+')
		{
			*write ++ = ' ';
		}
		else if (ch == '%')
		{
			if (read[0] >= '0' && read[0] <= '9')
				ch1 = (uint8_t)(read[0] - '0');
			else if (read[0] >= 'a' && read[0] <= 'f')
				ch1 = (uint8_t)(read[0] - 'a' + 10);
			else if (read[0] >= 'A' && read[0] <= 'F')
				ch1 = (uint8_t)(read[0] - 'A' + 10);
			else
			{
				*write++ = '%';
				continue;
			}

			if (read[1] >= '0' && read[1] <= '9')
				ch2 = (uint8_t)(read[1] - '0');
			else if (read[1] >= 'a' && read[1] <= 'f')
				ch2 = (uint8_t)(read[1] - 'a' + 10);
			else if (read[1] >= 'A' && read[1] <= 'F')
				ch2 = (uint8_t)(read[1] - 'A' + 10);
			else
			{
				*write++ = '%';
				continue;
			}

			read += 2;
			*write++ = ((ch1 & 0xF) << 4) | (ch2 & 0xF);
		}
		else
		{
			*write++ = ch;
		}
	}

	*write = 0;
	return write - str;
}

static char url_codes[256][4] = { 0 };
static void init_urlcodes()
{
	if (url_codes['%'][0] == 0)
	{
		//第一次，所以需要初始化
		url_codes['%'][3] = 1;
		url_codes['&'][3] = 1;
		url_codes[' '][3] = 1;
		url_codes['='][3] = 1;
		url_codes['/'][3] = 1;
		url_codes['#'][3] = 1;
		url_codes['+'][3] = 1;
		url_codes['\\'][3] = 1;
		url_codes[':'][3] = 1;
		url_codes[';'][3] = 1;
		url_codes['/'][3] = 1;

		for (uint32_t i = 1; i < 256; ++i)
		{
			uint8_t* p = (uint8_t*)url_codes[i];
			p[0] = '%';

			p[1] = i >> 4;
			if (p[1] > 9)
				p[1] = p[1] - 10 + 'a';
			else
				p[1] += '0';

			p[2] = i & 0xF;
			if (p[2] > 9)
				p[2] = p[2] - 10 + 'a';
			else
				p[2] += '0';
		}
	}
}

void urlEncode(const char* str, uint32_t leng, std::string& outbuf)
{
	init_urlcodes();

	char chars[3];
	uint8_t* pstr = (uint8_t*)str;
	for (uint32_t i = 0, l; i < leng; )
	{
		uint8_t ch = pstr[i];
		if (!(ch & 0x80))
		{
			//1 ascii
			outbuf.append(url_codes[ch], 3);
			++ i;
		}
		else
		{
			if ((ch & 0xE0) == 0xC0)
				l = 2;
			else if ((ch & 0xF0) == 0xE0)
				l = 3;
			else if ((ch & 0xF8) == 0xF0)
				l = 4;
			else if ((ch & 0xFC) == 0xF8)
				l = 5;
			else
				l = 6;

			while (l -- > 0)
			{
				const char* src = (const char*)url_codes[pstr[i ++]];
				outbuf.append(src, 3);
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
template <typename T> inline T UTF82SinleLen(T pBuffer)
{
	uint32_t hiChar = pBuffer[0];
	if (!(hiChar & 0x80))
		pBuffer ++;
	else if ((hiChar & 0xE0) == 0xC0)
		pBuffer += 2;
	else if ((hiChar & 0xF0) == 0xE0)
		pBuffer += 3;
	else if ((hiChar & 0xF8) == 0xF0)
		pBuffer += 4;
	else if ((hiChar & 0xFC) == 0xF8)
		pBuffer += 5;
	else
		pBuffer += 6;

	return pBuffer;
}
template <typename T> inline T UTF82UnicodeOne(T pBuffer, uint32_t& unicode)
{
	uint32_t ret;
	uint8_t hiChar = pBuffer[0];
	if (!(hiChar & 0x80))
	{
		unicode = hiChar;
		return pBuffer + 1;
	}

	if ((hiChar & 0xE0) == 0xC0)
	{
		//2 bit count
		ret = hiChar & 0x1F;
		ret = (ret << 6) | (pBuffer[1] & 0x3F);
		pBuffer += 2;
	}
	else if ((hiChar & 0xF0) == 0xE0)
	{
		//3 bit count
		ret = hiChar & 0xF;
		ret = (ret << 6) | (pBuffer[1] & 0x3F);
		ret = (ret << 6) | (pBuffer[2] & 0x3F);
		pBuffer += 3;
	}
	else if ((hiChar & 0xF8) == 0xF0)
	{
		//4 bit count
		ret = hiChar & 0x7;
		ret = (ret << 6) | (pBuffer[1] & 0x3F);
		ret = (ret << 6) | (pBuffer[2] & 0x3F);
		ret = (ret << 6) | (pBuffer[3] & 0x3F);
		pBuffer += 4;
	}
	else if ((hiChar & 0xFC) == 0xF8)
	{
		//5 bit count
		ret = hiChar & 0x3;
		ret = (ret << 6) | (pBuffer[1] & 0x3F);
		ret = (ret << 6) | (pBuffer[2] & 0x3F);
		ret = (ret << 6) | (pBuffer[3] & 0x3F);
		ret = (ret << 6) | (pBuffer[4] & 0x3F);
		pBuffer += 5;
	}
	else
	{
		//6 bit count
		ret = hiChar & 0x1;
		ret = (ret << 6) | (pBuffer[1] & 0x3F);
		ret = (ret << 6) | (pBuffer[2] & 0x3F);
		ret = (ret << 6) | (pBuffer[3] & 0x3F);
		ret = (ret << 6) | (pBuffer[4] & 0x3F);
		ret = (ret << 6) | (pBuffer[5] & 0x3F);
		pBuffer += 6;
	}

	unicode = ret;
	return pBuffer;
}

template <typename T> uint32_t UnicodeOne2UTF8(const uint32_t code, T p)
{
	//第二遍输出
	if (code < 0xC0)
	{
		*p ++ = (char)code;
		return 1;
	}
	if (code < 0x800)
	{
		p[0] = 0xc0 | (code >> 6);
		p[1] = 0x80 | (code & 0x3f);
		return 2;
	}
	if (code < 0x10000)
	{
		p[0] = 0xe0 | (code >> 12);
		p[1] = 0x80 | ((code >> 6) & 0x3f);
		p[2] = 0x80 | (code & 0x3f);
		return 3;
	}
	if (code < 0x200000)
	{
		p[0] = 0xf0 | (code >> 18);
		p[1] = 0x80 | ((code >> 12) & 0x3f);
		p[2] = 0x80 | ((code >> 6) & 0x3f);
		p[3] = 0x80 | (code & 0x3f);
		return 4;
	}
	if (code < 0x4000000)
	{
		p[0] = 0xf8 | (code >> 24);
		p[1] = 0x80 | ((code >> 18) & 0x3f);
		p[2] = 0x80 | ((code >> 12) & 0x3f);
		p[3] = 0x80 | ((code >> 6) & 0x3f);
		p[4] = 0x80 | (code & 0x3f);
		return 5;
	}

	p[0] = 0xfc | (code >> 30);
	p[1] = 0x80 | ((code >> 24) & 0x3f);
	p[2] = 0x80 | ((code >> 18) & 0x3f);
	p[3] = 0x80 | ((code >> 12) & 0x3f);
	p[4] = 0x80 | ((code >> 6) & 0x3f);
	p[5] = 0x80 | (code & 0x3f);
	return 6;
}

size_t Unicode2GBK(uint32_t uni, char* gbk)
{
	if (uni <= 0x7F)
	{
		gbk[0] = uni;
		return 1;
	}
	
	if (uni == 0x20AC)
	{
		gbk[0] = 0x80;
		return 1;
	}

	uint16_t ss = unicode2gbkTable[uni - 128];
	gbk[0] = ss >> 8;
	gbk[1] = ss & 0xFF;
	
	return 2;
}

size_t GBK2UTF8(const char* gbk, size_t leng, char* utf8output)
{
	uint8_t *utf8 = (uint8_t*)utf8output;

	for (size_t i = 0, uni; i < leng; ++ i)
	{
		uint8_t ch = gbk[i];
		if (ch <= 0x7F)
		{
			*utf8 ++ = ch;
		}
		else if (ch == 0x80)
		{
			utf8[0] = 0xe0 | (0x20AC >> 12);
			utf8[1] = 0x80 | ((0x20AC >> 6) & 0x3f);
			utf8[2] = 0x80 | (0x20AC & 0x3f);
			utf8 += 3;
			++ i;
		}
		else
		{
			++ i;
			ch -= 0x81;
			if (ch < sizeof(gbk2unicodeTables) / sizeof(gbk2unicodeTables[0]))
			{
				const uint16_t* pTable = gbk2unicodeTables[ch];
				ch = gbk[i];
				if (ch < 255)
					uni = pTable[ch - 0x40];
				else
					uni = 0;

				if (uni < 0xC0)
				{
					*utf8 ++ = uni;
				}
				else if (uni < 0x800)
				{
					utf8[0] = 0xc0 | (uni >> 6);
					utf8[1] = 0x80 | (uni & 0x3f);
					utf8 += 2;
				}
				else if (uni < 0x10000)
				{
					utf8[0] = 0xe0 | (uni >> 12);
					utf8[1] = 0x80 | ((uni >> 6) & 0x3f);
					utf8[2] = 0x80 | (uni & 0x3f);
					utf8 += 3;
				}
				else if (uni < 0x200000)
				{
					utf8[0] = 0xf0 | (uni >> 18);
					utf8[1] = 0x80 | ((uni >> 12) & 0x3f);
					utf8[2] = 0x80 | ((uni >> 6) & 0x3f);
					utf8[3] = 0x80 | (uni & 0x3f);
					utf8 += 4;
				}
			}
		}
	}

	return (char*)utf8 - utf8output;
}

size_t UTF82GBK(const char* utf8, size_t leng, char* gbkoutput)
{
	uint32_t unicode;
	char* gbk = gbkoutput;
	const char* src = utf8;

	while(src < utf8 + leng)
	{
		src = UTF82UnicodeOne(src, unicode);
		gbk += Unicode2GBK(unicode, gbk);
	}

	return gbk - gbkoutput;
}

//////////////////////////////////////////////////////////////////////////
static int lua_string_addbuf(luaL_Buffer* buf, const char* str, size_t len, bool toGbk = false)
{
	char gbk[640];
	int addBuf = 0;
	std::string gbkBuf;
	size_t lenleft = len, copy, gbklen;

	while (lenleft > 0)
	{
		copy = std::min(lenleft, (size_t)(LUAL_BUFFERSIZE - (buf->p - buf->buffer)));
		if (!copy)
		{
			addBuf ++;
			luaL_prepbuffer(buf);
			copy = std::min(lenleft, (size_t)LUAL_BUFFERSIZE);
		}

		if (toGbk)
		{
			if (copy <= 640)
			{				
				gbklen = UTF82GBK(str, copy, gbk);
				memcpy(buf->p, gbk, gbklen);
			}
			else
			{				
				gbkBuf.resize(len);

				char* gbkbuf = const_cast<char*>(gbkBuf.data());
				gbklen = UTF82GBK(str, copy, gbkbuf);
				memcpy(buf->p, gbkbuf, gbklen);
			}
			buf->p += gbklen;
		}
		else
		{
			memcpy(buf->p, str, copy);
			buf->p += copy;
		}

		lenleft -= copy;		
		str += copy;
	}

	return addBuf;
}

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

	return 1;
}