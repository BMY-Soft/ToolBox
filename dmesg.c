#if (defined __GNU__ && defined __MACH__) || (defined __sun && defined __SVR4)
#include "cat_u.c"
#endif
#include "dmesg_u.c"

int main(int argc, char *argv[]) {
	return dmesg_main(argc, argv);
}
