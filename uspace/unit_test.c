#include <types.h>
#include <stddef.h>
#include <syscall.h>
#include <errno.h>

static void usage();
static void echo_test();
static void ls_test();
static void cat_test();

int main(int argc, char **argv)
{
	int rc = 0;
	int nr_round;

	if (argc < 3) {
		usage();
		goto out;
	}

	if (strcmp(argv[1], "-n") != 0) {
		usage();
		goto out;
	}

	nr_round = atoi(argv[2]);
	if (nr_round == 0) {
		usage();
		goto out;
	}
	
	do {
		rc = unit_test(nr_round);
		if (rc != 0) {
			printf("unit_test failed.\n");
		} else {
			printf("unit_test finished with round %d.\n", nr_round);
		}
		
	} while (FALSE);

	echo_test();
	
	ls_test();

	cat_test();

	while (TRUE) {
		;
	}

 out:

	return rc;
}

void echo_test()
{
	int rc;
	char *echo[] = {
		"/echo",
		"Message from echo.",
		NULL
	};

	printf("unit_test echo message:\n");
	
	rc = create_process(echo[0], echo, 0, 16);
	if (rc == -1) {
		printf("create_process(%s) failed, err(%d).\n", echo[0], rc);
		goto out;
	}

 out:
	return;
}

void ls_test()
{
	int rc;
	char *ls[] = {
		"/ls",
		"/",
		NULL
	};

	printf("unit_test list directory:\n");
	
	rc = create_process(ls[0], ls, 0, 16);
	if (rc == -1) {
		printf("create_process(%s) failed, err(%d).\n", ls[0], rc);
		goto out;
	}

 out:
	return;
}

void cat_test()
{
	int rc;
	char *cat[] = {
		"/cat",
		"/crontab",
		NULL
	};

	printf("unit_test cat file:\n");

	rc = create_process(cat[0], cat, 0, 16);
	if (rc == -1) {
		printf("create_process(%s) failed, err(%d).\n", cat[0], rc);
		goto out;
	}

 out:
	return;
}

void usage()
{
	printf("unit_test -n round-number\n"
	       "Example: unit_test -n 100\n"
	       );
}