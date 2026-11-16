#ifdef NDEBUG
#undef NDEBUG
#endif

#include "Common/IniNameRegistry.h"

#include <cassert>
#include <cstdio>
#include <cstring>

int main()
{
	const char* const builtins[] = { "NONE", "FIRST", "SECOND" };
	IniNameRegistry forward(builtins, 3);
	IniNameRegistry reverse(builtins, 3);
	assert(forward.resolve("none") == 0);
	assert(forward.resolve("FiRsT") == 1);
	assert(forward.resolve("SECOND") == 2);
	assert(forward.resolve(NULL) == -1);
	assert(forward.resolve("") == -1);
	assert(forward.getName(-1) == NULL);
	assert(forward.getName(3) == NULL);

	int alpha = forward.resolve("MOD_TELEPORT");
	int beta = forward.resolve("MOD_CURSOR");
	assert(alpha >= 0x40000000);
	assert(beta >= 0x40000000);
	assert(alpha != beta);
	assert(reverse.resolve("mod_cursor") == beta);
	assert(reverse.resolve("mod_teleport") == alpha);
	assert(forward.resolve("Mod_Teleport") == alpha);
	assert(std::strcmp(forward.getName(alpha), "mod_teleport") == 0);
	assert(forward.getCustomNames().size() == 2);

	const char* const expanded[] = { "NONE", "FIRST", "SECOND", "NEW_BUILTIN" };
	IniNameRegistry upgraded(expanded, 4);
	assert(upgraded.resolve("MOD_TELEPORT") == alpha);
	assert(upgraded.resolve("MOD_CURSOR") == beta);

	int collision = forward.resolve("collision_klqrmzgtvj");
	assert(collision == 1229971372);
	assert(forward.resolve("collision_zgntkuncnh") == -1);
	assert(std::strcmp(forward.getName(collision), "collision_klqrmzgtvj") == 0);
	assert(forward.resolve("collision_klqrmzgtvj") == collision);

	for (int i = 0; i < 1000; ++i)
	{
		char name[32];
		std::sprintf(name, "MOD_POWER_%d", i);
		assert(forward.resolve(name) >= 0x40000000);
	}
	assert(forward.getCustomNames().size() == 1003);
	std::puts("IniNameRegistry tests passed");
	return 0;
}
