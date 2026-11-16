// The libraries of MemfdLoadTest.cpp: built twice, the second with -DSECOND.
#ifdef SECOND
int memfd_test_second = 2;
#else
int memfd_test_first = 1;
#endif
