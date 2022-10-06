// Dan Ha
// hadan@pdx.edu

#include "vikalloc.h"

// size of data structure node
#define BLOCK_SIZE (sizeof(mem_block_t))
// takes ptr to start of block, rets ptr to start of user's data
#define BLOCK_DATA(__curr) (((void *) __curr) + (BLOCK_SIZE))
// takes ptr to start of user's data, rets ptr to start of data structure data
#define DATA_BLOCK(__data) ((mem_block_t *) (__data - BLOCK_SIZE))

#define IS_FREE(__curr) ((__curr -> size) == 0)

#define PTR "0x%07lx"
#define PTR_T PTR "\t"

static mem_block_t *block_list_head = NULL;
static mem_block_t *block_list_tail = NULL;
static void *low_water_mark = NULL;
static void *high_water_mark = NULL;
// only used in next-fit algorithm
static mem_block_t *prev_fit = NULL;

static uint8_t isVerbose = FALSE;
static vikalloc_fit_algorithm_t fit_algorithm = FIRST_FIT;
static FILE *vikalloc_log_stream = NULL;

static void init_streams(void) __attribute__((constructor));

static size_t min_sbrk_size = MIN_SBRK_SIZE;

static void 
init_streams(void)
{
    vikalloc_log_stream = stderr;
}

size_t
vikalloc_set_min(size_t size)
{
    if (0 == size) {
        // just return the current value
        return min_sbrk_size;
    }
    if (size < (BLOCK_SIZE + BLOCK_SIZE)) {
        // In the event that it is set to something silly small.
        size = MAX(BLOCK_SIZE + BLOCK_SIZE, SILLY_SBRK_SIZE);
    }
    min_sbrk_size = size;

    return min_sbrk_size;
}

void 
vikalloc_set_algorithm(vikalloc_fit_algorithm_t algorithm)
{
    fit_algorithm = algorithm;
    if (isVerbose) {
        switch (algorithm) {
        case FIRST_FIT:
            fprintf(vikalloc_log_stream, "** First fit selected\n");
            break;
        case BEST_FIT:
            fprintf(vikalloc_log_stream, "** Best fit selected\n");
            break;
        case WORST_FIT:
            fprintf(vikalloc_log_stream, "** Worst fit selected\n");
            break;
        case NEXT_FIT:
            fprintf(vikalloc_log_stream, "** Next fit selected\n");
            break;
        default:
            fprintf(vikalloc_log_stream, "** Algorithm not recognized %d\n"
                    , algorithm);
            fit_algorithm = FIRST_FIT;
            break;
        }
    }
}

void
vikalloc_set_verbose(uint8_t verbosity)
{
    isVerbose = verbosity;
    if (isVerbose) {
        fprintf(vikalloc_log_stream, "Verbose enabled\n");
    }
}

void
vikalloc_set_log(FILE *stream)
{
    vikalloc_log_stream = stream;
}

void *
vikalloc(size_t size)
{
	size_t mem_needed = BLOCK_SIZE + size;
	size_t mem_requested = 0;
	mem_block_t *curr = block_list_head;

    if (isVerbose) {
        fprintf(vikalloc_log_stream, ">> %d: %s entry: size = %lu\n"
                , __LINE__, __FUNCTION__, size);
    }

	// if passed size is 0 or NULL, return NULL
	if (!size)
		return NULL;

	// find how many bytes to ask OS for, if needed
	mem_requested = (((size + BLOCK_SIZE) / min_sbrk_size) + 1) * min_sbrk_size;

	// the very first allocation
	if (!block_list_head) {
		block_list_head = (mem_block_t*) sbrk(mem_requested);
		if (((void*) -1) == block_list_head)
			return NULL;

		low_water_mark = block_list_head;
		high_water_mark = low_water_mark + mem_requested;
		block_list_tail = block_list_head;

		block_list_head->capacity = mem_requested - BLOCK_SIZE;
		block_list_head->size = size;
		block_list_head->next = block_list_head->prev = NULL;
		return BLOCK_DATA(block_list_head);
	}

	// for subsequent allocations, traverse list to find block w/ enough capacity
	// capacity - currSize = available space
	while (curr && ((curr->capacity - curr->size) < mem_needed))
		curr = curr->next;

	// if empty block was found, immediately add
	if (curr && !(curr->size)) {
		curr->size = size;
		return BLOCK_DATA(curr);
	}

	// if block w/ enough capacity is found, split it
	if (curr) {
		mem_block_t *new_block = BLOCK_DATA(curr) + curr->size;
		new_block->capacity = (curr->capacity - curr->size) - BLOCK_SIZE;
		new_block->size = size;
		new_block->next = curr->next;
		new_block->prev = curr;

		curr->capacity = curr->size;
		curr->next = new_block;
		if (new_block->next)
			new_block->next->prev = new_block;
		else
			block_list_tail = new_block;

		return BLOCK_DATA(new_block);
	}

	// if no block w/ enough capacity is found, request more mem
	curr = (mem_block_t*) sbrk(mem_requested);
	if (((void*) -1) == curr)
		return NULL;

	high_water_mark += mem_requested;
	curr->capacity = mem_requested - BLOCK_SIZE;
	curr->size = size;
	curr->next = NULL;
	curr->prev = block_list_tail;

	block_list_tail->next = curr;
	block_list_tail = curr;

    return BLOCK_DATA(curr);
}

void 
vikfree(void *ptr)
{
	mem_block_t *to_free = DATA_BLOCK(ptr);
	mem_block_t *next_block = to_free->next;
	mem_block_t *prev_block = to_free->prev;

	/*
    if (isVerbose) {
        fprintf(vikalloc_log_stream, ">> %d: %s entry\n"
                , __LINE__, __FUNCTION__);
	}
	*/

	// mark curr data as free to use
	if (to_free->size)
		to_free->size = 0;
	// if already freed and no need to coalesce, this is double free
	else if ((next_block && next_block->size) || !next_block) {
		if (isVerbose) {
			fprintf(vikalloc_log_stream, "Block is already free: ptr = " PTR "\n"
					, (long) (ptr - low_water_mark));
		}
		
		return;
	}

	// if next block is also free, coalesce
	if (next_block && !(next_block->size)) {
		to_free->next = next_block->next;

		if (next_block == block_list_tail)
			block_list_tail = to_free;
		else
			next_block->next->prev = to_free;
		
		to_free->capacity += (BLOCK_SIZE + next_block->capacity);
	}

	// if prev block is also free, recurse down to coalesce up
	if (prev_block && !(prev_block->size))
		vikfree(BLOCK_DATA(prev_block));

    return;
}

///////////////

void 
vikalloc_reset(void)
{
    if (isVerbose) {
        fprintf(vikalloc_log_stream, ">> %d: %s entry\n"
                , __LINE__, __FUNCTION__);
    }

    if (low_water_mark != NULL) {
        if (isVerbose) {
            fprintf(vikalloc_log_stream, "*** Resetting all vikalloc space ***\n");
        }

		brk(low_water_mark);
		high_water_mark = block_list_head = block_list_tail = NULL;
    }
}

void *
vikcalloc(size_t nmemb, size_t size)
{
    void *ptr = NULL;
    
    if (isVerbose) {
        fprintf(vikalloc_log_stream, ">> %d: %s entry\n"
                , __LINE__, __FUNCTION__);
    }

    return ptr;
}

void *
vikrealloc(void *ptr, size_t size)
{
    if (isVerbose) {
        fprintf(vikalloc_log_stream, ">> %d: %s entry\n"
                , __LINE__, __FUNCTION__);
    }

    return ptr;
}

void *
vikstrdup(const char *s)
{
    void *ptr = NULL;

    if (isVerbose) {
        fprintf(vikalloc_log_stream, ">> %d: %s entry\n"
                , __LINE__, __FUNCTION__);
    }

    return ptr;
}

#include "vikalloc_dump.c"
