#include <types.h>
#include <stddef.h>
#include "test.h"

void unit_test()
{
	print_test();
	list_test();
	lock_test();
	mem_test();
	avl_tree_test();
	
	while (1);
}
