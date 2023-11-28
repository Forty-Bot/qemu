DEF_HELPER_FLAGS_1(get_sr, TCG_CALL_NO_WG, i32, env)
DEF_HELPER_2(set_sr, void, env, i32)
DEF_HELPER_2(unsupported, noreturn, env, i32)
