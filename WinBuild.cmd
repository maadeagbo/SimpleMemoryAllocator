cl /O2 /W4 /std:c++14 /EHsc /link Dbghelp.dll /Fe: memalloc_test_win32 *.c *.cpp
del /s *.obj