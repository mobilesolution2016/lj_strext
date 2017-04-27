// utf8从前向后跳过完整的字符数
template <typename STR, typename COUNT> STR* lua_utf8str_skip_ht(STR* str, size_t len, COUNT skips)
{
	STR* ptr = str;
	if (skips == 0)
		return ptr;

	while(ptr - str < len)
	{
		STR hiChar = ptr[0];
		if (!(hiChar & 0x80))
			ptr ++;
		else if ((hiChar & 0xE0) == 0xC0)
			ptr += 2;
		else if ((hiChar & 0xF0) == 0xE0)
			ptr += 3;
		else if ((hiChar & 0xF8) == 0xF0)
			ptr += 4;
		else if ((hiChar & 0xFC) == 0xF8)
			ptr += 5;
		else if ((hiChar & 0xFE) == 0xFC)
			ptr += 6;
		else
			return 0;

		if (-- skips == 0)
			return ptr;
	}

	return 0;
}
// utf8从后向前跳过完整的字符数
template <typename STR, typename COUNT> STR* lua_utf8str_skip_th(STR* str, size_t len, COUNT skips)
{
	uint32_t cc = 0;
	STR* ptr = str + len;

	if (skips == 0)
		return ptr;

	ptr --;
	while(ptr >= str)
	{
		STR hiChar = ptr[0];
		if (!(hiChar & 0x80))
		{
			if (-- skips == 0)
				return ptr;

			ptr --;
			cc = 0;
			continue;
		}

		switch (cc)
		{
		case 0:			
			cc ++;
			break;

		case 1:
			if ((hiChar & 0xE0) == 0xC0)
			{
				if (-- skips == 0)
					return ptr;				
				cc = 0;
			}
			else
				cc ++;
			break;

		case 2:
			if ((hiChar & 0xF0) == 0xE0)
			{
				if (-- skips == 0)
					return ptr;				
				cc = 0;
			}
			else
				cc ++;
			break;

		case 3:
			if ((hiChar & 0xF8) == 0xF0)
			{
				if (-- skips == 0)
					return ptr;				
				cc = 0;
			}
			else
				cc ++;
			break;

		case 4:
			if ((hiChar & 0xFC) == 0xF8)
			{
				if (-- skips == 0)
					return ptr;				
				cc = 0;
			}
			else
				cc ++;
			break;

		case 5:
			if ((hiChar & 0xFE) == 0xFC)
			{
				if (-- skips == 0)
					return ptr;	
				cc = 0;
			}
			else
				return 0;
			break;

		default:
			return 0;
		}

		ptr --;
	}

	return 0;
}


//////////////////////////////////////////////////////////////////////////
static int lua_utf8str_det3(lua_State* L)
{
	int r = 1;
	size_t len = 0;
	const uint8_t* str = (const uint8_t*)luaL_checklstring(L, 1, &len);
	const uint8_t* ptr = str;

	while(ptr - str < len)
	{
		uint8_t hiChar = ptr[0];
		if (!(hiChar & 0x80))
			ptr ++;
		else if ((hiChar & 0xE0) == 0xC0 && (ptr[1] & 0xC0) == 0x80)
			ptr += 2;
		else if ((hiChar & 0xF0) == 0xE0 && (ptr[1] & 0xC0) == 0x80 && (ptr[2] & 0xC0) == 0x80)
			ptr += 3;
		else
		{
			r = 0;
			break;
		}
	}

	if (str + len != ptr)
		r = 0;

	lua_pushboolean(L, r);
	return 1;
}

static int lua_utf8str_len(lua_State* L)
{
	int cc = 0;
	size_t len = 0;
	const uint8_t* str = (const uint8_t*)luaL_checklstring(L, 1, &len);
	const uint8_t* ptr = str;

	if (lua_isnumber(L, 2))
	{
		long n = luaL_checklong(L, 2);
		if (n < len)
			len = n;
	}

	while(ptr - str < len)
	{
		uint8_t hiChar = ptr[0];
		if (!(hiChar & 0x80))
			ptr ++;
		else if ((hiChar & 0xE0) == 0xC0)
			ptr += 2;
		else if ((hiChar & 0xF0) == 0xE0)
			ptr += 3;
		else if ((hiChar & 0xF8) == 0xF0)
			ptr += 4;
		else if ((hiChar & 0xFC) == 0xF8)
			ptr += 5;
		else if ((hiChar & 0xFE) == 0xFC)
			ptr += 6;
		else
			return 0;

		cc ++;
	}

	if (str + len == ptr)
	{
		lua_pushinteger(L, cc);
		return 1;
	}

	return 0;
}

static int lua_utf8str_sub(lua_State* L)
{
	size_t i, len = 0;
	const uint8_t* str = (const uint8_t*)luaL_checklstring(L, 1, &len);
	long start = luaL_checklong(L, 2) - 1;
	if (start < 0)
	{
		lua_pushlstring(L, "", 0);
		return 1;
	}

	const uint8_t* ptr = lua_utf8str_skip_ht(str, len, start), *endptr = 0;
	if (!ptr)
	{
		lua_pushlstring(L, "", 0);
		return 1;
	}

	if (lua_isnumber(L, 3))
	{
		long end = luaL_checklong(L, 3);
		if (end > 0)
		{
			if (end < start)
			{
				lua_pushlstring(L, "", 0);
				return 1;
			}

			endptr = lua_utf8str_skip_ht(ptr, len - (ptr - str), end - start);
		}
		else if (end < 0)
		{
			endptr = lua_utf8str_skip_th(str, len, -1 - end);
		}

		if (!endptr || endptr <= ptr)
		{
			lua_pushlstring(L, "", 0);
			return 1;
		}
	}
	else
		endptr = ptr + len;

	lua_pushlstring(L, (const char*)ptr, endptr - ptr);
	return 1;
}

static int lua_utf8str_fromgbk(lua_State* L)
{
	if (lua_isstring(L, 1))
	{
		size_t len = 0;
		const char* gbk = lua_tolstring(L, 1, &len);

		if (len <= 400)
		{
			char utf8[600];
			len = GBK2UTF8(gbk, len, utf8);
			lua_pushlstring(L, utf8, len);
		}
		else
		{
			std::string utf8;
			utf8.resize((len >> 1) * 3);

			char* utf8buf = const_cast<char*>(utf8.data());
			len = GBK2UTF8(gbk, len, utf8buf);
			lua_pushlstring(L, utf8buf, len);
		}

		return 1;
	}
	return 0;
}

static int lua_utf8str_togbk(lua_State* L)
{
	if (lua_isstring(L, 1))
	{
		size_t len = 0;
		const char* utf8 = lua_tolstring(L, 1, &len);

		if (len <= 640)
		{
			char gbk[640];
			len = UTF82GBK(utf8, len, gbk);
			lua_pushlstring(L, gbk, len);
		}
		else
		{
			std::string gbk;
			gbk.resize(len);

			char* gbkbuf = const_cast<char*>(gbk.data());
			len = UTF82GBK(utf8, len, gbkbuf);
			lua_pushlstring(L, gbkbuf, len);
		}

		return 1;
	}
	return 0;
}

static int lua_utf8str_print(lua_State* L)
{
	int i, n = lua_gettop(L);
	if (n < 1)
		return 0;

	char gbkbuf[1024];
	std::string strPrint, strgbk;
	strPrint.reserve(256);

	lua_getglobal(L, "tostring");
	for (i = 1; i <= n; i ++)
	{
		size_t len;
		const char *s;
		lua_pushvalue(L, -1);
		lua_pushvalue(L, i);
		lua_call(L, 1, 1);
		s = lua_tolstring(L, -1, &len);
		if (s == NULL)
			return luaL_error(L, LUA_QL("tostring") " must return a string to " LUA_QL("print"));
		
		if (i > 1)
			strPrint += '\t';

		if (len <= 1024)
		{
			len = UTF82GBK(s, len, gbkbuf);
			s = gbkbuf;
		}
		else
		{
			strgbk.resize(len);
			len = UTF82GBK(s, len, const_cast<char*>(strgbk.data()));
			s = strgbk.data();
		}
		
		strPrint.append(s, len);
		lua_pop(L, 1);
	}

	puts(strPrint.c_str());

	return 0;
}

// 可用的模式有：
// %s：字符串
// %u：无符号数字
// %d：有符号数字
// %x：十六进制数字
// %f %g：浮点数
// %c：单个ansi码转字符
// 支持整数、小数、字符串的列宽控制，小数点位数控制
static int lua_utf8str_fmtprint(lua_State* L)
{
	luaL_Buffer buf;
	size_t srcLen = 0;
	char *endpos = 0, tmpbuf[64];
	const char* src = luaL_checklstring(L, 1, &srcLen), *val;

	if (srcLen < 2)
	{
		lua_pushvalue(L, 1);
		return 1;
	}

	lua_getglobal(L, "tostring");
	int tostrIndex = lua_gettop(L);

	luaL_Buffer* pBuf = &buf;
	luaL_buffinit(L, pBuf);

	double dv;
	int64_t lli;
	uint64_t lln;
	uint32_t flags, flag;
	bool hasLen = false, hasDigits = false;
	size_t start = 0, valLen, len, digits, carry;
	for (int cc = 2, tp; ; )
	{
		const char* foundpos = strchr(src + start, '%');
		if (!foundpos)
			break;

		size_t pos = foundpos - src;
		if (start < pos)
			lua_string_addbuf(&buf, src + start, pos - start, true);

		start = pos + 1;
		if (start >= srcLen)
			break;

		// 连续两个%为转义
		foundpos ++;
		char ctl = foundpos[0];
		if (ctl == '%')
		{
			start ++;
			luaL_addchar(pBuf, '%');
			continue;
		}

		// 标志位
		flags = 0;
		flag = string_fmt_valid_fmt[ctl];
		while ((flag >> 4) > 0)
		{
			flags |= flag;
			foundpos ++;
			ctl = foundpos[0];
			flag = string_fmt_valid_fmt[ctl];
		}

		// 长/宽度
		carry = 1;
		len = digits = 0;
		while (ctl >= '0' && ctl <= '9')
		{
			hasLen = true;
			len *= carry;
			len += ctl - '0';
			foundpos ++;
			carry *= 10;
			ctl = foundpos[0];
		}

		// 小数位数
		if (ctl == '.')
		{
			carry = 1;
			foundpos ++;
			hasDigits = true;
			ctl = foundpos[0];
			while (ctl >= '0' && ctl <= '9')
			{
				digits *= carry;
				digits += ctl - '0';
				foundpos ++;
				carry *= 10;
				ctl = foundpos[0];
			}
		}

		tp = lua_type(L, cc);
		if (tp <= LUA_TNIL)
		{
			luaL_error(L, "string.fmt #%d have no parameter", cc - 1);
			return 0;
		}

		switch (flag = string_fmt_valid_fmt[ctl])
		{
		case 1: // 字符串
			if (tp != LUA_TSTRING)
			{
				lua_pushvalue(L, tostrIndex);
				lua_pushvalue(L, cc);
				lua_pcall(L, 1, 1, 0);
				val = lua_tolstring(L, -1, &valLen);
			}
			else
				val = lua_tolstring(L, cc, &valLen);

			if (!val)
				return luaL_error(L, "string.fmt #%d expet string but got not string", cc - 1);

			if (len > 0)
			{
				lua_string_addbuf(pBuf, val, std::min(len, valLen), true);
				while (len -- > valLen)
					luaL_addchar(pBuf, ' ');
			}
			else
				lua_string_addbuf(pBuf, val, valLen, true);

			if (tp != LUA_TSTRING)
				lua_pop(L, 1);
			break;

		case 2: // 浮点数
			val = 0;
			if (tp == LUA_TNUMBER)
			{
				dv = lua_tonumber(L, cc);
			}
			else if (tp == LUA_TSTRING)
			{
				val = lua_tolstring(L, cc, &valLen);
				dv = strtod(val, &endpos);
				if (isnan(dv) || endpos - val != valLen)
					return luaL_error(L, "string.fmt #%d expet number but got not number", cc - 1);
			}
			else if (tp == LUA_TBOOLEAN)
			{
				tmpbuf[0] = lua_toboolean(L, cc);
				valLen = 1;
			}
			else
				return luaL_error(L, "string.fmt #%d expet number but got not number", cc - 1);

			if (hasDigits)
			{
				// 处理小数位数，不够digits就填充0，多了就裁掉
				valLen = opt_dtoa(dv, tmpbuf);
				val = (const char*)memchr(tmpbuf, '.', valLen);
				if (val)
				{
					len = valLen - (val - tmpbuf) - 1;
					if (digits < len)
						valLen -= len - digits;
					if (digits == 0 && !(flag & 0x40))
						valLen --;	// remove point
				}
				else
				{
					len = 0;
					if (digits)
						tmpbuf[valLen ++] = '.';
				}

				while (len ++ < digits)
					tmpbuf[valLen ++] = '0';

				val = tmpbuf;
			}
			else if (!val)
			{
				val = lua_tolstring(L, cc, &valLen);
			}

			lua_string_addbuf(pBuf, val, valLen);
			break;

		case 3:	// ansi码到字符
			if (tp == LUA_TSTRING)
			{
				val = lua_tolstring(L, cc, &valLen);
			}
			else if (tp == LUA_TNUMBER)
			{
				tmpbuf[0] = lua_tointeger(L, cc);
				val = tmpbuf;
			}
			else
				return luaL_error(L, "string.fmt #%d expet ansi code but got ansi code", cc - 1);

			luaL_addchar(pBuf, val[0]);
			break;

		case 0:	// 错误
			return luaL_error(L, "the %u-ith char '%c' invalid", foundpos - src + 1, ctl);

		default: // 整数
			if (tp == LUA_TNUMBER)
			{
				lli = lua_tointeger(L, cc);
			}
			else if (tp == LUA_TSTRING)
			{
				val = lua_tolstring(L, cc, &valLen);
				if (valLen >= 3 && val[0] == '0' && val[1] == 'x')
				{
					val += 2;
					valLen -= 2;
				}

				lli = strtoll(val, &endpos, 10);
				if (endpos - val != valLen)
					return luaL_error(L, "string.fmt #%d expet integer but got not integer", cc - 1);
			}
			else if (tp == LUA_TBOOLEAN)
			{
				lli = lua_toboolean(L, cc);
			}
			else
				return luaL_error(L, "string.fmt #%d expet integer but got not integer", cc - 1);

			lln = lli;
			switch (flag)
			{
			case 4:
				if (lln > UINT_MAX)
					valLen = opt_u64toa(lln, tmpbuf);
				else
					valLen = opt_u32toa(lln, tmpbuf);
				break;

			case 5:
				if (lli >= INT_MIN || lli <= INT_MAX)
					valLen = opt_i32toa(lli, tmpbuf);
				else
					valLen = opt_i64toa(lli, tmpbuf);
				break;

			case 6:
				if (lln > UINT_MAX)
					valLen = opt_u64toa_hex(lln, tmpbuf, false);
				else
					valLen = opt_u32toa_hex(lln, tmpbuf, false);
				break;

			case 7:
				if (lln > UINT_MAX)
					valLen = opt_u64toa_hex(lln, tmpbuf, true);
				else
					valLen = opt_u32toa_hex(lln, tmpbuf, true);
				break;
			}

			// 位数不足先补0
			while (len -- > valLen)
				luaL_addchar(pBuf, '0');

			lua_string_addbuf(pBuf, tmpbuf, valLen);
			break;
		}

		start = foundpos - src + 1;
		++ cc;
	}

	if (start < srcLen)
		lua_string_addbuf(pBuf, src + start, srcLen - start, true);

	luaL_pushresult(pBuf);	

	puts(lua_tostring(L, -1));

	return 0;
}

//////////////////////////////////////////////////////////////////////////
static void luaext_utf8str(lua_State *L)
{
	const luaL_Reg procs[] = {
		// 检测是否是3字节UTF8编码的类型
		{ "det3", &lua_utf8str_det3 },
		// 检测UTF8字符串的长度
		{ "len", &lua_utf8str_len },
		// 获取子串
		{ "sub", &lua_utf8str_sub },
		// 与GBK之间的转换
		{ "fromgbk", &lua_utf8str_fromgbk },
		{ "togbk", &lua_utf8str_togbk },
#ifdef _WINDOWS
		{ "print", &lua_utf8str_print },
#endif
		{ "fmtprint", &lua_utf8str_fmtprint },

		{ NULL, NULL }
	};

	luaL_register(L, "utf8str", procs);

#ifndef _WINDOWS
	lua_pushliteral(L, "print");
	lua_getglobal(L, "print");	
	lua_rawset(L, -3);
#endif

	lua_pop(L, 1);
}