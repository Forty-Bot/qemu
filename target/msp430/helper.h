DEF_HELPER_0(cio_exit, noreturn)
DEF_HELPER_1(cio_io, void, env)
DEF_HELPER_FLAGS_1(get_sr, TCG_CALL_NO_WG_SE, i32, env)
DEF_HELPER_2(set_sr, void, env, i32)
DEF_HELPER_2(unsupported, noreturn, env, i32)
DEF_HELPER_FLAGS_3(dadd, TCG_CALL_NO_RWG_SE, i32, i32, i32, i32)
DEF_HELPER_FLAGS_3(daddb, TCG_CALL_NO_RWG_SE, i32, i32, i32, i32)
