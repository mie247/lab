/*
 * mm-implicit.c
 * 
 * code is based on CSAPP 3e textbook section 9.9
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

/* Basic constants and macros */
#define WSIZE 4             /* Word and header/footer size (bytes) */
#define DSIZE 8             /* Double word size (bytes) */
#define CHUNKSIZE (1 << 12) /* Extend heap by this amount (bytes) */

#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))
#define PACK_ALL(size,prev_alloc,alloc) ((size)|(prev_alloc)|(alloc))

/* Read and write a word at address p */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp)-WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE(((char *)(bp)-DSIZE)))

static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);

static inline size_t adjust_alloc_size(size_t size);
static inline size_t get_index(size_t size);
static inline void get_range(size_t index);
static size_t low_range;
static size_t high_range;

static inline void insert_node(void* bp);
static inline void delete_node(void* bp);

static char *heap_listp;//指向序言块的第二块

#define FREE_LIST_NUM 16
#define GET(p) (*(unsigned int *)(p))
#define GET_HEAD(num) ((unsigned int *)(long)(GET(heap_listp + WSIZE * num)))
#define GET_PRE(bp) ((unsigned int *)(long)(GET(bp)))
#define GET_SUC(bp) ((unsigned int *)(long)(GET((unsigned int *)bp + 1)))

/*
 * mm_init
 */
int mm_init(void) {
// Your code here
    if((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1){
        return -1;
    }
    PUT(heap_listp,0);
    PUT(heap_listp+(1*WSIZE),PACK(DSIZE,1));
    PUT(heap_listp+(2*WSIZE),PACK(DSIZE,1));
    PUT(heap_listp+(3*WSIZE),PACK(0,1));
    heap_listp += (2*WSIZE);

    // if((heap_list=mem_sbrk((4+FREE_LIST_NUM)*WSIZE))==(void*)-1){
    //     return -1;  
    // }
    // int i=0;
    // while(i<FREE_LIST_NUM){
    //     // free_lists[i]=mem_heap_lo();
    //     PUT(heap_list + i*WSIZE, NULL);
    //     i++;
    // }
    // PUT(heap_list+FREE_LIST_NUM*WSIZE,0);//空字，用于对齐序言快头部
    // PUT(heap_list+((1+FREE_LIST_NUM)*WSIZE),PACK(DSIZE,1));//序言快头部
    // PUT(heap_list+((2+FREE_LIST_NUM)*WSIZE),PACK(DSIZE,1));//序言快尾部
    // PUT(heap_list+((3+FREE_LIST_NUM)*WSIZE),PACK(0,1));//结尾块头部 
    //heap_list+=((2+FREE_LIST_NUM)*WSIZE);

    if(extend_heap(CHUNKSIZE/WSIZE)==NULL){
        return -1;
    }
    return 0;
}

/*
 * malloc
 */
void *malloc(size_t size) {
// Your code here
    size_t asize;
    size_t extendsize;
    char *bp;
    if(size == 0){
        return NULL;
    }
    //优化：略小于2幂次的size向上调整为2的幂次
    size=adjust_alloc_size(size);

    if(size <= DSIZE){
        asize = 2*DSIZE;
    }else{
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
    }
    //寻找合适的空闲块
    if((bp = find_fit(asize)) != NULL){
        place(bp, asize);
        return bp;
    }
    //找不到则扩展堆
    extendsize = MAX(asize, CHUNKSIZE);
    if((bp = extend_heap(extendsize/WSIZE)) == NULL){
        return NULL;
    }
    place(bp, asize);
    return bp;
}

/*
 * free
 */
void free(void *ptr) {
// Your code here
    if(ptr==0){
        return;
    }
    size_t size = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);
}

/*
 * realloc
 */
void *realloc(void *oldptr, size_t size) {
    size_t oldsize, asize;
    void *newptr, *bp;

    if (size == 0) {
        free(oldptr);
        return NULL;
    }

    if (oldptr == NULL) {
        return malloc(size);
    }

    oldsize = GET_SIZE(HDRP(oldptr));
    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    if (oldsize >= asize) {
        // free the remaining bytes in this block
        if ((oldsize - asize) >= (2 * DSIZE)) {
            PUT(HDRP(oldptr), PACK(asize, 1));
            PUT(FTRP(oldptr), PACK(asize, 1));
            bp = NEXT_BLKP(oldptr);
            PUT(HDRP(bp), PACK(oldsize - asize, 0));
            PUT(FTRP(bp), PACK(oldsize - asize, 0));
            // need to coalesce this free block with next block if possible
            coalesce(bp);
        }
        return oldptr;
    } else {
        // need to allocate large block
        if ((newptr = malloc(size)) == NULL)
            return NULL;

        memcpy(newptr, oldptr, oldsize - 2 * WSIZE);
        free(oldptr);

        return newptr;
    }
}

/*
 * calloc
 */
void *calloc(size_t nmemb, size_t size) {
    size_t bytes = nmemb * size;
    void *newptr;

    if ((newptr = malloc(bytes)) != NULL)
        memset(newptr, 0, bytes);
    return newptr;
}

/*
 * mm_checkheap
 */
bool mm_checkheap(int lineno) {
    return (bool)lineno;
}

/* 
 * extend_heap
 * Extend the heap by `words` words. Return a pointer to the new free block on
 * success. Otherwise return NULL.
*/
// HINT: Make sure the heap size is properly aligned. Don't forget to coalesce
// free blocks.
static void *extend_heap(size_t words) {
// Your code here
    size_t size;
    char *bp;

    size=(words+1)/2*DSIZE;//双字对齐 
    if((long)(bp=mem_sbrk(size))==-1){
        return NULL;
    }
    //初始化新空闲块
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    //初始化新结尾快
    PUT(HDRP(NEXT_BLKP(bp)),PACK(0,1));

    return coalesce(bp);
}

/*
 * coalesce
 * Merge two adjacent free memory chunks, return the merged block.
*/
static void *coalesce(void *bp) {

    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    /* Case 1 */
    if (prev_alloc && next_alloc) {
        // insert_node(bp);
        return bp;
    }
    // Your code here: case 2, 3 and 4
    else if(prev_alloc && !next_alloc){
        // delete_node(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    else if(!prev_alloc && next_alloc){
        // delete_node(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    else{
        // delete_node(NEXT_BLKP(bp));
        // delete_node(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    // insert_node(bp);
    return bp;
}

/* First-fit search */
// Return the first fit block, if not find, return NULL
static void *find_fit(size_t asize) {
// Your code here
    void *bp;
    for(bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)){
        if((GET_SIZE(HDRP(bp)) >= asize) && (!GET_ALLOC(HDRP(bp)))){
            return bp;
        }
    }
    return NULL;
}
// void *find_fit(size_t asize)
// {
//     int num = get_index(asize);
//     unsigned int* bp;
//     while(num < FREE_LIST_NUM) {
//         bp = GET_HEAD(num);
//         while(bp) {
//             if(GET_SIZE(HDRP(bp)) >= asize){
//                 return (void *)bp;
//             }
//             bp = GET_SUC(bp);
//         }
//         num++;
//     }
//     return NULL;
// }

// Place the block
static void place(void *bp, size_t asize) {
// Your code here
    size_t csize = GET_SIZE(HDRP(bp));

    // delete_node(bp);

    if((csize - asize) >= 2*DSIZE) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));

        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));

        // insert_node(bp);
    }
    else{
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

static inline size_t adjust_alloc_size(size_t size){
    if(size>120&&size<128)size=128;
    if(size>240&&size<256)size=256;
    if(size>448&&size<512)size=512;
    if(size>1000&&size<1024)size=1024;
    if(size>2000&&size<2048)size=2048;
    return size;
}

static inline size_t get_index(size_t size) {
    int i=4;
    for(;i<=FREE_LIST_NUM+2;i++){
        if(size <= (size_t)(1<<i)){
            return i-4;
        }
    }
    return i-4;
}

static inline void get_range(size_t index) {
    int k=index+4;
    low_range=1<<(k-1);
    high_range=1<<k;
    if(index==FREE_LIST_NUM-1){
        high_range=0x7fffffff;
    }
}

static inline void insert_node(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    int num = get_index(size);
    if(GET_HEAD(num) == NULL){
        PUT(heap_listp + WSIZE * num, bp);
        PUT(bp, NULL);
        PUT((unsigned int *)bp + 1, NULL);
    } else {
        PUT((unsigned int *)bp + 1, GET_HEAD(num));
        PUT(GET_HEAD(num), bp);
        PUT(bp, NULL);
        PUT(heap_listp + WSIZE * num, bp);
    }
}

static inline void delete_node(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    int num = get_index(size);
    if (GET_PRE(bp) == NULL && GET_SUC(bp) == NULL) { 
        PUT(heap_listp + WSIZE * num, NULL);
    } 
    else if (GET_PRE(bp) != NULL && GET_SUC(bp) == NULL) {
        PUT(GET_PRE(bp) + 1, NULL);
    } 
    else if (GET_SUC(bp) != NULL && GET_PRE(bp) == NULL){
        PUT(heap_listp + WSIZE * num, GET_SUC(bp));
        PUT(GET_SUC(bp), NULL);
    }
    else if (GET_SUC(bp) != NULL && GET_PRE(bp) != NULL) {
        PUT(GET_PRE(bp) + 1, GET_SUC(bp));
        PUT(GET_SUC(bp), GET_PRE(bp));
    }
}
