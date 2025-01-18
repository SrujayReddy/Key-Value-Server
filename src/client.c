#include <stdio.h>
#include <sys/mman.h>
#include <pthread.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <signal.h>
#include <string.h>

#include "common.h"
#include "ring_buffer.h"

#define MAX_THREADS 128
#define LINE_LEN 256

#define PUT_STR "put"
#define GET_STR "get"
#define DEL_STR "del"

#define READY 1
#define NOT_READY 0

struct request {
	key_type k;
	value_type v;
	enum REQUEST_TYPE t;
};

struct thread_context {
	int tid; /* thread ID */
	int num_reqs; /* # of requests that this thread is responsible for */
	struct request *reqs; /* requests assigned to this thread */
	struct buffer_descriptor *res; /* Corresponding result for each request in reqs */
	struct buffer_descriptor *comps; /* Pointer to the start of the status board for this thread */
	int win_size;
	int nxt_comp; /* next completion that we're expecting */
	int comp_off; /* byte offset of the status board for this thread, w.r.t the start of the shared memory area */
};

struct ring *ring = NULL;
char *shmem_area = NULL;
char shm_file[] = "shmem_file";
char workload_file[256];
char expected_file[256];
pthread_t threads[MAX_THREADS];
struct thread_context contexts[MAX_THREADS];
struct request *requests;
struct buffer_descriptor *results;
int num_threads = 4;
int win_size = 1;
int num_requests = 4;
int verbose = 0;
int child_pid = -1;
int do_fork = 0;
int validate = 0;

/* Server arguments */
int s_num_threads = 1;
int s_init_table_size = 1000;

/* prints "Client" before each line of output because the child will also be printing
 * to the same terminal */
#define PRINTV(...) if (verbose) printf("Client: "); if (verbose) printf(__VA_ARGS__)

/*
 * Fork the server program as a child process
*/
void fork_server() {
	pid_t pid = fork();
	
	if (pid == 0) { /* The child process */
		/* number of arguments including the NULL pointer at the end */
		const int NUM_ARGS = 5;
		const int MAX_ARG_LEN = 256;
		char **argv = malloc(NUM_ARGS * sizeof(char *));
		if (argv == NULL)
			perror("malloc");

		for (int i = 0; i < NUM_ARGS; i++) {
			argv[i] = malloc(MAX_ARG_LEN * sizeof(char));
			if (argv[i] == NULL)
				perror("malloc");
		}

		int idx = 0;
		strcpy(argv[idx++], "./server");
		sprintf(argv[idx++], "-s %d", s_init_table_size);
		sprintf(argv[idx++], "-n %d", s_num_threads);
		if (verbose)
			sprintf(argv[idx++], "-v");
		argv[idx++] = NULL;
		execvp("./server", argv);

		/* Will only reach here if there's an error with execvp */
		perror("execvp");
	}
	else if (pid > 0) { /* The parent process if there was no error */
		child_pid = pid;
	} else { /* The parent process in case of an error with fork */
		perror("fork");
	}
}

/*
 * Initialize the shared memory ring buffer
 * Sets the shmem_area global variable to the beginning of the shared region
 * Sets the ring global variable the beginning of the shared region 
 * Shared memory area is organized as follows:
 * | RING | TID_0_COMPLETIONS | TID_1_COMPLETIONS | ... | TID_N_COMPLETIONS |
*/
int init_client() {
	int shm_size = sizeof(struct ring) + 
		num_threads * win_size * sizeof(struct buffer_descriptor);
	
	int fd = open(shm_file, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if (fd < 0)
		perror("open");

	/* Make the file length exactly shm_size bytes */
	if (ftruncate(fd, shm_size) == -1)
		perror("ftruncate");

	char *mem = mmap(NULL, shm_size, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
	if (mem == (void *)-1) 
		perror("mmap");

	/* mmap dups the fd, no longer needed */
	close(fd);

	memset(mem, 0, shm_size);
	ring = (struct ring *)mem;
	shmem_area = mem;
	int ring_rc = -1;
	if (ring_rc = init_ring(ring) < 0) {
		printf("Ring initialization failed with %d as return code\n", ring_rc);
		exit(EXIT_FAILURE);
	}

	if (do_fork)
		fork_server();
}

/*
 * Get request type from req_str and set type
 * @return 0 on success, -1 on failure
 * On failure, contents of type are undefined 
*/
int get_req_type(char *req_str, enum REQUEST_TYPE *type) {
	int rc = 0;
	if (!strcmp(req_str, PUT_STR))
		*type = PUT;
	else if (!strcmp(req_str, GET_STR))
		*type = GET;
	else
		rc = -1;

	return rc;
}

/* 
 * Parses an input line and stores the result into requests at index
 * @return 0 on success, -1 on failure
*/
int add_line_to_req(char *line, int index) {
	char *copy_line = strdup(line);
	char *tok = strtok(copy_line, " ");
	if (tok == NULL)
		return -1;

	enum REQUEST_TYPE type;
	if (get_req_type(tok, &type) < 0)
		return -1;

	requests[index].t = type;

	tok = strtok(NULL, " ");
	if (tok == NULL)
		return -1;

	int key = atoi(tok);
	requests[index].k = key;

	int value;
	if (type == PUT) {
		tok = strtok(NULL, " ");
		if (tok == NULL)
			return -1;

		value = atoi(tok);
		requests[index].v = value;
	}
	return 0;
}

int count_lines(FILE *f) {
	char line[LINE_LEN];
	int nl = 0;
	while(!feof(f)) {
		fgets(line, LINE_LEN, f);
		/* This prevents the last line from being read twice */
		if (feof(f))
			break;
		nl++;
	}
	fseek(f, 0, SEEK_SET);
	return nl;
}

/*
 * Reads the workload_file and stores in the requests array (global var)
 * Allocates both requests/results array enough space for all requests
*/
void read_input_files() {
	FILE *f = fopen(workload_file, "r");
	if (f == NULL)
		perror("fopen");

	/* Count the number of lines */
	int nl = count_lines(f);
	PRINTV("Num lines is %d\n", nl);
	num_requests = nl;

	/* Allocate request/result arrays */
	requests = malloc(num_requests * sizeof(struct request));
	if (requests == NULL)
		perror("malloc");
	results = malloc(num_requests * sizeof(struct buffer_descriptor));
	if (results == NULL)
		perror("malloc");

	/* Read line by line and fill up the requests array
	 * Ignores invalid lines */
	char line[LINE_LEN];
	int index = 0;
	while (!feof(f)) {
		fgets(line, LINE_LEN, f);
		if (add_line_to_req(line, index) < 0)
			continue;
		
		index++;
	}
}

/*
 * Submits as many requests as win_size allows 
 * last_submitted is updated in this function
 * @param ctx Context for this thread
 * @param last_completed last request that was completed
 * @param last_submitted last request that was submitted
*/
void submit_reqs(struct thread_context *ctx, int *last_completed, int *last_submitted) {
	struct buffer_descriptor bd;
	struct request *reqs = ctx->reqs;
	/* Keep win_size number of in-flight requests */
	for (int i = *last_submitted; *last_submitted - *last_completed < win_size; i++) {
		/* Have we submitted all of the requests? */
		if (*last_submitted >= ctx->num_reqs)
			break;

		memset(&bd, 0, sizeof(struct buffer_descriptor));
		bd.k = reqs[i].k;
		bd.v = reqs[i].v;
		bd.req_type = reqs[i].t;
		bd.res_off = ctx->comp_off + (*last_submitted % win_size) * sizeof(struct buffer_descriptor);
		ring_submit(ring, &bd);
		(*last_submitted)++;

		PRINTV("New submission %u %u\n", bd.k, bd.v);
	}
}

/*
 * Check possible completions in the request status board
 * Updates last_completed if there are any new completions
 * @param ctx context for this thread
 * @param last_completed last request that was completed
 * @param last_submitted last request that was submitted
*/
void process_completions(struct thread_context *ctx, int *last_completed, int *last_submitted) {
	/* Check completions until we break */
	while (true) {
		/* We're expecting ctx->nxt_comp to be completed. If that's not
		 * completed, we're done for now. Otherwise, process that and 
		 * check the next one.
		 * Notice that we're only allowing 'in-order acknowledgements'. */
		if (ctx->comps[ctx->nxt_comp].ready == READY) {
			struct buffer_descriptor tmp = ctx->comps[ctx->nxt_comp];
			PRINTV("New completion: %u %u\n", tmp.k, tmp.v);
			ctx->comps[ctx->nxt_comp].ready = NOT_READY;
			memcpy(&ctx->res[*last_completed], &ctx->comps[ctx->nxt_comp],
				       	sizeof(struct buffer_descriptor));

			/* Update for the next iteration */
			(*last_completed)++;
			ctx->nxt_comp = (ctx->nxt_comp + 1) % ctx->win_size;
			PRINTV("LC=%d\n", *last_completed);
		}
		else
			break;
	}
}

/* 
 * Function that's run by each thread
 * @param arg context for this thread
*/
void *thread_function(void *arg) {
	struct thread_context *ctx = arg;
	int last_completed = 0;
	int last_submitted = 0;
	PRINTV("Num reqs is %d\n", ctx->num_reqs);
	/* Keep submitting the requests and processing the completions */
	for (; last_submitted < ctx->num_reqs; ) {
		submit_reqs(ctx, &last_completed, &last_submitted);	
		process_completions(ctx, &last_completed, &last_submitted);
	}

	PRINTV("Done with subs\n");
	/* There might be some completions still in flight */
	while (last_completed < ctx->num_reqs)
		process_completions(ctx, &last_completed, &last_submitted);
}

/*
 * Launch num_threads number of threads
 * Prepares the context for each thread
 * The way we assign work to each thread is as follows:
 *
 * 	|  T0  |  T1  |  ...  |  TN  |
 *  	---------- REQUESTS ----------
 *  
 *  Each thread submits an equal contiguous part of the requests
*/
void start_threads() {
	int reqs_per_th = num_requests / num_threads;
	struct request *r = requests;
	struct buffer_descriptor *rs = results;

	for (int i = 0; i < num_threads; i++) {
		contexts[i].tid = i;
		contexts[i].num_reqs = reqs_per_th;
		contexts[i].reqs = r;
		contexts[i].win_size = win_size;
		contexts[i].comps = (struct buffer_descriptor *) (shmem_area + sizeof(struct ring) + i * win_size * sizeof(struct buffer_descriptor));
		contexts[i].res = rs;
		/* This is the byte offset to the first window for this thread */
		contexts[i].comp_off = sizeof(struct ring) + contexts[i].tid * win_size * sizeof(struct buffer_descriptor);

		if (pthread_create(&threads[i], NULL, &thread_function, &contexts[i]))
			perror("pthread_create");

		/* Each thread is only responsible for an equal part of requests */
		r += reqs_per_th;
		rs += reqs_per_th;
	}
}

void wait_for_threads() {
	for (int i = 0; i < num_threads; i++)
		if (pthread_join(threads[i], NULL))
			perror("pthread_join");
}

void usage(char *name) {
	printf("Usage: %s [-h] [-n num_threads] [-w win_size] [-v] [-t kv_store_threads] [-s init_table_size] [-f]\n", name);
	printf("-h show this help\n");
	printf("-n specify the number of threads\n");
	printf("-w specify the window size (max distance between last submitted request and last completed request\n");
	printf("-v give verbose output if set\n");
	printf("-t number of threads in the kv_store program (ignored if -f is not set)\n");
	printf("-s initial_table_size in the kv_store program (ignored if -f is not set)\n");
	printf("-f if set, forks the kv_store program as the child process - '-t' and '-s' options are only effective if this is set\n");
	printf("-c if set, checks the result of get queries - only works if -n 1 and -w 1 (synchronus submission)\n");
	printf("-l input workload file name (default: workload.txt)\n");
	printf("-e file name that contains the expected results for get queries(default: solution.txt)\n");
}

static int parse_args(int argc, char **argv)
{
	/* Default file name */
	strcpy(workload_file, "workload.txt");
	strcpy(expected_file, "solution.txt");

	int op;
	while ((op = getopt(argc, argv, "hn:w:vt:s:fce:i:")) != -1) {
		switch (op) {
		case 'h':
		usage(argv[0]);
		exit(EXIT_SUCCESS);
		break;
																	           
		case 'n':
		num_threads = atoi(optarg);
		break;
	
		case 'w':
		win_size = atoi(optarg);
		break;

		case 'v':
		verbose = 1;
		break;

		case 't':
		s_num_threads = atoi(optarg);
		break;

		case 's':
		s_init_table_size = atoi(optarg);
		break;

		case 'f':
		do_fork = 1;
		break;
		
		case 'c':
		validate = 1;
		break;
	
		case 'i':
		strcpy(workload_file, optarg);
		break;

		case 'e':
		strncpy(expected_file, optarg, 256);
		break;
		
		default:
		usage(argv[0]);
		return 1;
		}
	}
	return 0;
}

/* Return the elapsed time between two timespecs in ns. */
double get_elapsed_ns(struct timespec *start, struct timespec *end) {
    long es = end->tv_sec - start->tv_sec;
    long ens = end->tv_nsec - start->tv_nsec;
    double elapsed = es * 1e9 + ens;
    return elapsed;
}

/* 
 * Reads the solution file
 * Line n of this file is a number which specifies the result of the nth get request
 * @param f the solution file
 * @param exp an allocated array to store the values in
*/
void read_expected_file(FILE *f, value_type *exp) {
	char line[LINE_LEN];
	int idx = 0;
	while (!feof(f)) {
		fgets(line, LINE_LEN, f);
		/* Prevent the last line from being read twice */
		if (feof(f))
			break;

		exp[idx++] = atoi(line);
	}
}

/*
 * Check if the results returned by the server match the expected values
 * This function is only called if -c option is set
 * @param expected expected values (nth element is the result of nth get request)
 * @return 0 on success, 1 otherwise
*/
int check_results(value_type *expected) {
	int exp_idx = 0;
	for (int i = 0; i < num_requests; i++) {
		/* Only interested in GET requests */
		if (requests[i].t != GET)
			continue;

		/* Mismatch! */
		if (results[i].v != expected[exp_idx]) {
			fprintf(stderr, "Get(%u) should return %u, but got %u\n", 
					results[i].k, expected[exp_idx], results[i].v);
			fprintf(stderr, "Indices: req=%d exp=%d\n", i, exp_idx);
			return 1;
		}
		exp_idx++;
	}

	/* No mismatch found */
	return 0;
}

/*
 * Check the correctness of the results and print performance numbers
 * @param s start timestamp
 * @param e end timestamp
 * @return 0 on success, 1 if the check fails
*/
int process_results(struct timespec *s, struct timespec *e) {
	if (validate) {
		value_type *expected = NULL;
		FILE *f = fopen(expected_file, "r");
		if (f == NULL)
			perror("fopen");

		int nl = count_lines(f);
		expected = malloc(nl * sizeof(value_type));
		if (expected == NULL)
			perror("malloc");

		read_expected_file(f, expected);

		if (check_results(expected) != 0)
			return 1;
	}

	double ns = get_elapsed_ns(s, e);
	/* Throughput in K requests per second */
	double tput = (num_requests * 1e6) / ns;
	printf("Total time: %f ms\nThroughput: %f K/s\n", ns / 1e6, tput);

	/* No errors in check results */
	return 0;
}

int main(int argc, char *argv[]) {
	if (parse_args(argc, argv) != 0)
		exit(EXIT_FAILURE);

	init_client();

	read_input_files();

	struct timespec s, e;
	clock_gettime(CLOCK_REALTIME, &s);

	start_threads();
	wait_for_threads();

	clock_gettime(CLOCK_REALTIME, &e);

	/* Kill the server app */
	if (child_pid > 0)
		kill(child_pid, SIGKILL);

	return process_results(&s, &e);
}
