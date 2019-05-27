cmake_minimum_required(VERSION 3.5 FATAL_ERROR)
project(SimpleMemAlloc VERSION 0.1.0 LANGUAGES CXX C )
add_executable(mem_alloc_test main.cpp MemoryAllocator.cpp DebugLib.c)

target_compile_options(mem_alloc_test PRIVATE $<$<CXX_COMPILER_ID:MSVC>:${OpenMP_CXX_FLAGS} /W4 /WX /Qvec-report:2 $<$<CONFIG:DEBUG>:/Od> $<$<CONFIG:RELEASE>:/O2> $<$<CONFIG:MINSIZEREL>:/Os> $<$<CONFIG:RELWITHDEBINFO>:/O1>> )
target_compile_options(mem_alloc_test PRIVATE $<$<C_COMPILER_ID:MSVC>:${OpenMP_C_FLAGS} /W4 /WX /Qvec-report:2 $<$<CONFIG:DEBUG>:/Od> $<$<CONFIG:RELEASE>:/O2> $<$<CONFIG:MINSIZEREL>:/Os> $<$<CONFIG:RELWITHDEBINFO>:/O1>> )
target_compile_options(mem_alloc_test PRIVATE $<$<CXX_COMPILER_ID:GNU>:${OpenMP_CXX_FLAGS} -Wall -Werror -no-pie -fPIC -ftree-vectorize -fopt-info-vec-optimized -fopt-info-vec-missed $<$<CONFIG:DEBUG>:-ggdb> $<$<CONFIG:RELEASE>:-O3> $<$<CONFIG:MINSIZEREL>:-Os> $<$<CONFIG:RELWITHDEBINFO>:-O2 -ggdb> > )
target_compile_options(mem_alloc_test PRIVATE $<$<C_COMPILER_ID:GNU>:${OpenMP_C_FLAGS} -Wall -Werror -no-pie -fPIC -ftree-vectorize -fopt-info-vec-optimized -fopt-info-vec-missed $<$<CONFIG:DEBUG>:-ggdb> $<$<CONFIG:RELEASE>:-O3> $<$<CONFIG:MINSIZEREL>:-Os> $<$<CONFIG:RELWITHDEBINFO>:-O2 -ggdb> > )
target_compile_options(mem_alloc_test PRIVATE $<$<CXX_COMPILER_ID:CLANG>:${OpenMP_CXX_FLAGS} -Wall -Werror -Rpass=loop-vectorize -Rpass-missed=loop-vectorize -Rpass-analysis=loop-vectorize $<$<CONFIG:DEBUG>:-ggdb> $<$<CONFIG:RELEASE>:-O3> $<$<CONFIG:MINSIZEREL>:-Os> $<$<CONFIG:RELWITHDEBINFO>:-O2 -ggdb> > )
target_compile_options(mem_alloc_test PRIVATE $<$<C_COMPILER_ID:CLANG>:${OpenMP_C_FLAGS} -Wall -Werror -Rpass=loop-vectorize -Rpass-missed=loop-vectorize -Rpass-analysis=loop-vectorize $<$<CONFIG:DEBUG>:-ggdb> $<$<CONFIG:RELEASE>:-O3> $<$<CONFIG:MINSIZEREL>:-Os> $<$<CONFIG:RELWITHDEBINFO>:-O2 -ggdb> > )

if(MSVC)
	target_link_libraries( mem_alloc_test Dbghelp )
endif(MSVC)
