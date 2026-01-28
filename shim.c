#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUF_SIZE 256

typedef struct memory {
	void* pointer;
	size_t size;
	struct memory* next;
} memory_block;
memory_block* head = NULL;

typedef void* (*orig_malloc_t)(size_t);
typedef void* (*orig_calloc_t)(size_t, size_t);
typedef void* (*orig_realloc_t)(void*, size_t);
typedef void (*orig_free_t)(void*);

orig_malloc_t real_malloc = NULL;
orig_calloc_t real_calloc = NULL;
orig_realloc_t real_realloc = NULL;
orig_free_t real_free = NULL;

__attribute__((constructor)) void init() {
	real_malloc = dlsym(RTLD_NEXT, "malloc");
	real_calloc = dlsym(RTLD_NEXT, "calloc");
	real_realloc = dlsym(RTLD_NEXT, "realloc");
	real_free = dlsym(RTLD_NEXT, "free");
	if (!real_malloc || !real_calloc || !real_free || !real_free) {
		perror("dlsym");
		exit(EXIT_FAILURE);
	}
}

static unsigned int total = 0;
static unsigned int allocs = 0;
static unsigned int frees = 0;

memory_block* create_block(void* ptr, size_t size) {
	memory_block* new = real_malloc(sizeof(memory_block));
	new->pointer = ptr;
	new->size = size;
	new->next = NULL;
	return new;
}

bool insert_block(memory_block* block) {
	if (head == NULL) {
		head = block;
	} else {
		block->next = head;
		head = block;
	}
	return true;
}

bool delete_block(memory_block* block) {
	if (block == head) {
		head = block->next;
	} else {
		memory_block* temp = head;
		while (temp != NULL && temp->next != block) temp = temp->next;
		if (temp == NULL) return false;
		temp->next = block->next;
	}
	block->next = NULL;
	real_free(block);
	return true;
}

void print_log(const char* buf) {
	char* logmsg = "[LOG] ";
	write(1, logmsg, strlen(logmsg) + 1);
	write(1, buf, strlen(buf) + 1);
}

void print_memory_status() {
	char buffer[BUF_SIZE];
	print_log("Status:\n");
	memory_block* temp = head;
	while (temp != NULL) {
		snprintf(buffer, BUF_SIZE, "[STATUS] -> %p of size %zu\n", temp->pointer, temp->size);
		print_log(buffer);
		temp = temp->next;
	}
}

__attribute__((destructor)) void exit_log() {
	char buffer[BUF_SIZE];
	print_log("<<<<<<< Heap Summary >>>>>>>\n");
	int blocks_leaked = allocs - frees;
	snprintf(buffer, BUF_SIZE, "[SUMMARY] Total bytes of memory leaked: %u bytes\n", total);
	print_log(buffer);
	snprintf(buffer, BUF_SIZE, "[SUMMARY]    => Total allocs: %u\n", allocs);
	print_log(buffer);
	snprintf(buffer, BUF_SIZE, "[SUMMARY]    => Total frees: %u\n", frees);
	print_log(buffer);
	snprintf(buffer, BUF_SIZE, "[SUMMARY]    => Total blocks of memory leaked: %d\n", blocks_leaked);
	print_log(buffer);
	if (blocks_leaked > 0) print_memory_status();
}

memory_block* find_ptr(void* ptr) {
	memory_block* temp = head;
	while (temp != NULL && temp->pointer != ptr) {
		temp = temp->next;
	}
	return temp;
}

void* malloc(size_t size) {
	char buffer[BUF_SIZE];
	void* ptr = real_malloc(size);
	memory_block* block = create_block(ptr, size);
	insert_block(block);
	allocs++;
	total += size;
	snprintf(buffer, BUF_SIZE, "Malloc: Allocate %zu bytes of memory at %p\n", size, ptr);
	print_log(buffer);
	return ptr;
}

void* calloc(size_t n, size_t size) {
	char buffer[BUF_SIZE];
	size_t memsize = n * size;
	void* ptr = real_calloc(n, size);
	memory_block* block = create_block(ptr, size);
	insert_block(block);
	allocs++;
	total += memsize;
	snprintf(buffer, BUF_SIZE, "Calloc: Allocate %zu bytes of memory at %p\n", memsize, ptr);
	print_log(buffer);
	return ptr;
}

void* realloc(void* ptr, size_t size) {
	char buffer[BUF_SIZE];
	void* new = real_realloc(ptr, size);
	memory_block* block = find_ptr(ptr);
	if (block == NULL) {
		memory_block* new_block = create_block(new, size);
		insert_block(new_block);
		total += size;
		allocs++;
		snprintf(buffer, BUF_SIZE, "Realloc: New memory allocated at %p of size %zu bytes\n", new, size);
		print_log(buffer);
		return new;
	}
	if (new == ptr) {
		size_t old = block->size;
		block->size = size;
		total = total + size - old;
		snprintf(buffer, BUF_SIZE, "Realloc: Realocated memory at %p from %zu to %zu bytes\n", ptr, old, size);
		print_log(buffer);
	} else {
		memory_block* new_block = create_block(new, size);
		insert_block(new_block);
		total = total + size - block->size;
		snprintf(buffer, BUF_SIZE, "Realloc: Realocated new memory at %p of %zu\n", new, size);
		print_log(buffer);
		snprintf(buffer, BUF_SIZE, "       : Deleted memory at %p of %zu\n", block->pointer, block->size);
		print_log(buffer);
		allocs++;
		frees++;
		delete_block(block);
	}
	return new;
}

void free(void* ptr) {
	if (ptr == NULL) {
		print_log("Free: Attempt to free NULL\n");
		return;
	}
	char buffer[BUF_SIZE];
	memory_block* block = find_ptr(ptr);
	if (block == NULL) {
		print_log("Free: Unknown memory tried to free\n");
		return;
	} else {
		size_t size = block->size;
		frees++;
		total -= size;
		snprintf(buffer, BUF_SIZE, "Free: %p of size %zu freed\n", ptr, size);
		print_log(buffer);
		delete_block(block);
	}
	real_free(ptr);
}
