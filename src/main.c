#include <stdio.h>
#include <stdbool.h>

#include "arena.h"

typedef struct s_Test {
	Arena arena;
	int val;

	void (*invoke)(struct s_Test *s, int step);
	void (*destroy)(struct s_Test *s);

} Test;

void test_invoke(Test *self, int step) {
	self->val += step;
}

void test_destroy(Test *self) {
	arena_term(&self->arena);
}

Test test_create(int val) {

	Test output;
	output.invoke = test_invoke;
	output.destroy = test_destroy;
	arena_init(&output.arena);
	output.val = val;
	return output;
}

int main(void) {
	printf("json-c\n");

	Test test = test_create(5);
	test.invoke(&test, 10);
	printf("new val: %d\n", test.val);
	test.destroy(&test);

	return 0;
}
