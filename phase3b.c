/*
 * phase3b.c
 *
 * James Brechtel and Zach
 * 
 */

#include <assert.h>
#include <phase1.h>
#include <phase2.h>
#include <usloss.h>
#include <string.h>
#include <libuser.h>

#include "phase3.h"
#include "phase3Int.h"

#ifdef DEBUG
int debugging3 = 1;
#else
int debugging3 = 0;
#endif
// just holds frameNum and the next node
typedef struct FreeFrames{
    int frameNum;
    struct FreeFrames *next;
} FreeFrames;
// holds the frame number, page number in the frame and the pid that holds the frame
typedef struct UsedFrames{
    int frameNum;
    int pageNum;
    int pid;
    struct UsedFrames *next;
} UsedFrames;

// dummy head nodes for both structs
FreeFrames *FreeFramesHead;
UsedFrames *UsedFramesHead;

int numPages;
int lockId;

void debug3(char *fmt, ...)
{
    va_list ap;

    if (debugging3) {
        va_start(ap, fmt);
        USLOSS_VConsole(fmt, ap);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * P3FrameInit --
 *
 *  Initializes the frame data structures.
 *
 * Results:
 *   P3_ALREADY_INITIALIZED:    this function has already been called
 *   P1_SUCCESS:                success
 *
 *----------------------------------------------------------------------
 */
int
P3FrameInit(int pages, int frames)
{
    int result = P1_SUCCESS;
    int i;
    int rc;
    FreeFrames *cur;

    // check kernel mode
    // NOTE: USLOSS_PsrGet() if return is 1 kernel, 0 is user
    // NOTE2: returns 8 bits and 0th bit is mode
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) USLOSS_IllegalInstruction();


    // initialize the frame data structures, e.g. the pool of free frames
    // initialize head of the free frames
    FreeFramesHead = (FreeFrames *) malloc(sizeof(FreeFrames));
    // set frame num to -1 for head node
    FreeFramesHead->frameNum = -1;
    FreeFramesHead->next = NULL;
    cur = FreeFramesHead;
    // create frames number of frames and insert them into the list Ascending order (1,2,3,..)
    for(i = 0; i < frames; i++){
        cur->next = (FreeFrames *) malloc(sizeof(FreeFrames));
        cur->next->frameNum = i;
        cur = cur->next;
    }

    //initialize head of the used frames
    UsedFramesHead = (UsedFrames *) malloc(sizeof(UsedFrames));
    UsedFramesHead->frameNum = -1;
    UsedFramesHead->pageNum = -1;
    UsedFramesHead->next = NULL;
    
    // set numPages
    numPages = pages;
    // creates lock
    rc = P1_LockCreate("lock", &lockId);
    assert(rc == P1_SUCCESS);

    // set P3_vmStats.freeFrames
    P3_vmStats.freeFrames = frames;

    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * P3FrameFreeAll --
 *
 *  Frees all frames used by a process
 *
 * Results:
 *   P3_NOT_INITIALIZED:    P3FrameInit has not been called
 *   P1_INVALID_PID:        pid is invalid
 *   P1_SUCCESS:            success
 *
 *----------------------------------------------------------------------
 */

int
P3FrameFreeAll(int pid)
{
    int result = P1_SUCCESS;
    int rc;
    UsedFrames *cur;
    UsedFrames *prev;
    UsedFrames *temp;
    FreeFrames *new;
    // free all frames in use by the process (P3PageTableGet)
    rc = P1_Lock(lockId);
    assert(rc == P1_SUCCESS);
    cur = UsedFramesHead->next;
    prev = UsedFramesHead;
    while(cur != NULL){
        // if the current node is controlled by pid
        if(cur->pid == pid){
            // cut cur node out of list
            prev->next = cur->next;
            // create new free frame
            new = (FreeFrames *) malloc(sizeof(FreeFrames));
            // set free frame number to the released frame
            new->frameNum = cur->frameNum;
            // free used frame memory
            temp = cur;
            free(temp);
            // sets current to the next node
            cur = prev->next;
            P3_vmStats.freeFrames++;
        }
        else{
            // Normal linked list traversal
            prev = cur;
            cur = cur->next;
        }
    }
    rc = P1_Unlock(lockId);
    assert(rc == P1_SUCCESS);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * P3PageFaultResolve --
 *
 *  Frees all frames used by a process
 *
 * Results:
 *   P3_NOT_INITIALIZED:    P3FrameInit has not been 
 *   P3_NOT_IMPLEMENTED:    this function has not been implemented
 *   P1_INVALID_PID:        pid is invalid
 *   P1_INVALID_PAGE:       page is invalid
 *   P3_OUT_OF_SWAP:        there is no more swap space
 *   P1_SUCCESS:            success
 *
 *----------------------------------------------------------------------
 */
int 
P3PageFaultResolve(int pid, int page, int *frame) 
{
    /*******************
    if there is a free frame
        frame = a free frame
    else
        rc = P3SwapOut(&frame)
        if rc == P3_OUT_OF_SWAP
            return rc
    rc = P3SwapIn(pid, page, frame)
    if rc == P3_PAGE_NOT_FOUND
        fill frame with zeros
    return P1_SUCCESS
    *******************/
    int rc;
    FreeFrames *freeTemp;
    UsedFrames *usedTemp;
    void *vmRegion;
    void *pmAddr;
    int pageSize, numFrames, mode;
    if(FreeFramesHead->next != NULL){
         rc = P1_Lock(lockId);
         assert(rc == P1_SUCCESS);
        // sets return to frame num
        freeTemp = FreeFramesHead->next;
        *frame = freeTemp->frameNum;
        printf("frame = %d\n",*frame);
        FreeFramesHead->next = freeTemp->next;
        // free the memory of the free Node to be assigned to used
        free(freeTemp);
        // reduce free frames
        P3_vmStats.freeFrames--;
        // allocate space for used frame;
        usedTemp = (UsedFrames *) malloc(sizeof(UsedFrames));
        // add tempUsed to used frames (head of list)
        usedTemp->next = UsedFramesHead->next;
        usedTemp->pageNum = page;
        usedTemp->frameNum = *frame;
        usedTemp->pid = pid;
        UsedFramesHead->next = usedTemp;
        rc = P1_Unlock(lockId);
        assert(rc == P1_SUCCESS);
    }
    else{
        rc = P3SwapOut(frame);
        if(rc == P3_OUT_OF_SWAP){
            return rc;
        }
    }
    rc = P3SwapIn(pid, page, *frame);
    if(rc == P3_PAGE_NOT_FOUND){
        rc = USLOSS_MmuGetConfig(&vmRegion, &pmAddr, &pageSize, &numPages, &numFrames, &mode);
        assert(rc == USLOSS_MMU_OK);
        // sets contents of frame to 0
        printf("memset\n");
        memset(pmAddr + (pageSize * (*frame)), 0, pageSize);
    }
    return P1_SUCCESS;
}

