// ������ASCII�ַ����Ա�1��ʾ���ţ�2��ʾ��Сд��ĸ��3��ʾ���֣�4��ʾ��������ķ��ţ�5��6�ǵ�ʽ����
static uint8_t sql_where_splits[128] = 
{
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	1,		// 0~32
	6, 1, 1, 1, 1, 1, 4, 1, 1, 4, 1, 1, 2, 2, 1,	// 33~47
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3,	// 48~57
	1, 1, 6, 5, 6, 4, 1,	// 58~64
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,	// 65~92
	1, 1, 1, 1, 2, 1,	// 91~96
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,	// 97~122
	1, 1, 1, 1, 1,
};

enum StringSplitFlags {
	kSplitAsKey = 0x40000000,
	kSplitTrim = 0x20000000,
};

enum StringJsonFlags {
	kJsonNoCopy = 0x80000000,
	kJsonRetCData = 0x40000000,
	kJsonLuaString = 0x20000000,
	kJsonUnicodes = 0x10000000,
	kJsonDropNull = 0x08000000,
	kJsonSimpleEscape = 0x04000000,
	kJsonEmptyToObject = 0x02000000
};

enum StringHtmlEntFlags {
	kHtmlEntReservedOnly = 0x1,
};

//////////////////////////////////////////////////////////////////////////

struct BMString
{
	uint32_t	m_flags;
	uint32_t	m_hasNext;
	size_t		m_subLen;
	size_t		m_subMaxLen;	

	void setSub(const char* src, size_t len)
	{
#ifdef REEME_64
		m_subMaxLen = len + 7 >> 3 << 3;
#else
		m_subMaxLen = len + 3 >> 2 << 2;
#endif
		m_subLen = len;
		memcpy(this + 1, src, len);
	}

	void makePreTable()
	{
		size_t pos, i;
		uint8_t* sub = (uint8_t*)(this + 1);
		size_t* pSkips = (size_t*)(sub + m_subMaxLen);
		size_t* pShifts = pSkips + 256;

		memset(pShifts, 0, sizeof(size_t) * m_subLen);

		for (i = 0; i < 256; ++ i)
			pSkips[i] = m_subLen;

		for (pos = m_subLen - 1; ; -- pos)
		{
			uint8_t *good = sub + pos + 1;
			size_t goodLen = m_subLen - pos - 1;

			while (goodLen > 0)
			{
				uint8_t *p = sub + m_subLen - 1 - goodLen;
				while (p >= sub)
				{
					if (memcmp(p, good, goodLen) == 0)
					{
						pShifts[pos] = (m_subLen - pos) + (good - p) - 1;
						break;
					}
					p --;
				}

				if (pShifts[pos] != 0)
					break;

				good ++;
				goodLen --;
			}

			if (pShifts[pos] == 0)
				pShifts[pos] = m_subLen - pos;

			if (pos == 0)
				break;
		}

		i = m_subLen;
		while (i)
			pSkips[*sub ++] = -- i;
	}

	BMString* getNext() const
	{
		if (!m_hasNext)
			return 0;

		uint8_t* sub = (uint8_t*)(this + 1);
		size_t* pTbl = (size_t*)(sub + m_subMaxLen);

		return (BMString*)(pTbl + 256 + m_subLen);
	}

	size_t find(const uint8_t* src, size_t srcLen) const
	{
		const uint8_t* sub = (const uint8_t*)(this + 1);
		const size_t* pSkips = (const size_t*)(sub + m_subMaxLen);
		const size_t* pShifts = pSkips + 256;

		size_t lenSub1 = m_subLen - 1;
		size_t strEnd = lenSub1, subEnd;
		while (strEnd <= srcLen)
		{
			subEnd = lenSub1;
			while (src[strEnd] == sub[subEnd])
			{
				if (subEnd == 0)
					return strEnd;

				strEnd --;
				subEnd --;
			}

			strEnd += std::max(pSkips[src[strEnd]], pShifts[subEnd]);
		}

		return -1;
	}

	static size_t calcAllocSize(size_t subLen)
	{
		size_t sub;
#ifdef REEME_64
		sub = subLen + 7 >> 3 << 3;
#else
		sub = subLen + 3 >> 2 << 2;
#endif
		return (subLen + 256) * sizeof(size_t) + sizeof(BMString) + sub;
	}
};


//////////////////////////////////////////////////////////////////////////
static int lua_string_at(lua_State* L)
{
	size_t len;
	int top = lua_gettop(L), pos, i, cc = 0;
	const char* s = luaL_checklstring(L, 1, &len);

	for (i = 2; i <= top; ++ i)
	{
		pos = lua_tointeger(L, 2);
		if (pos >= 1 && pos <= len)
		{
			++ cc;
			lua_pushlstring(L, s + pos - 1, 1);
		}
	}
	return cc;
}

// ������1�ַ������ղ���2�ַ����г��ֵ�ÿһ���ַ������з֣��з�ʱ�����ݲ���3�����õı�־������Ӧ�Ĵ����������4������Ϊtrue���зֵĽ�����Զ෵��ֵ���أ�������table�����зֵĽ��
// ��ʹ����1�ַ�����ȫ�����в���2�ַ����е��κ�һ���ַ���Ҳ�����һ���з֣�ֻ�Ƿ���Ϊ��������1�ַ���
static int lua_string_split(lua_State* L)
{
	int callback = 0;
	bool exists = false;	
	uint8_t checker[256] = { 0 }, ch;
	size_t byLen = 0, srcLen = 0, start = 0;
	uint32_t nFlags = 0, maxSplits = 0, cc = 0;
	int top = lua_gettop(L), retAs = LUA_TTABLE, tblVal = 3;

	const uint8_t* src = (const uint8_t*)luaL_checklstring(L, 1, &srcLen);
	const uint8_t* by = (const uint8_t*)luaL_checklstring(L, 2, &byLen);	

	retAs = lua_type(L, 3);
	if (retAs == LUA_TNUMBER)
	{
		nFlags = luaL_optinteger(L, 3, 0);
		maxSplits = nFlags & 0x0FFFFFFF;

		if (top >= 4)
			retAs = lua_type(L, tblVal = 4);
	}

	if (retAs == LUA_TBOOLEAN)
	{
		// is true return plained string(s)
		if (lua_toboolean(L, tblVal))
			retAs = LUA_TNIL;
	}
	else if (retAs == LUA_TTABLE)
	{
		// is table then use it directly
		cc = lua_objlen(L, tblVal);
		exists = true;
	}
	else if (retAs == LUA_TFUNCTION)
	{
		callback = tblVal;
		retAs = LUA_TNUMBER;
	}
	else
	{
		retAs = LUA_TTABLE;
	}

	if (retAs == LUA_TTABLE)
	{
		if (!exists)
		{
			int n1 = std::max(maxSplits, 4U), n2 = 0;

			if (nFlags & kSplitAsKey)
				std::swap(n1, n2);

			lua_createtable(L, n1, n2);
			tblVal = lua_gettop(L);
		}
	}
	else
		tblVal = 0;

	if (maxSplits == 0)
		maxSplits = 0x0FFFFFFF;

	// ���ñ��
	size_t i, endpos;
	for(i = 0; i < byLen; ++ i)
		checker[by[i]] = 1;
	
	// ����ַ��ļ��	
	for (i = endpos = 0; i < srcLen; ++ i)
	{
		ch = src[i];
		if (!checker[ch])
			continue;

		endpos = i;
		if (nFlags & kSplitTrim)
		{
			// trim
			while(start < endpos && src[start] <= 32)
				start ++;
			while(start < endpos && src[endpos - 1] <= 32)
				endpos --;
		}

		if (start < endpos)
		{
		_lastseg:
			if (callback)
			{
				// callback
				lua_pushvalue(L, tblVal);
				lua_pushinteger(L, cc + 1);
				lua_pushlstring(L, (const char*)src + start, endpos - start);
				if (lua_pcall(L, 2, 1, 0))
					return 0;
				if (lua_isboolean(L, -1) && lua_toboolean(L, -1))
					break;

				lua_settop(L, top);
			}
			else if (tblVal)
			{
				// push result
				lua_pushlstring(L, (const char*)src + start, endpos - start);
				if (nFlags & kSplitAsKey)
				{
					// as key
					lua_pushboolean(L, 1);
					lua_rawset(L, tblVal);
				}
				else
				{
					// as array element
					lua_rawseti(L, tblVal, cc + 1);
				}
			}
			else
			{
				lua_pushlstring(L, (const char*)src + start, endpos - start);
			}

			cc ++;
			if (cc >= maxSplits)
				break;
		}

		start = i + 1;
	}

	if (maxSplits && start < srcLen)
	{
		endpos = srcLen;
		maxSplits = 0;
		goto _lastseg;
	}

	if (cc == 0)
	{
		// no one
		lua_pushvalue(L, 1);
		if (tblVal)
		{
			if (nFlags & kSplitAsKey)
			{
				// as key
				lua_pushboolean(L, 1);
				lua_rawset(L, tblVal);
			}
			else
			{
				// as array element
				lua_rawseti(L, tblVal, cc + 1);
			}
		}

		return 1;
	}

	if (callback)
	{
		lua_pushinteger(L, cc);
		return 1;
	}

	return retAs == LUA_TTABLE ? 1 : cc;
}

// ����ĳ�ַ��ŷָ����ַ�����ʾ��ID��������з֣�
static int lua_string_idarray(lua_State* L)
{
	size_t len, splitByLen = 1;
	const char* src = luaL_checklstring(L, 1, &len);
	const char* splitBySrc = luaL_optlstring(L, 2, ",", &splitByLen);
	lua_Integer asInteger = luaL_optinteger(L, 3, 0);

	if (splitByLen != 1)
	{
		luaL_error(L, "the 2-th parameter for string.idarray must be a char");
		return 0;
	}
	
	if (len < 1)
	{
		lua_newtable(L);
		return 1;
	}
	

	int cc = 0;
	char* endPtr = NULL;
	const char* readPtr = src, *readEnd = src + len;

	lua_createtable(L, 4, 0);
	for( ; ; )
	{
		while((uint8_t)readPtr[0] <= 32 && readPtr < readEnd)
			readPtr ++;	// skip invisible chars

		if (asInteger)
			lua_pushinteger(L, strtoll(readPtr, &endPtr, 10));
		else
			lua_pushnumber(L, strtod(readPtr, &endPtr));

		if (endPtr == readPtr)
			return 0;	// error formats

		lua_rawseti(L, -2, ++ cc);

		if (endPtr[0] == splitBySrc[0])
			readPtr = endPtr + 1;
		else
			break;
	}

	if (endPtr - src == len)
		return 1;

	// error formats
	return 0;
}

//////////////////////////////////////////////////////////////////////////
// ���ַ����������ҷǿɼ�����ȥ��������2��3���������Ϊtrue|false�ֱ��ʾ�Ƿ�Ҫ�������|�ұߡ����û�в���2��3��Ĭ�����Ҷ�����
static int lua_string_trim(lua_State* L)
{
	size_t len = 0;
	int triml = 1, trimr = 1;
	const uint8_t* src = (const uint8_t*)luaL_checklstring(L, 1, &len);
	
	if (lua_isboolean(L, 2))
		triml = lua_toboolean(L, 2);
	if (lua_isboolean(L, 3))
		trimr = lua_toboolean(L, 3);

	const uint8_t* left = src;
	const uint8_t* right = src + len;
	if (triml)
	{
		while(left < right)
		{
			if (left[0] > 32)
				break;
			left ++;
		}
	}
	if (trimr)
	{
		while (left < right)
		{
			if (*(right - 1) > 32)
				break;
			right --;
		}
	}

	if (right - left == len)
		lua_pushvalue(L, 1);
	else
		lua_pushlstring(L, (const char*)left, right - left);
	return 1;
}

//////////////////////////////////////////////////////////////////////////
// �ַ����������ַ�������ң���֧�ֶ��ַ����в��Ҳ�֧���ַ���
static int lua_string_rfindchar(lua_State* L)
{
	size_t len = 0, len2 = 0;
	const char* s = luaL_checklstring(L, 1, &len);
	const char* f = luaL_checklstring(L, 2, &len2);

	if (len && len2 == 1)
	{
		long t = luaL_optinteger(L, 3, 0);
		if (t > 1 && t <= len)
			f = strrchr(s + len - t, f[0]);
		else
			f = strrchr(s, f[0]);

		if (f)
		{
			lua_pushinteger(L, f - s + t + 1);
			return 1;
		}
	}

	return 0;
}

//////////////////////////////////////////////////////////////////////////
struct StringReplacePos
{
	size_t		offset;
	size_t		fromLen, toLen;
	const char	*from, *to;
};
struct StringReplacePosGreater
{
	int32_t operator () (const StringReplacePos& a, const StringReplacePos& b) { return (ptrdiff_t)a.offset - (ptrdiff_t)b.offset; }
};

struct StringReplacePosAlloc
{
	StringReplacePosAlloc()
	{
		cc = 0;
		upper = 80;
		all = fixedBuf;
	}
	~StringReplacePosAlloc()
	{
		if (all != fixedBuf)
			free(all);
	}

	StringReplacePos* alloc()
	{
		if (cc >= upper)
		{
			upper <<= 2;
			StringReplacePos* n = (StringReplacePos*)malloc(sizeof(StringReplacePos) * upper);
			if (cc)
				memcpy(n, all, sizeof(StringReplacePos) * cc);
			if (all != fixedBuf)
				free(all);
			all = n;
		}

		return &all[cc ++];
	}

	uint32_t			cc, upper;
	StringReplacePos	fixedBuf[80];
	StringReplacePos	*all;
};

// ���ֶ���ģʽ���ַ�/�ַ����滻
static int lua_string_replace(lua_State* L)
{
	int top = lua_gettop(L);
	if (top < 2)
		return 0;

	luaL_Buffer buf;	
	StringReplacePosAlloc posAlloc;
	size_t srcLen = 0, fromLen = 0, toLen = 0, offset;
	const char* src = luaL_checklstring(L, 1, &srcLen), *srcptr, *foundPos, *from, *to;

	luaL_buffinit(L, &buf);

	int tp1 = lua_type(L, 2), tp2 = lua_type(L, 3);	
	if (tp2 == LUA_TTABLE)
	{
		int i;
		if (tp1 == LUA_TTABLE)
		{
			// �ַ���������ַ�������
			int fromTbLen = lua_objlen(L, 2);
			if (fromTbLen < 1 || fromTbLen != lua_objlen(L, 3))
				return luaL_error(L, "string.replace with two table which length not equal", 0);
			
			for (i = 1; i <= fromTbLen; ++ i)
			{
				lua_rawgeti(L, 2, i);
				from = lua_tolstring(L, -1, &fromLen);
				if (!fromLen)
					return luaL_error(L, "cannot replace from empty string at table(%d) by string.replace", i);

				lua_rawgeti(L, 3, i);
				to = lua_tolstring(L, -1, &toLen);

				srcptr = src;
				while((foundPos = fromLen == 1 ? strchr(srcptr, from[0]) : strstr(srcptr, from)) != 0)
				{
					StringReplacePos* newrep = posAlloc.alloc();
					newrep->offset = foundPos - src;
					newrep->fromLen = fromLen;
					newrep->toLen = toLen;
					newrep->from = from;
					newrep->to = to;

					srcptr = foundPos + fromLen;
				}
			}
		}
		else if (tp1 == LUA_TUSERDATA)
		{
			// BM�ַ������ַ�������
			BMString* bms = (BMString*)lua_touserdata(L, 2);
			if (bms->m_flags != 'BMST')
				return luaL_error(L, "not a string returned by string.bmcompile", 0);

			int toTbLen = lua_objlen(L, 3);
			if (toTbLen != bms->m_hasNext + 1)
				return luaL_error(L, "string.replace with two table which length not equal", 0);

			i = 1;

			do 
			{
				lua_rawgeti(L, 3, i ++);
				to = lua_tolstring(L, -1, &toLen);

				offset = fromLen = 0;
				while((offset = bms->find((const uint8_t*)src + fromLen, srcLen - fromLen)) != -1)
				{
					StringReplacePos* newrep = posAlloc.alloc();
					newrep->offset = offset + fromLen;
					newrep->fromLen = bms->m_subLen;
					newrep->toLen = toLen;
					newrep->from = (const char*)(bms + 1);
					newrep->to = to;

					fromLen += offset + newrep->fromLen;
				}

			} while ((bms = bms->getNext()) != 0);
		}
		else
			return luaL_error(L, "error type for the 2-th parameter of string.replcae", 0);
	}
	else if (tp2 == LUA_TSTRING)
	{
		int repcc = 0;
		to = lua_tolstring(L, 3, &toLen);

		if (tp1 == LUA_TSTRING)
		{
			// �ַ������ַ����滻
			from = lua_tolstring(L, 2, &fromLen);
			if (fromLen > 0)
			{				
				srcptr = src;
				for(;;)
				{
					foundPos = fromLen == 1 ? strchr(srcptr, from[0]) : strstr(srcptr, from);
					if (!foundPos)
						break;

					offset = foundPos - srcptr;
					lua_string_addbuf(&buf, srcptr, offset);
					lua_string_addbuf(&buf, to, toLen);	

					srcptr = foundPos + fromLen;
					repcc ++;
				}
			
				if (repcc)
				{
					lua_string_addbuf(&buf, srcptr, srcLen - (srcptr - src));
					luaL_pushresult(&buf);
					return 1;
				}
			}
		}
		else if (tp1 == LUA_TUSERDATA)
		{
			// BM�ַ�������ͨ�ַ���
			BMString* bms = (BMString*)lua_touserdata(L, 2);
			if (bms->m_flags != 'BMST')
				return luaL_error(L, "not a string returned by string.bmcompile", 0);

			offset = fromLen = 0;
			while((offset = bms->find((const uint8_t*)src + fromLen, srcLen - fromLen)) != -1)
			{
				if (offset > fromLen)
					lua_string_addbuf(&buf, src + fromLen, offset - fromLen);
				lua_string_addbuf(&buf, to, toLen);

				fromLen += offset + bms->m_subLen;
				repcc ++;
			}

			if (repcc)
			{
				if (fromLen < srcLen)
					lua_string_addbuf(&buf, src + fromLen, srcLen - fromLen);

				luaL_pushresult(&buf);
				return 1;
			}
		}

		lua_pushvalue(L, 1);
		return 1;
	}
	else if (top == 2 && tp1 == LUA_TTABLE)
	{
		// keyy=>value���滻������ֻ��Ҫ����������
		lua_pushnil(L);
		while(lua_next(L, 2))
		{
			from = lua_tolstring(L, -2, &fromLen);
			if (!from || !fromLen)
				return luaL_error(L, "cannot replace from empty string by string.replace", 0);

			to = lua_tolstring(L, -1, &toLen);
			if (!to)
				to = "";

			srcptr = src;
			while((foundPos = fromLen == 1 ? strchr(srcptr, from[0]) : strstr(srcptr, from)) != 0)
			{
				StringReplacePos* newrep = posAlloc.alloc();
				newrep->offset = foundPos - src;
				newrep->fromLen = fromLen;
				newrep->toLen = toLen;
				newrep->from = from;
				newrep->to = to;

				srcptr = foundPos + fromLen;
			}

			lua_pop(L, 1);
		}
	}

	if (posAlloc.cc)
	{
		// ����֮��˳���滻
		offset = 0;
		uint32_t cc = posAlloc.cc;
		StringReplacePos* allPoses = posAlloc.all;

		tsort<StringReplacePos>(allPoses, cc, StringReplacePosGreater(), structswap<StringReplacePos>());
		for(uint32_t i = 0; i < cc; ++ i)
		{
			StringReplacePos& rep = allPoses[i];
			assert(i == 0 || rep.offset >= offset);

			lua_string_addbuf(&buf, src + offset, rep.offset - offset);
			lua_string_addbuf(&buf, rep.to, rep.toLen);

			offset = rep.offset + rep.fromLen;
		}

		if (offset < srcLen)
			lua_string_addbuf(&buf, src + offset, srcLen - offset);

		if (cc)
			luaL_pushresult(&buf);
		else
			lua_pushvalue(L, 1);
	}
	else
		lua_pushvalue(L, 1);

	return 1;
}

// ������1�ַ������ɲ���3ָ����λ�ÿ�ʼ������λ�ã����Բ�ָ�����ò���4ָ�������ַ������ò���2�������滻
static int lua_string_subreplaceto(lua_State* L)
{
	size_t srcLen = 0, repLen = 0;
	const char* src = (const char*)luaL_checklstring(L, 1, &srcLen);
	const char* rep = (const char*)luaL_checklstring(L, 2, &repLen);

	if (srcLen < 1)
	{
		lua_pushvalue(L, 1);
		return 1;
	}

	ptrdiff_t startp = luaL_checklong(L, 3) - 1, endp = srcLen - 1;
	if (lua_isnumber(L, 4))
		endp = lua_tointeger(L, 4);

	if (endp < 0)
		endp = srcLen + endp;
	else if (endp >= srcLen)
		endp = srcLen - 1;
	else
		endp --;

	if (endp < startp)
	{
		lua_pushlstring(L, "", 0);
		return 1;
	}

	luaL_Buffer buf;
	luaL_buffinit(L, &buf);

	if (startp > 1)
		lua_string_addbuf(&buf, src, startp - 1);
	lua_string_addbuf(&buf, rep, repLen);
	lua_string_addbuf(&buf, src + endp + 1, srcLen - endp - 1);

	luaL_pushresult(&buf);
	return 1;
}

// ������1�ַ������ɲ���3ָ����λ�ÿ�ʼ��ָ�����ȣ����Բ�ָ�����ò���4ָ�������ַ������ò���2�������滻
// ��������������subreplaceto��Ψһ�������һ���õ��ǿ�ʼ+λ�ý�����һ���õ��ǿ�ʼλ��+����
static int lua_string_subreplace(lua_State* L)
{
	size_t srcLen = 0, repLen = 0;
	const char* src = (const char*)luaL_checklstring(L, 1, &srcLen);
	const char* rep = (const char*)luaL_checklstring(L, 2, &repLen);

	if (srcLen < 1)
	{
		lua_pushvalue(L, 1);
		return 1;
	}

	ptrdiff_t startp = luaL_checklong(L, 3) - 1, leng = LONG_MAX;
	if (lua_isnumber(L, 4))
		leng = lua_tointeger(L, 4);

	leng = std::min(leng, (ptrdiff_t)(srcLen - startp));
	if (leng < 1)
	{
		lua_pushvalue(L, 1);
		return 0;
	}

	luaL_Buffer buf;
	luaL_buffinit(L, &buf);

	if (startp > 0)
		lua_string_addbuf(&buf, src, startp);
	lua_string_addbuf(&buf, rep, repLen);
	if (leng)
		lua_string_addbuf(&buf, src + startp + leng, srcLen - startp - leng);

	luaL_pushresult(&buf);
	return 1;
}

// ����2ָ����ʼλ�ã�Ȼ���ò���3ָ���Ľ���λ�û��ַ������в����Եõ�һ������λ�ã�����ʼ�����λ��֮����ַ���ȡ���󷵻�
// �÷�1��string.subto('abcdefghijklmn', 'hijkl') => abcdefg   hijkl�ǽ���λ���ַ���������sub������ǿ�ʼ��h֮ǰ
// �÷�2��string.subto('abcdefghijklmn', 4, 'hijkl') => defg   ͬ��һ����ֻ����ָ���˿�ʼλ��Ϊ��4���ַ������ǰ���3���ַ����ӵ���
static int lua_string_subto(lua_State* L)
{
	size_t srcLen, toLen = 0;
	int top = std::min(3, lua_gettop(L));
	const char* src = luaL_checklstring(L, 1, &srcLen), *to = 0;
	long start = 0, endp = srcLen;	

	if (top == 3)
		start = luaL_checklong(L, 2) - 1;
	if (start < 0 || srcLen < 1)
		return 0;

	if (lua_isnumber(L, top))
	{
		endp = luaL_checklong(L, top);
		if (endp < 0)
			endp = (long)srcLen + endp;
	}
	else if (lua_isstring(L, top))
	{
		to = luaL_checklstring(L, top, &toLen);
		if (!to || toLen < 1)
			return 0;

		const char* findStart = src + start;
		const char* pos = toLen == 1 ? strchr(findStart, to[0]) : strstr(findStart, to);
		if (!pos)
			return 0;
		
		if (pos == findStart)
		{
			lua_pushlstring(L, "", 0);
			return 1;
		}

		endp = pos - src - 1;
	}
	else if (lua_isnil(L, top))
	{
		endp = (long)srcLen - 1;
	}
	else
		return 0;

	if (endp < start)
		return 0;

	lua_pushlstring(L, src + start, endp - start + 1);
	return 1;
}

//////////////////////////////////////////////////////////////////////////
// �������2�ַ����е�ÿһ���ַ��ڲ���1�ַ������ܹ������˶��ٴΣ�ע��ͳ�Ƶ��ǲ���2��ÿһ���ַ����ڲ���1���ܹ����ֵĴ���
static int lua_string_countchars(lua_State* L)
{	
	size_t srcLen = 0, byLen = 0;
	uint8_t checker[256] = { 0 }, ch;

	const uint8_t* src = (const uint8_t*)luaL_checklstring(L, 1, &srcLen);
	const uint8_t* by = (const uint8_t*)luaL_checklstring(L, 2, &byLen);

	while ((ch = *by) != 0)
	{
		checker[ch] = 1;
		by ++;
	}

	int cc = 0;
	for(size_t i = 0; i < srcLen; ++ i)
	{
		if (checker[src[i]])
			cc ++;
	}

	lua_pushinteger(L, cc);
	return 1;
}

// �������2�ַ�����ÿһ���ַ��ֱ��ڲ���1�ַ����г����˶��ٴΣ�����Ϊһ��table
static int lua_string_counteachchars(lua_State* L)
{	
	size_t srcLen = 0, byLen = 0, i;
	uint32_t counts[256] = { 0 };
	uint8_t checker[256] = { 0 }, ch;	

	const uint8_t* src = (const uint8_t*)luaL_checklstring(L, 1, &srcLen);
	const uint8_t* by = (const uint8_t*)luaL_checklstring(L, 2, &byLen);

	for(i = 0; i < byLen; ++ i)
	{
		checker[by[i]] = 1;
		by ++;
	}

	for(i = 0; i < srcLen; ++ i)
	{
		uint8_t ch = src[i];
		if (checker[ch])
			counts[ch] ++;
	}

	int cc = 1;
	lua_createtable(L, byLen, 0);
	for(i = 0; i < byLen; ++ i)
	{
		lua_pushinteger(L, counts[by[i]]);
		lua_rawseti(L, -2, cc ++);
	}

	return 1;
}

//////////////////////////////////////////////////////////////////////////
// ����Ƿ�����ֵ��������ʽ��ʾ��������ֵ��������ϼ���������򷵻�ת�������ֵ�����򷵻�nil�����2������в���2�Ļ���
static int lua_string_checknumeric(lua_State* L)
{
	double d = 0;
	int r = 0, t = lua_gettop(L);

	if (t >= 1)
	{
		if (lua_isnumber(L, 1))
		{
			r = 1;
			d = lua_tonumber(L, 1);
		}
		else
		{
			size_t len = 0;
			char *endp = 0;
			const char* s = (const char*)lua_tolstring(L, 1, &len);

			if (len > 0)
			{
				d = strtod(s, &endp);
				if (endp && endp - s == len)
					r = 1;
			}
		}
	}

	if (r)
	{
		lua_pushnumber(L, d);
		return 1;
	}
	if (t >= 2)
	{
		lua_pushvalue(L, 2);
		return 1;
	}

	return 0;
}

static int lua_string_hasnumeric(lua_State* L)
{
	double d = 0;
	size_t len = 0;
	char *endp = 0;
	const char* s = 0;
	int r = 0, t = lua_gettop(L);

	if (t >= 1)
	{
		if (lua_isnumber(L, 1))
		{
			r = 1;
			d = lua_tonumber(L, 1);
		}
		else
		{			
			s = (const char*)lua_tolstring(L, 1, &len);
			if (len > 0)
			{
				d = strtod(s, &endp);
				if (endp > s)
					r = 1;
			}
		}
	}

	if (r)
	{
		lua_pushnumber(L, d);
		if (endp)
		{
			lua_pushinteger(L, endp - s + 1);
			return 2;
		}

		return 1;
	}
	if (t >= 2)
	{
		lua_pushvalue(L, 2);
		return 1;
	}

	return 0;
}

// ����Ƿ���������������ʽ��ʾ������ֵ��������ϼ���������򷵻�ת��������������򷵻�nil�����2������в���2�Ļ���
static int lua_string_checkinteger(lua_State* L)
{	
	size_t len = 0;
	long long v = 0;
	const char* s = 0;
	bool negative = false;
	int r = 0, t = lua_gettop(L), tp = 0;	

	if (t >= 1)
	{
		char *endp = 0;

		tp = lua_type(L, 1);
		if (tp == LUA_TNUMBER)
		{
			r = 1;
			v = lua_tointeger(L, 1);
		}
		else if (tp == LUA_TSTRING)
		{						
			s = (const char*)lua_tolstring(L, 1, &len);

			if (len > 0)
			{
				int digits = 10;
				switch(s[0])
				{
				case '0':
					if (s[1] == 'x')
					{
						digits = 16;
						len -= 2;
						s += 2;						
					}
					else
					{
						digits = 8;
						len --;
						s ++;						
					}
					break;

				case '-':
					negative = true;
					break;
				
				case '+':
					s ++;
					len --;
					break;
				}

				v = strtoll(s, &endp, digits);
				if (endp && endp - s == len)
					r = 1;
			}
		}
	}

	if (r)
	{
		lua_pushnumber(L, (double)v);
		return 1;
	}
	if (t >= 2)
	{
		lua_pushvalue(L, 2);
		return 1;
	}

	return 0;
}
// ����ַ����Ƿ������֣������������ֿ�ͷ�ַ������ַ���������֮������������������ν��������ǣ��������ֱ���number���ͣ��Լ��ַ����з����ֿ�ʼ��λ��
// ����������ֻ򲻺����֣��򷵻�Ϊnil������в���2������ʧ��ʱ���ز���2��
static int lua_string_hasinteger(lua_State* L)
{	
	size_t len = 0;
	long long v = 0;
	char *endp = 0;
	const char* s = 0;
	bool negative = false;
	int r = 0, t = lua_gettop(L), tp = 0;	

	if (t >= 1)
	{
		tp = lua_type(L, 1);
		if (tp == LUA_TNUMBER)
		{
			r = 1;
			v = lua_tointeger(L, 1);
		}
		else if (tp == LUA_TSTRING)
		{						
			s = (const char*)lua_tolstring(L, 1, &len);

			if (len > 0)
			{
				int digits = 10;
				switch(s[0])
				{
				case '0':				
					if (s[1] == 'x')
					{
						digits = 16;
						len -= 2;
						s += 2;						
					}
					else
					{
						digits = 8;
						len --;
						s ++;						
					}
					break;
				case '-':
					negative = true;
					break;
				case '+':
					s ++;
					len --;
					break;
				}

				v = strtoll(s, &endp, digits);
				if (endp > s)
					r = 2;
			}
		}
	}

	if (r)
	{
		lua_pushnumber(L, (double)v);
		if (endp)
		{
			lua_pushinteger(L, endp - s + 1);
			return 2;
		}
		return 1;
	}
	if (t >= 2)
	{
		lua_pushvalue(L, 2);
		return 1;
	}

	return 0;
}

static int lua_string_checkinteger32(lua_State* L)
{
	lua_Integer resulti = 0;
	int r = 0, t = lua_gettop(L), tp = 0;

	if (t >= 1)
	{
		size_t len = 0;
		const char* s;
		char *endp = 0;

		tp = lua_type(L, 1);
		if (tp == LUA_TNUMBER)
		{
			double d = lua_tonumber(L, 1);
			if (d <= INT_MAX && d >= INT_MIN)
			{
				r = 1;
				resulti = dtoi(d);
			}
		}
		else if (tp == LUA_TSTRING)
		{
			s = (const char*)lua_tolstring(L, 1, &len);

			if (len > 0)
			{
				int digits = 10;
				if (s[0] == '0')
				{
					if (s[1] == 'x')
					{
						digits = 16;
						len -= 2;
						s += 2;
					}
					else
					{
						digits = 8;
						len --;
						s ++;
					}
				}

				long long v = strtoll(s, &endp, digits);
				if (endp && endp - s == len && v <= INT_MAX && v >= INT_MIN)
				{
					r = 1;
					resulti = v;
				}
			}
		}
	}

	if (r)
	{
		lua_pushinteger(L, resulti);
		return 1;
	}
	if (t >= 2)
	{
		lua_pushvalue(L, 2);
		return 1;
	}

	return 0;
}

// ����Ƿ��ǲ���ֵ��������ʽ��ʾ�Ĳ���ֵ��������ϼ���������򷵻�ת����Ĳ���ֵ�����򷵻�nil�����2������в���2�Ļ���
static int lua_string_checkboolean(lua_State* L)
{
	int top = lua_gettop(L), r = 0, cc = 0;
	if (top >= 1)
	{
		switch (lua_type(L, 1))
		{
		case LUA_TNUMBER:
		if ((lua_isnumber(L, 1) && lua_tonumber(L, 1) != 0) || lua_tointeger(L, 1) != 0)
			r = 1;
		cc = 1;
		break;

		case LUA_TBOOLEAN:
			r = lua_toboolean(L, 1);
			cc = 1;
			break;

		case LUA_TSTRING:
		{
			size_t len = 0;
			const char* s = lua_tolstring(L, 1, &len);

			if (len == 4 && stricmp(s, "true") == 0)
				r = cc = 1;
			else if (len == 5 && stricmp(s, "false") == 0)
				cc = 1;
			else if (len == 1)
			{
				if (s[0] == '0') cc = 1;
				else if (s[0] == '1') r = cc = 1;
			}
		}
		break;
		}
	}

	if (cc)
	{
		lua_pushboolean(L, r);
		return 1;
	}
	if (top >= 2)
	{
		lua_pushvalue(L, 2);
		return 1;
	}
	return 0;
}

//////////////////////////////////////////////////////////////////////////
static int lua_string_htmlentitiesenc(lua_State* L)
{
	char ch;	
	size_t i, prevpos = 0;
	luaL_Buffer buf, *pBuf = &buf;
	size_t len = 0, skips = 0, addlen;
	const char* s = luaL_optlstring(L, 1, NULL, &len), *src;
	uint32_t flags = luaL_optinteger(L, 2, 0);

	if (!s || len == 0)
	{
		lua_pushlstring(L, "", 0);
		return 1;
	}

	uint8_t v, pos = 0;
	luaL_buffinit(L, &buf);

	for(i = 0; i < len; i += skips)
	{
		v = s[i];
		if (v & 0x80)
		{
			if ((v & 0xE0) == 0xC0)
			{
				skips = 2;
				pos = string_htmlent_ctls[((uint32_t)v << 8) | (uint8_t)s[i + 1]];
			}
			else if ((v & 0xF0) == 0xE0)
				skips = 3;
			else if ((v & 0xF8) == 0xF0)
				skips = 4;
			else if ((v & 0xFC) == 0xF8)
				skips = 5;
			else if ((v & 0xFE) == 0xFC)
				skips = 6;
			else
				return luaL_error(L, "Unknown code '0x%02x' in string.htmlentitiesenc", v);
		}
		else
		{
			skips = 1;
			pos = string_htmlent_ctls[v];
		}

		if (pos)
		{
			addlen = i - prevpos;
			if (addlen)
				lua_string_addbuf(pBuf, s + prevpos, addlen);
			prevpos = i + skips;

			if (pos <= 5)
			{
				src = string_htmlent_strs[pos - 1];
				while((ch = *src ++) != 0)
					luaL_addchar(pBuf, ch);

				pos = 0;
				continue;
			}
			else if (!(flags & kHtmlEntReservedOnly))
			{
				src = string_htmlent_strs[pos - 1];
				while((ch = *src ++) != 0)
					luaL_addchar(pBuf, ch);

				pos = 0;
				continue;
			}			
		}
	}

	lua_string_addbuf(pBuf, s + prevpos, i - prevpos);
	luaL_pushresult(pBuf);
	return 1;
}

static int lua_string_htmlentitiesdec(lua_State* L)
{
	StringPtrKey key;
	char tmpBuf[12], ch;
	bool foundEnd = false;
	luaL_Buffer buf, *pBuf = &buf;
	size_t i, len, bufLen, v, prevpos = 0;
	HtmlEntStringsMap::iterator ite, iEnd = gHtmlEntStrings.end();
	const char* s = luaL_checklstring(L, 1, &len);

	if (!s || len == 0)
	{
		lua_pushlstring(L, "", 0);
		return 1;
	}

	luaL_buffinit(L, pBuf);
	for(i = 0; i < len; ++ i)
	{
		ch = s[i];
		if (ch == '&')
		{
		_refind:
			tmpBuf[0] = '&';
			foundEnd = false;

			for(bufLen = 1; bufLen < 12 && i + bufLen < len; )
			{
				ch = s[i + bufLen];
				tmpBuf[bufLen ++] = ch;
				if (ch == ';')
				{
					foundEnd = true;
					break;
				}
				if (ch == '&')
				{
					i += bufLen;
					goto _refind;
				}
			}

			if (foundEnd)
			{
				tmpBuf[bufLen] = 0;
				key.pString = tmpBuf;
				key.nHashID = hashString(tmpBuf, bufLen);

				ite = gHtmlEntStrings.find(key);
				if (ite != iEnd)
				{
					size_t addlen = i - prevpos;
					if (addlen)
						lua_string_addbuf(pBuf, s + prevpos, addlen);
					prevpos = i + bufLen;
					i = prevpos - 1;

					v = ite->second;
					switch (v)
					{
					case 0: luaL_addchar(pBuf, '"'); break;
					case 1: luaL_addchar(pBuf, '\''); break;
					case 2: luaL_addchar(pBuf, '&'); break;
					case 3: luaL_addchar(pBuf, '<'); break;
					case 4: luaL_addchar(pBuf, '>'); break;
					case 5: luaL_addchar(pBuf, ' '); break;

					default:
						v = (v - 5) * 2;
						luaL_addchar(pBuf, string_htmlent_dblcodes[v]);
						luaL_addchar(pBuf, string_htmlent_dblcodes[v + 1]);
						break;
					}
				}
			}
		}
	}

	lua_string_addbuf(pBuf, s + prevpos, i - prevpos);
	luaL_pushresult(pBuf);
	return 1;
}

static int lua_string_addslashes(lua_State* L)
{
	char ch;	
	size_t i, prevpos = 0;
	luaL_Buffer buf, *pBuf = &buf;
	size_t len = 0, skips = 0, addlen;
	const char* s = luaL_optlstring(L, 1, NULL, &len), *src;
	int nsq = lua_toboolean(L, 2), ndq = lua_toboolean(L, 3);

	if (!s || len == 0)
	{
		lua_pushlstring(L, "", 0);
		return 1;
	}

	uint8_t v, pos = 0;
	luaL_buffinit(L, &buf);

	for(i = 0; i < len; ++ i)
	{
		ch = s[i];
		if ((ch == '\'' && !nsq) || (ch == '"' && !ndq) || ch == '\\')
		{
			addlen = i - prevpos;
			if (addlen)
				lua_string_addbuf(pBuf, s + prevpos, addlen);
			prevpos = i + 1;

			luaL_addchar(pBuf, '\\');
			luaL_addchar(pBuf, ch);
		}
	}

	lua_string_addbuf(pBuf, s + prevpos, i - prevpos);
	luaL_pushresult(pBuf);
	return 1;
}

static int lua_string_stripslashes(lua_State* L)
{
	char ch;	
	size_t i, prevpos = 0;
	luaL_Buffer buf, *pBuf = &buf;
	size_t len = 0, skips = 0, addlen;
	const char* s = luaL_optlstring(L, 1, NULL, &len), *src;
	int nsq = lua_toboolean(L, 2), ndq = lua_toboolean(L, 3);

	if (!s || len == 0)
	{
		lua_pushlstring(L, "", 0);
		return 1;
	}

	uint8_t v, pos = 0;
	luaL_buffinit(L, &buf);

	for(i = 0; i < len; ++ i)
	{
		ch = s[i];
		if (ch = '\\')
		{
			ch = s[i + 1];
			if ((ch == '\'' && !nsq) || (ch == '"' && !ndq) || ch == '\\')
			{
				addlen = i - prevpos;
				if (addlen)
					lua_string_addbuf(pBuf, s + prevpos, addlen);
				prevpos = i + 2;

				luaL_addchar(pBuf, ch);
			}
		}
	}

	lua_string_addbuf(pBuf, s + prevpos, i - prevpos);
	luaL_pushresult(pBuf);
	return 1;
}

//////////////////////////////////////////////////////////////////////////
static int lua_string_bmcompile(lua_State* L)
{
	size_t len;
	const char* src;
	BMString* bms;
	
	if (lua_istable(L, 1))
	{
		luaL_checkstack(L, 1, "string.bmcompile only build from one table");
		
		size_t totals = 0, cc = 0;
		int i, t = lua_objlen(L, 1);
		
		for(i = 1; i <= t; ++ i)
		{
			lua_rawgeti(L, 1, i);

			len = 0;
			src = lua_tolstring(L, -1, &len);
			if (!src || len == 0)
				return luaL_error(L, "string.bmcompile cannot compile a empty string", 0);

			len =  BMString::calcAllocSize(len);
			if (len)
			{
				totals += len;
				cc ++;
			}
		}

		if (cc)
		{			
			char* mem = (char*)lua_newuserdata(L, totals);
			for (i = 2, ++ t; i <= t; ++ i)
			{
				src = lua_tolstring(L, i, &len);
				if (src)
				{
					bms = (BMString*)mem;
					bms->setSub(src, len);
					bms->m_flags = 'BMST';
					bms->m_hasNext = -- cc;
					bms->makePreTable();

					mem += BMString::calcAllocSize(len);
				}
			}

			return 1;
		}
	}
	else
	{
		int n = 1, t = lua_gettop(L);
		for( ; n <= t; ++ n)
		{
			len = 0;
			src = luaL_checklstring(L, 1, &len);
			if (!src || len == 0)
				return luaL_error(L, "string.bmcompile cannot compile a empty string", 0);

			bms = (BMString*)lua_newuserdata(L, BMString::calcAllocSize(len));
			bms->setSub(src, len);
			bms->m_flags = 'BMST';
			bms->m_hasNext = 0;
			bms->makePreTable();
		}

		return t;
	}

	return 0;
}

static int lua_string_bmfind(lua_State* L)
{
	size_t srcLen;
	const uint8_t* src = (const uint8_t*)luaL_checklstring(L, 1, &srcLen);

	BMString* bms = (BMString*)lua_touserdata(L, 2);
	if (!bms || bms->m_flags != 'BMST')
		return luaL_error(L, "not a string returned by string.bmcompile", 0);

	size_t pos = bms->find(src, srcLen);
	if (pos != -1)
	{
		lua_pushinteger(L, pos + 1);
		return 1;
	}

	return 0;
}

//////////////////////////////////////////////////////////////////////////
typedef TMemNode JsonMemNode;

class JsonMemList : public TMemList
{
public:
	lua_State		*L;

	void escapeJsonString(const char* src, size_t len, uint32_t flags)
	{		
		uint8_t ch, v;
		uint32_t unicode;
		size_t i = 0, spos = 0;
		const char upperChars[] = { "0123456789ABCDEF" };

		while (i < len)
		{
			uint8_t ch = src[i];
			if (ch == 0)
			{
				// Ϊʲô�����NULL�ַ���????
				if (i > spos)
					addString(src + spos, i - spos);
				spos = ++ i;
				continue;
			}

			if (!(flags & kJsonSimpleEscape))
				v = json_escape_chars[ch];
			else if (ch == '\\' || ch == '"')
				v = 1;
			else
				v = 0;

			if (v == 0)
			{
				// defered
				++ i;
				continue;
			}

			if (v == 1)
			{
				// escape some chars
				if (i > spos)
					addString(src + spos, i - spos);
				addChar2('\\', ch);
				spos = ++ i;
			}
			else if (v == 2)
			{
				if (flags & kJsonUnicodes)
				{
					// defered
					if ((ch & 0xE0) == 0xC0)		//2 bit count
						i += 2;
					else if ((ch & 0xF0) == 0xE0)	//3 bit count
						i += 3;
					else if ((ch & 0xF8) == 0xF0)	//4 bit count
						i += 4;
					else
						i ++;

					continue;
				}

				if (i > spos)
					addString(src + spos, i - spos);

				// check utf8
				uint8_t* utf8src = (uint8_t*)src + i;
				if ((ch & 0xE0) == 0xC0)
				{
					//2 bit count
					unicode = ch & 0x1F;
					unicode = (unicode << 6) | (utf8src[1] & 0x3F);
					i += 2;
				}
				else if ((ch & 0xF0) == 0xE0)
				{
					//3 bit count
					unicode = ch & 0xF;
					unicode = (unicode << 6) | (utf8src[1] & 0x3F);
					unicode = (unicode << 6) | (utf8src[2] & 0x3F);
					i += 3;
				}
				else if ((ch & 0xF8) == 0xF0)
				{
					//4 bit count
					unicode = ch & 0x7;
					unicode = (unicode << 6) | (utf8src[1] & 0x3F);
					unicode = (unicode << 6) | (utf8src[2] & 0x3F);
					unicode = (unicode << 6) | (utf8src[3] & 0x3F);
					i += 4;
				}
				else
				{
					unicode = '?';
					i ++;
					assert(0);
				}

				char* utf8dst = reserve(6);
				utf8dst[0] = '\\';
				utf8dst[1] = 'u';

				utf8dst[2] = upperChars[(unicode >> 12) & 0xF];
				utf8dst[3] = upperChars[(unicode >> 8) & 0xF];
				utf8dst[4] = upperChars[(unicode >> 4) & 0xF];
				utf8dst[5] = upperChars[unicode & 0xF];

				spos = i;
			}
			else
			{
				// invisible(s) to visibled
				if (i > spos)
					addString(src + spos, i - spos);
				addChar2('\\', v);
				spos = ++ i;
			}
		}

		if (i > spos)
			addString(src + spos, i - spos);
	}

	void copyLuaString(const char* src, size_t len, uint32_t eqSymbols)
	{
		char* dst;
		uint32_t i;

		dst = reserve(eqSymbols + 2);
		*dst ++ = '[';
		for (i = 0; i < eqSymbols; ++ i)
			dst[i] = '=';
		dst[i] = '[';

		addString(src, len);

		dst = reserve(eqSymbols + 2);
		*dst ++ = ']';
		for (i = 0; i < eqSymbols; ++ i)
			dst[i] = '=';
		dst[i] = ']';
	}
};

#define JSON_MAX_DEEP_LEVELS	1024
#define jsonConvValue()\
	switch(vtype) {\
	case LUA_TTABLE:\
		if (recursionJsonEncode(L, mem, base + 1, flags, funcsIdx) == -1)\
			return -1;\
		break;\
	case LUA_TNUMBER:\
		v = lua_tonumber(L, -1);\
		ival = static_cast<int64_t>(v);\
		if (v == ival) {\
			if (v < 0) {\
				if (ival < INT_MIN)\
					len = opt_i64toa(ival, buf);\
				else\
					len = opt_i32toa(ival, buf);\
			} else if (ival <= UINT_MAX)\
				len = opt_u32toa(ival, buf);\
			else\
				len = opt_u64toa(ival, buf);\
		} else {\
			len = opt_dtoa(v, buf);\
		}\
		mem->addString(buf, len);\
		break;\
	case LUA_TSTRING:\
		ptr = lua_tolstring(L, -1, &len);\
		if (flags & kJsonLuaString) {\
			mem->copyLuaString(ptr, len, flags & 0xFFFFFF);\
		} else {\
			mem->addChar('"');\
			mem->escapeJsonString(ptr, len, flags);\
			mem->addChar('"');\
		}\
		break;\
	case LUA_TBOOLEAN:\
		if (lua_toboolean(L, -1))\
			mem->addString("true", 4);\
		else\
			mem->addString("false", 5);\
		break;\
	case LUA_TLIGHTUSERDATA:\
	case LUA_TUSERDATA:\
		if (!(flags & kJsonDropNull) && lua_touserdata(L, -1) == NULL)\
			mem->addString("null", 4);\
		break;\
	}

static int recursionJsonEncode(lua_State* L, JsonMemList* mem, int tblIdx, uint32_t flags, int* funcsIdx)
{
	double v;
	size_t len;	
	char buf[64];
	int64_t ival;
	const char* ptr;

	if (funcsIdx[3] ++ >= JSON_MAX_DEEP_LEVELS)
		return -1;

	size_t arr = lua_objlen(L, tblIdx), cc = 0;
	if (arr == 0)
	{
		int base = lua_gettop(L) + 1;

		lua_pushnil(L);
		while(lua_next(L, tblIdx))
		{
			int vtype = lua_type(L, -1);
			if (vtype < LUA_TFUNCTION)
			{
				ptr = lua_tolstring(L, -2, &len);

				mem->addChar2(cc ? ',' : '{', '"');
				mem->addString(ptr, len);
				mem->addChar2('"', ':');

				jsonConvValue();
				cc ++;
			}

			lua_settop(L, base);
		}

		if (cc)
			mem->addChar('}');
		else if (flags & kJsonEmptyToObject)
			mem->addChar2('{', ']');
		else
			mem->addChar2('[', ']');
	}
	else
	{
		int base = lua_gettop(L);

		mem->addChar('[');

		for (size_t n = 1; n <= arr; ++ n)
		{
			lua_rawgeti(L, tblIdx, n);

			int vtype = lua_type(L, -1);
			if (vtype < LUA_TFUNCTION)
			{
				if (n > 1)
					mem->addChar(',');

				jsonConvValue();
				cc ++;
			}

			lua_settop(L, base);
		}

		mem->addChar(']');
	}

	funcsIdx[3] --;

	return cc;
}

static int pushJsonString(lua_State* L, const char* v, size_t len, uint32_t retCData, int32_t* funcs)
{
	if (retCData)
	{
		lua_pushvalue(L, funcs[0]);
		lua_pushliteral(L, "uint8_t[?]");
		lua_pushinteger(L, len);
		lua_pcall(L, 2, 1, 0);

		void* dst = const_cast<void*>(lua_topointer(L, -1));
		if (dst)
		{
			memcpy(dst, v, len);
			return 1;
		}

		return 0;
	}

	lua_pushlstring(L, v, len);
	return 1;
}
static int pushJsonString(lua_State* L, JsonMemList& mems, size_t total, uint32_t retCData, int32_t* funcs)
{
	JsonMemNode* n;
	if (retCData)
	{
		lua_pushvalue(L, funcs[0]);
		lua_pushliteral(L, "uint8_t[?]");
		lua_pushinteger(L, total);
		lua_pcall(L, 2, 1, 0);

		char* dst = (char*)const_cast<void*>(lua_topointer(L, -1));
		if (dst)
		{
			while ((n = mems.popFirst()) != NULL)
			{
				memcpy(dst, (char*)(n + 1), n->used);
				dst += n->used;
				free(n);
			}

			return 1;
		}

		return 0;
	}

	char* dst = (char*)malloc(total), *ptr = dst;
	while ((n = mems.popFirst()) != NULL)
	{
		size_t used = n->used;
		memcpy(ptr, (char*)(n + 1), n->used);
		if (ptr != dst)
			free(n);	// ��һ������Ҫ�ͷţ���Ϊ������malloc�����
		ptr += used;
	}

	lua_pushlstring(L, dst, total);
	return 1;
}

// ����1��JSON�ַ���������ֵ��������һ���ǽ������Table������һ�����õ����ַ����ĳ��ȡ������2������ֵΪnil���ʾ����JSON��ʱ�������
// ����2�����ڵ�{}��[]Ϊ�յ�ʱ�򣬱�ʶ������һ��Object����һ��Array
// ����õ���cdata���ͣ�����boxed 64bit integer������ô�����ڵ���֮ǰrequire('ffi')
static int lua_string_json(lua_State* L)
{
	int top = lua_gettop(L);
	int tp = lua_type(L, 1);

	if (tp == LUA_TSTRING)
	{
		// json string to lua table
		size_t len = 0;
		bool copy = true;
		const char* str;
		lua_Integer flags = 0;
		int needSetMarker = 0;
		
		if (tp == LUA_TSTRING)
		{
			str = luaL_checklstring(L, 1, &len);
		}
		else
		{
			// ʹ��sizeof�������ַ�������
			lua_rawgeti(L, LUA_REGISTRYINDEX, kLuaRegVal_FFISizeof);
			lua_pushvalue(L, 1);
			lua_pcall(L, 1, 1, 0);

			len = lua_tointeger(L, -1);
			str = (const char*)lua_topointer(L, 1);
		}

		if (!str || len < 2)
			return 0;

		if (top >= 2 && !lua_isnil(L, 2))
			needSetMarker = 2;
		if (top >= 3)
		{
			flags = luaL_optinteger(L, 3, 0);
			if (flags & kJsonNoCopy)
				copy = false;
		}

		lua_newtable(L);
		top = lua_gettop(L);

		JSONFile f(L, flags & kJsonDropNull);
		size_t readlen = f.parse(str, len, copy, needSetMarker);
		if (readlen == 0)
		{
			// error
			char err[512], summary[64] = { 0 };
		
			f.summary(summary, 63);
			size_t errl = snprintf(err, 512, "JSON parse error: %s, position is approximately at: %s", f.getError(), summary);
		
			lua_settop(L, top - 1);
			lua_pushlstring(L, err, errl);
			return 1;
		}

		lua_settop(L, top);
		lua_pushinteger(L, readlen);
		return 2;
	}
	else if (tp == LUA_TTABLE)
	{
		// lua table to json string
		JsonMemList memList;
		char fixedBuf[4096];

		int32_t funcs[4] = { 0 };
		uint32_t flags = 0;

		memList.L = L;
		memList.wrapNode(fixedBuf, sizeof(fixedBuf));

		if (top >= 2)
			flags = luaL_optinteger(L, 2, 0);

		lua_rawgeti(L, LUA_REGISTRYINDEX, kLuaRegVal_FFINew);
		lua_rawgeti(L, LUA_REGISTRYINDEX, kLuaRegVal_tostring);

		funcs[1] = lua_gettop(L);
		funcs[0] = funcs[1] - 1;

		if (recursionJsonEncode(L, &memList, 1, flags, funcs) != -1)
		{
			// ������û���κ����⣬���Ƿ��ؽ�����������2�ı�־λ�к���kJsonRetCData����ô������һ��uint8_t�͵�cdata�ڴ�飬���򽫷���һ��lua string
			int r = 0;
			JsonMemNode* n = memList.first();
			if (memList.size() == 1)
			{
				r = pushJsonString(L, (char*)(n + 1), n->used, flags & kJsonRetCData, funcs);
				memList.clear();
			}
			else
			{
				size_t total = 0;
				while (n)
				{
					total += n->used;
					n = n->next();
				}

				r = pushJsonString(L, memList, total, flags & kJsonRetCData, funcs);
			}

			return r;
		}
		else if (memList.size() >= 1)
		{
			memList.popFirst();
		}
	}

	return 0;
}

//////////////////////////////////////////////////////////////////////////
static int lua_string_base64encode(lua_State* L)
{
	size_t len = 0;
	std::string strout;
	const char* src = luaL_checklstring(L, 1, &len);

	len = Base64Encode(src, len, strout);
	lua_pushlstring(L, strout.c_str(), len);

	return 1;
}

static int lua_string_base64decode(lua_State* L)
{
	size_t len = 0;
	std::string strout;
	const char* src = luaL_checklstring(L, 1, &len);

	len = Base64Decode(src, len, strout);
	lua_pushlstring(L, strout.data(), len);

	return 1;
}

static int lua_string_urlencode(lua_State* L)
{
	size_t len = 0;
	std::string strout;
	const char* src = luaL_checklstring(L, 1, &len);

	urlEncode(src, len, strout);

	lua_pushlstring(L, strout.c_str(), strout.length());
	return 1;
}

static int lua_string_urldecode(lua_State* L)
{
	char* src;
	size_t len = 0;
	char fixedBuf[512];
	std::string strSource;
	const char* str = luaL_checklstring(L, 1, &len);

	if (len <= 512)
	{
		memcpy(fixedBuf, str, len);
		src = fixedBuf;
	}
	else
	{
		strSource.append(str, len);
		src = const_cast<char*>(strSource.data());
	}

	len = urlDecode(src, len);

	lua_pushlstring(L, src, len);
	return 1;
}

static int lua_string_hexdump(lua_State* L)
{
	uint8_t* dst;
	size_t len = 0;
	char fixedBuf[512];
	std::string strout;
	const uint8_t* src = (const uint8_t*)luaL_checklstring(L, 1, &len);
	static const uint8_t converts[] = { "0123456789ABCDEF" };

	if (len <= 256)
	{
		dst = (uint8_t*)fixedBuf;
	}
	else
	{
		strout.resize(len << 1);
		dst = (uint8_t*)const_cast<char*>(strout.data());
	}

	for(size_t i = 0; i < len; ++ i)
	{
		dst[0] = converts[(src[i] >> 4) & 0xF];
		dst[1] = converts[src[i] & 0xF];
		dst += 2;
	}

	if (len <= 256)
		lua_pushlstring(L, fixedBuf, len << 1);
	else
		lua_pushlstring(L, strout.data(), len << 1);
	return 1;
}

//////////////////////////////////////////////////////////////////////////
static inline bool _isTagEnd(const uint8_t* code, const uint8_t* codeEnd, const uint8_t** outPtr)
{
	if (code[0] == '>')
	{
		*outPtr = code + 1;
		return true;
	}
	if (code[0] == '/')
	{
		code ++;
		while (code < codeEnd)
		{
			if (code[0] == '>')
			{
				*outPtr = code + 1;
				return true;
			}
			else if (code[0] > 32)
				break;
			code ++;
		}
	}
	return false;
}
static inline bool _isCommentEnd(const uint8_t* code, const uint8_t* codeEnd, const uint8_t** outPtr)
{
	if (code[0] == '-' && code[1] == '-' && code[2] == '>')
	{
		*outPtr = code + 4;
		return true;
	}
	return false;
}

static bool _findEnd(const uint8_t* code, const uint8_t* codeEnd, const uint8_t** outPtr)
{
	uint8_t quoteIn = 0;
	while (code < codeEnd)
	{
		if (quoteIn)
		{
			if (code[0] == quoteIn)
				quoteIn = 0;
			else if (code[0] == '\\')
				code ++;
		}
		else if (code[0] == '\'' || code[0] == '"')
		{
			quoteIn = code[0];
		}
		else if (_isTagEnd(code, codeEnd, outPtr))
		{

			return true;
		}

		code ++;
	}

	return false;
}
static bool _findCommentEnd(const uint8_t* code, const uint8_t* codeEnd, const uint8_t** outPtr)
{
	uint8_t quoteIn = 0;
	while (code < codeEnd)
	{
		if (quoteIn)
		{
			if (code[0] == quoteIn)
				quoteIn = 0;
			else if (code[0] == '\\')
				code ++;
		}
		else if (code[0] == '\'' || code[0] == '"')
		{
			quoteIn = code[0];
		}
		else if (_isCommentEnd(code, codeEnd, outPtr))
		{
			return true;
		}

		code ++;
	}

	return false;
}

static int lua_string_removemarkups(lua_State* L)
{
	size_t len = 0;
	std::string strResult;
	const uint8_t* code = (const uint8_t*)luaL_checklstring(L, 1, &len);
	const uint8_t* codeEnd = code + len, *testPtr;
	uint8_t flag = 0;

	strResult.reserve(len / 2);
	while (code < codeEnd)
	{
		if (code[0] == '<')
		{
			testPtr = code + 1;
			if (testPtr[0] == '!' && testPtr[1] == '-' && testPtr[2] == '-')
			{
				// ע��
				if (_findCommentEnd(testPtr + 3, codeEnd, &code))
					continue;
			}
			else
			{
				while (testPtr < codeEnd)
				{
					flag = json_value_char_tbl[testPtr[0]];
					if (flag) break;
					testPtr ++;
				}

				if (testPtr[0] == '/' || (flag >= 4 && flag <= 7))
				{
					// ��ǩ��ʼ������������
					testPtr ++;
					while (testPtr < codeEnd)
					{
						flag = json_value_char_tbl[testPtr[0]];
						if (flag < 4 || flag > 7)
							break;
						testPtr ++;
					}

					if (flag == 0)
					{
						// �ҵ�����λ��						
						if (_findEnd(testPtr, codeEnd, &code))
							continue;
					}
					else if (_isTagEnd(testPtr, codeEnd, &code))
					{
						// �����ǩ������
						continue;
					}
				}
			}
		}

		strResult += code[0];
		code ++;
	}

	lua_pushlstring(L, strResult.c_str(), strResult.length());
	return 1;
}

//////////////////////////////////////////////////////////////////////////
#ifdef USE_RE2
typedef MAP_CLASS_NAME<std::string, re2::RE2*> RE2PatsMap;
static RE2PatsMap re2pats;

static int lua_string_re2clear(lua_State* L)
{
	for (RE2PatsMap::iterator ite = re2pats.begin(), iend = re2pats.end(); ite != iend; ++ ite)
	{
		re2::RE2* p = ite->second;
		delete p;
	}

	return 0;
}

static int lua_string_re2match(lua_State* L)
{		
	std::string str;
	re2::RE2* cache = NULL;
	bool bFullMatch = true, bMatched = false;
	size_t srclen = 0, patlen = 0, optslen = 0;
	const char* src = luaL_checklstring(L, 1, &srclen);
	const char* pat = luaL_checklstring(L, 2, &patlen);
	const char* opts = lua_tolstring(L, 3, &optslen);

	while (optslen -- > 0)
	{
		switch (opts[optslen])
		{
		case 'c':	// ����RE2
		{
			str.append(pat, patlen);
			RE2PatsMap::iterator ite = re2pats.find(str);
			if (ite == re2pats.end())
			{
				cache = new re2::RE2(re2::StringPiece(pat, patlen));
				re2pats.insert(RE2PatsMap::value_type(str, cache));
			}
			else
				cache = ite->second;
		}
			break;

		case 'p':	// ����ƥ��
			bFullMatch = false;
			break;
		}
	}	

	return 0;
}
#endif


//////////////////////////////////////////////////////////////////////////
static void luaext_string(lua_State *L)
{
	const luaL_Reg procs[] = {
		// һ��ȡ���λ�õ��ַ�ת�ַ���
		{ "at", &lua_string_at },

		// �ַ����з�
		{ "split", &lua_string_split },
		// ID�����з�
		{ "idarray", &lua_string_idarray },
		// trim����
		{ "trim", &lua_string_trim },

		// �����ַ�������ң���֧���ַ�������
		{ "rfindchar", &lua_string_rfindchar },
		// ���ַ������������ַ����ִ������ܼ���
		{ "countchars", &lua_string_countchars },
		// ���ַ�������ÿһ���ַ����ִ����ķֱ����
		{ "counteachchars", &lua_string_counteachchars },

		// �ַ�����ģʽ�����滻��֧�ֵ��Ե�����������飬bm�ַ��������ͨ�ַ��������
		{ "replace", &lua_string_replace },
		// �ַ���ָ��λ��+����λ���滻
		{ "subreplaceto", &lua_string_subreplaceto },
		// �ַ���ָ��λ��+�����滻
		{ "subreplace", &lua_string_subreplace },
		// �ַ������Ҵ���ȡ
		{ "subto", &lua_string_subto },

		// ��ֵ+�������ַ������
		{ "checknumeric", &lua_string_checknumeric },
		{ "hasnumeric", &lua_string_hasnumeric },
		// �����ַ�����⣬֧��boxed int64
		{ "checkinteger", &lua_string_checkinteger },
		{ "checkinteger32", &lua_string_checkinteger32 },
		{ "hasinteger", &lua_string_hasinteger },
		// �����ͼ�⣨������ֵ���ַ�����boolean���ͣ�
		{ "checkboolean", &lua_string_checkboolean },

		// HTMLʵ��ת��
		{ "htmlentitiesenc", &lua_string_htmlentitiesenc },
		{ "htmlentitiesdec", &lua_string_htmlentitiesdec },

		// Ϊ��˫������ӷ�б��/ȥ����б��
		{ "addslashes", &lua_string_addslashes },
		{ "stripslashes", &lua_string_stripslashes },

		// ����Boyer-Moore���ַ������ڲ���
		{ "bmcompile", &lua_string_bmcompile },
		// �ñ���õ�BM�ַ������в��ң��ʺ���һ�α��룬Ȼ���ڴ������ı��п��ٵĲ���һ���Ӵ����Ӵ���Դ�ַ���Խ������Խ�ţ�
		{ "bmfind", &lua_string_bmfind },

		// Json���루decʱ2~4��������ngx�����õ�cjson��encʱ��table�ĸ��Ӷ�1.5~4.5��������cjson�������ⲻ�ǹؼ����ؼ��Ǳ�json encode֧��boxed 64bit integer�Լ�cdata�Զ�����Ϊbase64�ȱ�����е���չ������
		{ "json", &lua_string_json },

		// Base64�������
		{ "base64encode", &lua_string_base64encode },
		{ "base64decode", &lua_string_base64decode },

		// URL�������
		{ "urlencode", &lua_string_urlencode },
		{ "urldecode", &lua_string_urldecode },

		// Hexת��
		{ "hexdump", &lua_string_hexdump },

		// �Ƴ�MarkupLanguageʽ�ı�ǩ����
		{ "removemarkups", &lua_string_removemarkups },

		{ NULL, NULL }
	};


	lua_getglobal(L, "string");

	// �ַ����з�ʱ���õı�־λ
	lua_pushliteral(L, "SPLIT_ASKEY");				// ���г�����ֵ��Ϊkey
	lua_pushinteger(L, kSplitAsKey);
	lua_rawset(L, -3);

	lua_pushliteral(L, "SPLIT_TRIM");				// ÿһ���г�����ֵ��������trim
	lua_pushinteger(L, kSplitTrim);
	lua_rawset(L, -3);

	// JSON����ʱ���õı�־λ
	lua_pushliteral(L, "JSON_NOCOPY");				// ����ʱֱ����ԭ�ַ����ϲ�����ԭ�ַ����ڴ潫�⵽�ƻ��������֮�󽫲�������ʹ�õĻ����ƻ�Ҳû��ϵ�ˣ��ֿ�����һ��copy��
	lua_pushinteger(L, kJsonNoCopy);
	lua_rawset(L, -3);

	lua_pushliteral(L, "JSON_RETCDATA");			// ����ʱ����uint8_t�͵�cdata���ݶ�����string
	lua_pushinteger(L, kJsonRetCData);
	lua_rawset(L, -3);

	lua_pushliteral(L, "JSON_LUASTRING");			// �����ַ���ʱֱ��ʹ�ñ���֧�ֵ���չ [[ ]] ����ʾ����ʹ�ñ�׼��json�ַ�����ʾ�������Ҳ��û��escape��ת��utf8/unicode�Ĺ���
	lua_pushinteger(L, kJsonLuaString);
	lua_rawset(L, -3);

	lua_pushliteral(L, "JSON_UNICODES");			// �ַ����е�utf8/unicode��Ҫת�壬ֱ�ӱ���ʹ�á���һ����־JSON_LUASTRING����ʱ������־������
	lua_pushinteger(L, kJsonUnicodes);
	lua_rawset(L, -3);

	lua_pushliteral(L, "JSON_SIMPLE_ESCAPE");		// ����\��"����ת�壬�����Ķ�����
	lua_pushinteger(L, kJsonSimpleEscape);
	lua_rawset(L, -3);

	lua_pushliteral(L, "JSON_EMPTY_TOOBJECT");		// ��tableΪ��ʱ�����������Ĭ����{}������[]
	lua_pushinteger(L, kJsonEmptyToObject);
	lua_rawset(L, -3);

	lua_pushliteral(L, "JSON_DROPNULL");			// ����־������ڣ�����ʱ���������ĳ��ֵΪngx.null������Ե����������nullֵ��������ʱ�������nullֵ�������lua ngx.null
	lua_pushinteger(L, kJsonDropNull);
	lua_rawset(L, -3);

	lua_pushliteral(L, "HTML_RESERVED_ONLY");		// htmlentitiesenc����������HTML��Ԥ��ʵ����ţ�������ISO-XXXXϵ�еķ���
	lua_pushinteger(L, kHtmlEntReservedOnly);
	lua_rawset(L, -3);

	// ������չ�ĺ���
	luaL_register(L, NULL, procs);

	lua_pop(L, 1);
}
