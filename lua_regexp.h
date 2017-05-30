// Boost RegExp¿â
static boost::regex_constants::match_flag_type RegExp_translateFlags(const char* flags)
{
	boost::regex_constants::match_flag_type r = boost::regex_constants::match_default;
	while (!flags[0])
	{
		switch (flags[0])
		{
		case 'g': r |= boost::regex_constants::match_all; break;
		}

		flags ++;
	}

	return r;
}

static int lua_RegExp_gc(lua_State* L)
{
	boost::regex* expr = (boost::regex*)lua_touserdata(L, 1);
	if (expr)
		expr->~basic_regex();

	return 0;
}

static int lua_RegExp_find(lua_State* L)
{
	size_t alen;
	int top = lua_gettop(L);
	const char* a = luaL_checklstring(L, 2, &alen);
	if (!a || alen < 1)
		return 0;

	int tp3 = lua_type(L, 3);
	boost::regex_constants::match_flag_type flag = boost::regex_constants::match_default;
	if (tp3 == LUA_TSTRING)
	{
		tp3 = 4;
		flag = RegExp_translateFlags(lua_tostring(L, 3));
	}
	else
	{
		tp3 = 3;
	}

	boost::cmatch match;
	int tp = lua_type(L, 1);
	if (tp == LUA_TSTRING)
	{
		size_t blen;
		const char* b = luaL_checklstring(L, 1, &blen);
		if (!b || blen < 1)
			return 0;

		boost::regex expr(b);
		if (!boost::regex_search(a, match, expr, flag))
			return 0;
	}
	else if (tp == LUA_TUSERDATA)
	{
		boost::regex* expr = (boost::regex*)lua_touserdata(L, 1);
		if (!expr || !regex_search(a, match, *expr, flag))
			return 0;
	}

	size_t i, t = match.size();
	if (t == 0)
		return 0;

	tp = top == tp3 ? lua_type(L, top) : LUA_TNONE;
	if (tp == LUA_TBOOLEAN && lua_toboolean(L, top))
	{
		lua_newtable(L);

		for (i = 1; i < t; ++ i)
		{
			std::string& r = match[i].str();
			lua_pushlstring(L, r.c_str(), r.length());
			lua_rawseti(L, -2, i);
		}

		return 1;
	}
	if (tp == LUA_TFUNCTION)
	{
		for (i = 1; i < t; ++ i)
		{
			std::string& r = match[i].str();

			lua_pushvalue(L, top);
			lua_pushinteger(L, i);
			lua_pushlstring(L, r.c_str(), r.length());
			lua_pushvalue(L, 1);
			if (lua_pcall(L, 3, 1, 0) || lua_toboolean(L, -1) == 0)
				break;

			lua_settop(L, top);
		}

		lua_pushinteger(L, t - 1);
		return 1;
	}

	for (i = 1; i < t; ++ i)
	{
		std::string& r = match[1].str();
		lua_pushlstring(L, r.c_str(), r.length());
	}

	return t - 1;
}

static int lua_RegExp_new(lua_State* L)
{
	const char* s = luaL_checkstring(L, 1);
	if (!s || !s[0])
		return 0;

	boost::regex* expr = (boost::regex*)lua_newuserdata(L, sizeof(boost::regex));
	new (expr) boost::regex(s);

	luaL_newmetatable(L, "__RegExp");
	lua_setmetatable(L, -2);

	return 1;
}

static void luaext_RegExp(lua_State *L)
{
	const luaL_Reg procs[] = {
		{ "new", &lua_RegExp_new },
		{ "find", &lua_RegExp_find },
		{ NULL }
	} ;

	luaL_register(L, "RegExp", procs);
	lua_pop(L, 1);

	luaL_newmetatable(L, "__RegExp");

		lua_pushliteral(L, "__index");
		lua_newtable(L);

			lua_pushliteral(L, "find");
			lua_pushcfunction(L, &lua_RegExp_find);
			lua_rawset(L, -3);

		lua_rawset(L, -3);

		lua_pushliteral(L, "__gc");
		lua_pushcfunction(L, &lua_RegExp_gc);
		lua_rawset(L, -3);

	lua_pop(L, 1);	
}