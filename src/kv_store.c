// This file implements a concurrent KV store.
// It uses Open Addressing via Linear Probing.
// It implements this in a thread-safe manner,
// and allows a set of migrator threads to help
// Grow the table whenever the table is almost full.
// While the migration is happening all the writer threads
// Which are tasked to put data into the store are put to sleep
// on a condition variable, while the readers can still access
// And work on the read requests.

#include "common.h"
#include "ring_buffer.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>

typedef struct pair {
    int key;
    int val;
} Pair;

typedef struct migratorArgs {
    int start;
    int end;
    int new_cap;
} MigratorArgs;

typedef struct hashTable {
    Pair** table;
    int size;
    int capacity;
} HashTable;

#define LOAD_FACTOR 0.5
#define SUCCESS 10
#define FAILURE 0 
#define PRINTV(...) if (verbose) printf("Server: "); if (verbose) printf(__VA_ARGS__)

// Initialize the relevant condition Vars
char isMigration;
pthread_cond_t migration = PTHREAD_COND_INITIALIZER;
pthread_mutex_t migration_lock = PTHREAD_MUTEX_INITIALIZER;

// Global Variables

// The shared memory vars
struct ring *ring = NULL;
char *shmem_area = NULL;
char shm_file[] = "shmem_file";
// The heap allocated hash table
HashTable *table;
Pair **new_table;
int cap = 0;
// Array of server threads
pthread_t *server_threads;
int st_count = 0;
// Array of migrator threads
pthread_t *migrator_threads;
int mt_count = 0;
int verbose = 0;

// Wrapper for malloc which checks for error cases
void *Malloc(size_t size) {
    void *ptr = malloc(size);
    if (ptr == NULL) {
        perror("malloc");
    }
    return ptr;
}

void *migrateTable(void* args) {
    MigratorArgs *m_args = (MigratorArgs *) args;
    // The passed in arguments
    int start = m_args->start;
    int end = m_args->end;
    int new_cap = m_args->new_cap;
    
    // Starts copying data
    for (int i=start;i<end;i++) {
        Pair* curr = table->table[i];
        if (curr == NULL) {
            continue;
        }
        int new_i = hash_function(curr->key,new_cap);
        while (1) {
            new_i = new_i % new_cap;
            Pair* curr = new_table[new_i];
            // If the current pair pointer is null,
            // It means we don't have an entry in this place
            // and hence don't have such a key
            if (curr == NULL) {
                // Try to attomically insert the new pair into the table
                if (__atomic_compare_exchange(&new_table[new_i],
                            &curr,&table->table[i],0,__ATOMIC_SEQ_CST,
                            __ATOMIC_SEQ_CST)) {
                    break;
                }
                // Retry otherwise
                new_i--;
            }
            new_i++;
        }
    }
    // Exit this thread
    pthread_exit(NULL);
}
int get(key_type key) {
    HashTable *l_table;
    // Repeat get if the working table changed
    do {
        l_table = table;
        int i = hash_function(key, l_table->capacity);
        while (1) {
            i = i % l_table->capacity;
            Pair* curr = l_table->table[i];
            if (curr == NULL) {
               return FAILURE; 
            }
            
            if (curr->key == key) {
                return curr->val;
            }
            i++;
        }
    } while (!__atomic_compare_exchange(&table,&l_table, &l_table,
                            0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));
    return FAILURE;
}

// The insert method for our HashTable
int insert(key_type key, value_type val) {
    // Label to go to if insert was in the process
    // of migrating
start: 
    // If a migration is on going
    // Make the current thread sleep on the condition variable
    // migration
    if (isMigration) {
        pthread_mutex_lock(&migration_lock);
        pthread_cond_wait(&migration, &migration_lock);
        pthread_mutex_unlock(&migration_lock);
    }

    HashTable *l_table;
    l_table = table;
    int old_size;
    int new_size;
    int i = hash_function(key, l_table->capacity);
    Pair *newPair = Malloc(sizeof(Pair));
    newPair->key = key;
    newPair->val = val;
    
    while (1) {
        // If a migration was triggered
        // Go to start
        if (isMigration) {
            free(newPair);
            goto start;
        }
        i = i % l_table->capacity;
        Pair* curr = l_table->table[i];
        // If the current pair pointer is null,
        // It means we don't have an entry in this place
        // and hence don't have such a key
        if (curr == NULL) {
            // Try to attomically insert the new pair into the table
            if (__atomic_compare_exchange(&l_table->table[i],
                        &curr,&newPair,0,__ATOMIC_SEQ_CST,
                        __ATOMIC_SEQ_CST)) {
                // Update the size atomically
                do {
                   old_size = table->size; 
                   new_size = old_size+1;
                } while (!__atomic_compare_exchange(&l_table->size,&old_size, &new_size,
                            0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));
                break;
            }
            // Retry otherwise
            continue;
        }
        // If the key exists return failure
        if (curr->key == key) {
            free(newPair);
            return FAILURE;
        }
        i++;
    }
    
    if (isMigration) {
        free(newPair);
        goto start;
    } else {
        if (l_table->size > l_table->capacity * LOAD_FACTOR) {
            // Atomically set the migration var
            if (!__atomic_test_and_set(&isMigration,__ATOMIC_SEQ_CST)) {

               // Create a double capacity one
               new_table = Malloc(2*l_table->capacity * sizeof(Pair *)); 
               MigratorArgs *args = Malloc(mt_count * sizeof(MigratorArgs));
               int b_size = l_table->capacity / mt_count;

               // Create all the migrator threads
               for (int i=0;i<mt_count;i++) {
                   args[i].start = i * b_size;
                   args[i].end = (i==mt_count-1) ? l_table->capacity : (i+1) * b_size;
                   args[i].new_cap = 2 * l_table->capacity;
                   pthread_create(&migrator_threads[i],0,migrateTable,&args[i]);
               }
                
               // Wait for the migrator threads to join
               for (int i=0;i<mt_count;i++) {
                   if (pthread_join(migrator_threads[i], NULL)) 
                       perror("pthread_join");
               }

               // Once they join, 
               // Change the table and capacity
               table->table = new_table;
               table->capacity = 2 * table->capacity;
               // Clear the migration var 
               __atomic_clear(&isMigration, __ATOMIC_SEQ_CST);
               
               // Wake up all the sleeping writers
               pthread_mutex_lock(&migration_lock);
               pthread_cond_broadcast(&migration);
               pthread_mutex_unlock(&migration_lock);
            }
        }
    }
  // Check if migration is neeeded
    return SUCCESS;
}

// The server thread function which will check the 
void* server_thread(void* arg) {
    struct buffer_descriptor *res;
    struct buffer_descriptor bd;
    int ret;
    for (;;) {
        // Get the req
        ring_get(ring, &bd);
        //printf("Server %ld: Recieved %s (%d,%d)\n", pthread_self(),
        //        ((bd.req_type==PUT)? "put":"get"),bd.k,bd.v);                                                                                                                                
        PRINTV("Request Recieved\n");
        switch (bd.req_type) {
            case GET:
               ret = get(bd.k); 
               PRINTV("Get: <%u,%u>\n",bd.k,ret);
               res = (struct buffer_descriptor *)&shmem_area[bd.res_off];
               // Save the result at the start
               res->v = ret;
               res->ready = 1;
               break;
            case PUT:
               ret = insert(bd.k, bd.v);
               PRINTV("Finished insert <%u,%u>\n",bd.k,bd.v);
               res = (struct buffer_descriptor *)&shmem_area[bd.res_off];
               res->ready = 1;
               break;
        }
    }
}


void start_threads() {
    for (int i=0;i<st_count;i++) {
        pthread_create(&server_threads[i], NULL, &server_thread, NULL);
    }
}

void wait_threads() {
    for (int i=0;i<st_count;i++) {
        pthread_join(server_threads[i],NULL);
    }
}

void init_server() {

    // Initialize the shared
    // memory area
    FILE *fp = fopen(shm_file,"r+");
    fseek(fp,0,SEEK_END);
    int shm_size = ftell(fp);
    rewind(fp);
    int fd = fileno(fp);
	char *mem = mmap(NULL, shm_size, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
	if (mem == (void *)-1) 
		perror("mmap");
    fclose(fp);
    ring = (struct ring*) mem;
    shmem_area = mem;
    
    // Initialize the hash table
    table = Malloc(sizeof(HashTable));
    
    // Increase cap in case of more threads
    if (st_count > cap) {
        cap = st_count;
    }
    table->table = Malloc(sizeof(Pair *) * cap);
    memset(table->table, 0, sizeof(Pair *) * cap);
    table->size = 0;
    table->capacity = cap;

    // Initialize the server and migrator threads
    server_threads = Malloc(st_count * sizeof(pthread_t));
    migrator_threads = Malloc(st_count * sizeof(pthread_t));
    mt_count = st_count;
    PRINTV("Init Complete\n");
}

void usage(char *name) {
	printf("Usage: %s [-n num_threads] [-s init_table_size]\n", name);
	printf("-h show this help\n");
	printf("-n specify the number of server threads\n");
	printf("-s initial_table_size in the kv_store\n");
}

static int parse_args(int argc, char** argv) {

   int op;
   while ((op = getopt(argc,argv,"hvn:s:")) != -1) {
       switch (op) {
           case 'h':
               usage(argv[0]);
               exit(EXIT_SUCCESS);
               break;
           case 'n':
               st_count = atoi(optarg);
               break;
           case 's':
              cap = atoi(optarg); 
              break;
           case 'v':
              verbose = 1;
              break;
           default:
              usage(argv[0]);
              return -1;
       }

   }

   if (st_count == 0 || cap == 0) {
       usage(argv[0]);
       return -1;
   }
   return 0;
}


int main(int argc, char** argv) {
    if (parse_args(argc,argv) != 0)
        exit(EXIT_FAILURE);
    init_server();
    PRINTV("Starting Threads\n");
    start_threads();
    PRINTV("Main Waiting\n");
    wait_threads();
    printf("Ending server\n");
    return 0;
}
