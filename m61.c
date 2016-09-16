#define M61_DISABLE 1
#include "m61.h"
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// Create a linked list to store allocated memory pointers(nodes)
typedef struct node{
    unsigned long long buffer;
    char* 	       address;
    struct 	       node* next;
    struct             node* prev;
    bool               active;
    const char*        fileName;
    int                lineNumber;
    size_t             allocationSize;
    int                padding;   
}node;

// Initialize a node in the linked list of freed pointers
typedef struct freeNode{
    unsigned long long buffer;
    char*              address;
    struct             freeNode* next;
}freeNode;

// Initialize a statistics struct
struct m61_statistics stats = {.nactive     = 0, 
			       .active_size = 0, 
			       .ntotal      = 0,    
			       .total_size  = 0,
                               .nfail       = 0, 
			       .fail_size   = 0, 
			       .heap_min    = NULL, 
			       .heap_max    = NULL
			      };

// Check number of the overflow buffer 
unsigned long long deadbeef = 0xDEADBEEF;

// nodes: variable to store number of succesfull allocation (nodes in linked list of active allocation) 
int successNodes = 0;
                                  
// Initialize the active pointers linked list
struct node* firstActive = NULL;

// Initialize the free pointers linked list 
struct freeNode* firstFree = NULL;

// Struct to store heavy hitter data
typedef struct heavHitterNode{
    			struct             heavHitterNode* next;
    			const char*        fileName;
    			int                lineNumber;
    			int                numAllocations;
    			unsigned long long size;
		      }heavHitterNode;

// firstheavHitter: pointer to heavy hitter nodes linked list 
heavHitterNode* firstheavHitter = NULL;

/* totalAllocatedBytes: a global variable to store the number of  allocated 
                        bytes of all nodes in the heavy hitter list*/
unsigned long long totalAllocatedBytes = 0;

/* A function to insert a node into the heavy hitter linked list if (file,line) pair 
   is already there and increment size by sz. 
   else, create a new node and add to the begining of the linked list   */
void insertheavHitterNode(const char* file, int line, unsigned long long sz)
{
    heavHitterNode* ptr = firstheavHitter;
    
    while(ptr != NULL)
    {
        // if the (file,line) pair is already in the list
        if (ptr->fileName == file && ptr->lineNumber == line)
        {
            ptr->numAllocations += 1;
            ptr->size += sz;
            totalAllocatedBytes += sz;
            return;
        }
        ptr = ptr->next;
    }
    
    // The else part: create new node and add to front of list
    heavHitterNode* new = malloc(sizeof(heavHitterNode));
    new->fileName = file;
    new->lineNumber = line;
    new->numAllocations = 1;
    new->size = sz;
    new->next = firstheavHitter;
    firstheavHitter = new;
    totalAllocatedBytes += sz;   
}


/* A function to insert a pointer into the list of freed 
   pointers after a successfully freeing a pointer */
bool insertFreePtr(char* newAddrss)
{
    struct freeNode* newptr = malloc(sizeof(struct freeNode));
    if (newptr == NULL)
    {
        return false;
    }
    
    // Node initializiation
    newptr->buffer  = deadbeef;
    newptr->address = newAddrss;
    newptr->next    = NULL;
   
    if (firstFree == NULL)
    {
        firstFree = newptr;
    }
        
    // else insert new node at head of linked list
    else
    {
        newptr->next = firstFree;
        firstFree = newptr;
    }
    return true;   
}   

/* A function to chech the existance of a specific pointer 
   in the list of freed pointers*/
bool checkFreePtr(char* check)
{
    freeNode* ptr = firstFree;
    while (ptr != NULL)
    {
        if (ptr->address == check)
             return true;
        ptr = ptr->next;
    }
    return false;
}

/* A function that is called whenever we malloc to remove 
   a pointer from the linked list of free pointers  */
void removeFreePtr(char* check)
{
    freeNode* ptr = firstFree;
    freeNode* previousPtr = NULL;
    while(ptr != NULL)
    {
        if (ptr->address == check)
        {
            // if the pointer at the head
            if (ptr == firstFree)
            {
                // remove node from list 
                firstFree = ptr->next;
                free(ptr);
            }
            else
            {
                // remove node from list
                previousPtr->next = ptr->next;
                free(ptr);    
            }
        }
        previousPtr = ptr;
        ptr = ptr->next;
    }
}


/* A function to insert at the head of the linked list of active alloocations */
bool insertPtr(char* newAddrss, const char* file, int line, size_t size)
{
    struct node* newptr = malloc(sizeof(struct node));
    if (newptr == NULL)
    {
        return false;
    }
    
    // initialize node
    newptr->buffer = deadbeef;
    newptr->address = newAddrss;
    newptr->next = NULL;
    newptr->active = true;
    newptr->fileName = file;
    newptr->lineNumber = line;
    newptr->allocationSize = size;
    
    // remove from free list
    removeFreePtr(newAddrss);
    
    // check for empty list
    if (firstActive == NULL)
        firstActive = newptr;
        
    // else insert new node at head of list
    else
    {
        newptr->next = firstActive;
        firstActive = newptr;
    }
    
    successNodes++;
    return true;   
}

// A function to check if a node is storing the memory address check
bool checkActive(char* check)
{
    node* ptr = firstActive;
    while (ptr != NULL)
    {
        if (ptr->address == check)
            return true;
        ptr = ptr->next;
    }
    return false;
}

/* check to see if a pointer is in an allocated region
   if it is print the proper error message            */
void wildWrite(char* check)
{
    node* ptr = firstActive;
    while (ptr != NULL)
    {
        // check if the address we check is in an allocated region
        if ((ptr->address < check) && (check < (ptr->address + ptr->allocationSize)))
        {   
            // region size
            int bytes = check - ptr->address;
            
            // Error message
            printf("  %s:%i: %p is %i bytes inside a %zu byte region allocated here\n", 
                        ptr->fileName, ptr->lineNumber, check, bytes, ptr->allocationSize);
        }
        ptr = ptr->next;
    } 
}


void* m61_malloc(size_t sz, const char* file, int line) {
    (void) file, (void) line;   // avoid uninitialized variable warnings
    
    // Check if malloc argument will overflow
    if (sz >= ((size_t) -1) - 2 * sizeof(unsigned long long))
    {
        stats.fail_size += sz;
        stats.nfail++;
        return NULL;
    }
        
    // Allocate memory of size (sz + metadata + buffer)
    void* voidPtr = malloc(sz + sizeof(node) + sizeof(unsigned long long));
    node* nodePtr = (node*) voidPtr;
    if (nodePtr != NULL)
    {
        // get voidPtr to the begining of user allocated data
        voidPtr += sizeof(node);
        
        // update data
        stats.nactive++;
        stats.ntotal++;
        stats.active_size += sz;
        stats.total_size += sz;
        
        // store the metadata
        nodePtr->buffer         = deadbeef;
        nodePtr->address        = voidPtr;
        nodePtr->next           = firstActive;
        nodePtr->prev           = NULL;
        nodePtr->active         = true;
        nodePtr->fileName       = file;
        nodePtr->lineNumber     = line;
        nodePtr->allocationSize = sz;
        
        // move metadata node to the linked list of active pointers
        firstActive = nodePtr;
        if (nodePtr->next != NULL)
            nodePtr->next->prev = nodePtr;
       
        // create the buffer at the end to detect boundary errors
        unsigned long long* bufferPtr = (unsigned long long*) (voidPtr + sz);
        *bufferPtr = deadbeef;
        
        // if nothing stored in stats.heap_min
        if (stats.heap_min == NULL)
            stats.heap_min = (char*) voidPtr;

        /* if new allocation is less than the one in the struct
           store the pointer to that string in heap_min
        */
        else if ((char*) voidPtr < stats.heap_min)
            stats.heap_min = (char*) voidPtr;

        /* if nothing stored in stats.heap_max, 
	   store the pointer to that string in heap_max */
        if (stats.heap_max == NULL)
            stats.heap_max = (char*) voidPtr + (int) sz;
      
        /* if new allocation is higher than stats.heap_max,
	   store the pointer to that string in heap_max    */
        else if (((char*) voidPtr + (int) sz) > stats.heap_max)
            stats.heap_max = (char*) voidPtr + (int) sz;   
    }
    
    // a case of malloc failedr, we update stsats related to failuer    
    else
    {
        stats.nfail++;
        stats.fail_size += sz;
        return NULL;
    }
   
    // Add a sample of allocation and add it to heavHitterList 
    if (drand48() > 0.7)
        insertheavHitterNode(file,line,sz);

    removeFreePtr(voidPtr);
    return voidPtr;
}

void m61_free(void *ptr, const char *file, int line)
{
    (void) file; 
    (void) line;  
    if (ptr == NULL)
        return;
   
    // get to the start of the metadata
    ptr -= sizeof(node);
    node* nodePtr = (node*) ptr;
    
    // get to pointer returned by malloc
    ptr += sizeof(node);
        
    // if ptr isn't on the heap    
    if (ptr < (void*) stats.heap_min || ptr > (void*)stats.heap_max)
    {
        printf("MEMORY BUG: %s:%i: invalid free of pointer %p, not in heap\n", file, line, ptr);
        abort();
    }
    
    // if ptr has never been allocated
    if (checkActive(ptr) == false && checkFreePtr(ptr) == false )
    {
        printf("MEMORY BUG: %s:%i: invalid free of pointer %p, not allocated\n", file, line, ptr);
        wildWrite(ptr);
        abort();    
    }
    // if a double free
    if (checkFreePtr(ptr) == true)
    {
        printf("MEMORY BUG: %s:%i: invalid free of pointer %p\n", file, line, ptr);
        abort();
    }
        
    stats.nactive--;
    unsigned long long size = nodePtr->allocationSize;
    
    // get buffer value
    void* bufferPtr = (ptr + size);
    unsigned long long buffer = *((unsigned long long*) bufferPtr);
    
    // if buffer was overwritten
    if (buffer != deadbeef)
    {
        printf("MEMORY BUG: %s:%i: detected wild write during free of pointer %p\n", file, line, ptr);
        abort();
    }
    
    // if memeory was overwritten 
    if (nodePtr->active == true && checkFreePtr(ptr))
    {
        printf("MEMORY BUG: %s:%i: %p free of pointer %p\n", file, line, ptr, ptr);
        abort();
    }
    
    stats.active_size -= size;
    nodePtr->active = false;
    
    // remove node from active list
    if (nodePtr->prev != NULL)
        nodePtr->prev->next = nodePtr->next;

    if (nodePtr->prev == NULL)
        firstActive = nodePtr->next;
    
    if (nodePtr->next != NULL)
        nodePtr->next->prev = nodePtr->prev;

    insertFreePtr(ptr);
    
    // free all the data of pointer
    ptr -= sizeof(node);
    free(ptr);
}

void* m61_realloc(void* ptr, size_t sz, const char* file, int line)
{
    void* new_ptr = NULL;
    if (sz != 0)
        new_ptr = m61_malloc(sz, file, line);
    if (ptr != NULL && new_ptr != NULL)
    {
        // copy metadata
        unsigned long long* ptr_copy = (unsigned long long*) ptr;
        ptr_copy -= 1;
        size_t old_sz = ptr_copy[0];
        
        // copy data from ptr to new_ptr
        if (old_sz < sz)
            memcpy(new_ptr, ptr, old_sz);           
        else
            memcpy(new_ptr, ptr, sz); 
    }
    // free the old pointer
    m61_free(ptr, file, line);
    return new_ptr;
}

void* m61_calloc(size_t nmemb, size_t sz, const char* file, int line)
{
    if (sz == 0 || nmemb == 0)
    {
        stats.nfail++;
        return NULL;
    }
   
    // if integer overflow happend
    if (((size_t) -1) / sz <= nmemb || ((size_t) -1) / nmemb <= sz)
    {
        stats.nfail++;
        return NULL;
    }
    void* ptr = m61_malloc(nmemb * sz, file, line);
    
    // overwrite with zeros
    if (ptr != NULL)
        memset(ptr, 0, nmemb * sz);
    return ptr;
}

void m61_getstatistics(struct m61_statistics* stat)
{
    stat->nactive 	= stats.nactive;
    stat->active_size 	= stats.active_size;
    stat->ntotal	= stats.ntotal;
    stat->total_size 	= stats.total_size;
    stat->nfail 	= stats.nfail;
    stat->fail_size 	= stats.fail_size;
    stat->heap_min 	= stats.heap_min;
    stat->heap_max 	= stats.heap_max;
}

void m61_printstatistics(void)
{
    printf("malloc count: active %10llu   total %10llu   fail %10llu\n",
           stats.nactive, stats.ntotal, stats.nfail);
    printf("malloc size:  active %10llu   total %10llu   fail %10llu\n",
           stats.active_size, stats.total_size, stats.fail_size);
}

void m61_printleakreport(void)
{
    node* pointer = firstActive;
    while (pointer != NULL)
    {
        if (pointer->active == true)
            printf("LEAK CHECK: %s:%i: allocated object %p with size %zu\n", 
                        pointer->fileName, pointer->lineNumber, 
                        pointer->address, pointer->allocationSize );
        pointer = pointer->next;
    }
}

/* A function to print heaver hitter report. 
   we print line using 0.10 threshold reopnsibility of total bytes. */
void m61_heavyHitterTest()
{
    heavHitterNode* ptr = firstheavHitter;
    while (ptr != NULL)
    {
        // if (file, line) pair allocation size is greater than 0.10 of total
        if ((float) ptr->size / (float) totalAllocatedBytes > .10)
            printf("HEAVY HITTER: %s:%i: %llu bytes, (~%.1f%)\n", 
                ptr->fileName, ptr->lineNumber, ptr->size,
                (float) ptr->size / (float) totalAllocatedBytes * 100 );
        ptr = ptr->next;
    }
}
