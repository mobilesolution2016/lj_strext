static void doTableClone(lua_State* L, int src, int dst, int deepClone)
{
	if (dst == -1)
	{
		lua_createtable(L, lua_objlen(L, src), 4);
		dst = lua_gettop(L);
	}

	lua_pushnil(L);
	while (lua_next(L, src))
	{
		lua_pushvalue(L, -2);
		if (deepClone && lua_istable(L, -2))
			doTableClone(L, lua_gettop(L) - 1, -1, 1);
		else
			lua_pushvalue(L, -2);
		lua_rawset(L, dst);
		lua_pop(L, 1);
	}
}
static int lua_table_clone(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);	

	if (lua_istable(L, 2))
	{
		doTableClone(L, 2, 1, lua_isboolean(L, 3) ? lua_toboolean(L, 3) : 0);
		lua_pushvalue(L, 2);		
	}
	else
	{
		doTableClone(L, 1, -1, lua_isboolean(L, 2) ? lua_toboolean(L, 2) : 0);
	}

	return 1;
}

static void doTableExtend(lua_State* L, int src, int dst)
{
	lua_pushnil(L);
	while (lua_next(L, src))
	{
		if (lua_istable(L, -1))
		{
			int newsrc = lua_gettop(L);

			lua_pushvalue(L, -2);
			lua_gettable(L, 1);
			if (!lua_istable(L, -1))
			{
				lua_pushvalue(L, -3);
				lua_createtable(L, lua_objlen(L, newsrc), 4);

				doTableExtend(L, newsrc, newsrc + 3);
				lua_settable(L, dst);				
			}
			else
			{
				doTableExtend(L, newsrc, newsrc + 1);
			}
			lua_pop(L, 2);
		}
		else
		{
			lua_pushvalue(L, -2);
			lua_pushvalue(L, -2);
			lua_settable(L, dst);
			lua_pop(L, 1);
		}		
	}
}
static int lua_table_extend(lua_State* L)
{
	int n = lua_gettop(L);
	luaL_checktype(L, 1, LUA_TTABLE);

	for (int i = 2; i <= n; ++ i)
	{
		if (!lua_istable(L, i))
			continue;

		doTableExtend(L, i, 1);
	}

	lua_pushvalue(L, 1);
	return 1;
}

static int lua_table_join(lua_State* L)
{
	const char* equation, *joins;
	size_t keylen, vallen, joinlen = 0, eqlen = 1;

	luaL_checktype(L, 1, LUA_TTABLE);
	joins = luaL_optlstring(L, 2, ",", &joinlen);
	equation = luaL_optlstring(L, 3, "=", &eqlen);

	lua_pushnil(L);
	bool tostring = false;
	int tostringIdx = lua_gettop(L), cc = 0;

	TMemList mems;
	char fixedBuf[4096];
	mems.wrapNode(fixedBuf, sizeof(fixedBuf));

	lua_pushnil(L);
	while (lua_next(L, 1))
	{
		int pop = 1;
		const char* name = lua_tolstring(L, -2, &keylen);
		const char* val = lua_tolstring(L, -1, &vallen);
		if (!val)
		{
			if (tostring)
			{
				lua_pushvalue(L, tostringIdx);
			}
			else
			{
				tostring = true;
				lua_rawgeti(L, LUA_REGISTRYINDEX, kLuaRegVal_tostring);
				lua_pushvalue(L, -1);
				lua_replace(L, tostringIdx);
			}

			lua_pushvalue(L, -2);
			if (lua_pcall(L, 1, 1, 0) == 0)
				val = lua_tolstring(L, -1, &vallen);

			pop = 2;
		}

		if (val)
		{
			if (joinlen && cc)
			{
				switch (joinlen)
				{
				case 1: mems.addChar(joins[0]); break;
				case 2: mems.addChar2(joins[0], joins[1]); break;
				default: mems.addString(joins, joinlen); break;
				}
			}

			mems.addString(name, keylen);

			switch (eqlen)
			{
			case 1: mems.addChar(equation[0]); break;
			case 2: mems.addChar2(equation[0], equation[1]); break;
			default: mems.addString(equation, eqlen); break;
			}

			mems.addString(val, vallen);

			cc ++;
		}
		lua_pop(L, pop);
	}

	TMemNode* n = mems.first();
	if (mems.size() == 1)
	{
		lua_pushlstring(L, *n, n->used);
		mems.popFirst();
	}
	else
	{
		size_t total = 0;
		char* dst = mems.joinAll(&total);
		lua_pushlstring(L, dst, total);
		free(dst);
	}

	return 1;
}

//////////////////////////////////////////////////////////////////////////
static int lua_table_filter(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	int tp = lua_type(L, 2), top = -1;

	if (tp == LUA_TSTRING && (!lua_isboolean(L, 3) || !lua_toboolean(L, 3)))
	{
		lua_newtable(L);
		top = lua_gettop(L);
	}

	if (tp == LUA_TSTRING)
	{
		// 将字符串按照 , 分解，以每一个名称做为key去查找
		size_t len = 0;
		const char* keys = lua_tolstring(L, 2, &len);
		if (!keys || len < 1)
			return 1;

		int cc = 0;
		const char* endp, *ptr = keys, *ptrend = keys + len;
		for(;;)
		{
			while (ptr < ptrend)
			{
				if ((uint8_t)ptr[0] > 32)
					break;
				ptr ++;
			}

			endp = (const char*)memchr(ptr, ',', len - (ptr - keys));
			if (!endp)
				break;

			lua_pushlstring(L, ptr, endp - ptr);
			if (top != -1)
			{				
				lua_pushvalue(L, -1);
				lua_gettable(L, 1);
				lua_rawset(L, top);
			}
			else
			{
				lua_gettable(L, 1);
			}
			ptr = endp + 1;
			cc ++;
		}

		if (ptr < ptrend)
		{
			// 最后一组
			lua_pushlstring(L, ptr, ptrend - ptr);
			if (top != -1)
			{
				lua_pushvalue(L, -1);
				lua_gettable(L, 1);
				lua_rawset(L, top);
			}
			else
			{
				lua_gettable(L, 1);
			}
			cc ++;
		}

		if (top == -1)
			return cc;
	}
	else if (tp == LUA_TTABLE)
	{
		// table中的每一个key或值用于去查找
		lua_pushnil(L);
		while (lua_next(L, 2))
		{
			if (lua_isstring(L, -1) && lua_isnumber(L, -2))
			{
				// 值是字符串，用值去查找
				lua_pushvalue(L, -1);
				lua_pushvalue(L, -1);
				lua_gettable(L, 1);
				lua_rawset(L, top);
			}
			else if (lua_isstring(L, -2))
			{
				// 键是字符串，用键去查找
				lua_pushvalue(L, -2);
				lua_pushvalue(L, -1);
				lua_rawget(L, 1);
				lua_rawset(L, top);
			}

			lua_pop(L, 1);
		}
	}
	else if (tp == LUA_TFUNCTION)
	{
		// 对源table中的每一个值都调用函数，当函数返回true时保留这个值
		lua_pushnil(L);
		int pop = lua_gettop(L);

		while (lua_next(L, 1))
		{
			lua_pushvalue(L, 2);
			lua_pushvalue(L, -3);
			lua_pushvalue(L, -3);
			if (lua_pcall(L, 2, 1, 0) == 0 && lua_isboolean(L, -1))
			{
				lua_pushvalue(L, -3);
				lua_pushvalue(L, -3);
				lua_rawset(L, top);
			}

			lua_settop(L, pop);
		}
	}

	return 1;
}

//////////////////////////////////////////////////////////////////////////
static int lua_table_val2key(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);

	lua_createtable(L, 0, lua_objlen(L, 1));
	int idx = lua_gettop(L);

	lua_pushnil(L);
	while (lua_next(L, 1))
	{
		lua_pushvalue(L, -1);
		lua_pushvalue(L, -3);
		lua_rawset(L, idx);
		lua_pop(L, 1);
	}

	return 1;
}

//////////////////////////////////////////////////////////////////////////
static int lua_table_new(lua_State* L)
{
	int t = lua_gettop(L), narr = 4, nrec = 0;

	if (t == 2)
	{
		narr = luaL_optinteger(L, 1, 4);
		nrec = luaL_optinteger(L, 2, 0);
	}
	else if (t == 1)
	{
		narr = luaL_optinteger(L, 1, 4);
	}

	lua_createtable(L, narr, nrec);
	return 1;
}

static int lua_table_clear(lua_State* L)
{
	lua_pushnil(L);
	while (lua_next(L, 1))
	{
		lua_pushvalue(L, -2);
		lua_pushnil(L);
		lua_rawset(L, 1);
		lua_pop(L, 1);
	}

	return 1;
}

//////////////////////////////////////////////////////////////////////////
static int lua_table_in(lua_State* L)
{
	int r = 0;
	if (lua_gettop(L) >= 2)
	{
		luaL_checktype(L, 1, LUA_TTABLE);

		lua_pushnil(L);
		while (lua_next(L, 1))
		{
			if (lua_equal(L, -1, 2))
			{
				r = 1;
				break;
			}
			lua_pop(L, 1);
		}
	}

	lua_pushboolean(L, r);
	return 1;
}

static int lua_table_rawin(lua_State* L)
{
	int r = 0;
	if (lua_gettop(L) >= 2)
	{
		luaL_checktype(L, 1, LUA_TTABLE);

		lua_pushnil(L);
		while (lua_next(L, 1))
		{
			if (lua_rawequal(L, -1, 2))
			{
				r = 1;
				break;
			}
			lua_pop(L, 1);
		}
	}

	lua_pushboolean(L, r);
	return 1;
}

//////////////////////////////////////////////////////////////////////////
static void luaext_table(lua_State *L)
{
	const luaL_Reg procs[] = {
		// 字符串快速替换
		{ "clone", &lua_table_clone },
		// 字符串指定位置+结束位置替换
		{ "extend", &lua_table_extend },
		// 将key和value组合成一个大字符串（不支持递归）
		{ "join", &lua_table_join },
		// 过滤
		{ "filter", &lua_table_filter },
		// value和key互转
		{ "val2key", &lua_table_val2key },
		// 判断值是否在数组中出现
		{ "in", &lua_table_in },
		// 判断值是否在数组中出现
		{ "rawin", &lua_table_rawin },

		{ NULL, NULL }
	};

	lua_getglobal(L, "table");

	luaL_register(L, NULL, procs);

	// check pack/unpack function
	lua_pushliteral(L, "pack");
	lua_rawget(L, -2);
	int t = lua_isnil(L, -1);
	lua_pop(L, 1);

	if (t)
	{
		lua_pushliteral(L, "pack");
		lua_getglobal(L, "pack");
		lua_rawset(L, -3);

		lua_pushliteral(L, "unpack");
		lua_getglobal(L, "unpack");
		lua_rawset(L, -3);
	}

	// check new function
	lua_pushliteral(L, "new");
	lua_rawget(L, -2);
	t = lua_isnil(L, -1);
	lua_pop(L, 1);

	if (t)
	{
		lua_pushliteral(L, "new");
		lua_pushcfunction(L, &lua_table_new);
		lua_rawset(L, -3);
	}

	// check clear function
	lua_pushliteral(L, "clear");
	lua_rawget(L, -2);
	t = lua_isnil(L, -1);
	lua_pop(L, 1);

	if (t)
	{
		lua_pushliteral(L, "clear");
		lua_pushcfunction(L, &lua_table_clear);
		lua_rawset(L, -3);
	}

	lua_pop(L, 1);
}