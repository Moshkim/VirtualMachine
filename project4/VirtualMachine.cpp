//KWANIL KIM
//QIANHUI FAN

#include "VirtualMachine.h"
#include "Machine.h"
#include <unistd.h>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <vector>
#include <sys/msg.h>
#include <termios.h>
#include <sys/types.h>
#include <inttypes.h>
#include <string.h>
#include <iostream>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <sys/stat.h>

using namespace std;

extern "C" {
    
    TVMMainEntry VMLoadModule(const char *module);
    void VMUnloadModule(void);
    void *MachineInitialize(size_t);
    void MachineEnableSignals(void);
    void Scheduler();
    void Skeleton(void *param);
    const TVMMemoryPoolID VM_MEMORY_POOL_ID_SYSTEM = 0;
    char current_path[100] = "/";
    
    
    TVMStatus VMMemoryPoolAllocate(TVMMemoryPoolID memory, TVMMemorySize size, void **pointer);
    
    class Mutex;
    class BPB;
    
    class memBlock{
    public:
        uint8_t *base;
        TVMMemorySize blockSize;
        
        memBlock(uint8_t *Base, TVMMemorySize BlockSize){
            base = Base;
            blockSize = BlockSize;
        }
    };
    
    class memPool{
    public:
        uint8_t* base;
        TVMMemoryPoolID MemID;
        TVMMemorySize MemSize;
        vector<memBlock*> alloc;//allocated
        vector<memBlock*> dealloc;//deallocated
        
        memPool(TVMMemoryPoolID id, TVMMemorySize size, uint8_t* Base){
            base = Base;
            MemID = id;
            MemSize = size;
        }
        //structure for keep tracking allocated memory blocks and deallocated ones
    };
    
    
    
    class Thread{
    public:
        TVMThreadID id;
        TVMThreadState state;
        TVMThreadPriority priority;
        TVMThreadEntry entry;
        TVMMemorySize memsize;
        TVMTick sleeptick;
        void *param;
        SMachineContext MC;
        char *stack_addr;
        int *result_Return;
        vector<Mutex*> hold;
        
        Thread(TVMThreadEntry Entry, void *Param, TVMMemorySize Memsize, TVMThreadPriority Priority, TVMThreadID ID, TVMThreadState State){
            id = ID;
            state = State;
            priority = Priority;
            entry = Entry;
            param = Param;
            memsize = Memsize;
            VMMemoryPoolAllocate(VM_MEMORY_POOL_ID_SYSTEM, Memsize, (void**)&stack_addr);
        }
        
    };
    
    TVMMutexID Mutex_Count;
    Thread *idle_thread;
    Thread *curThread;
    TVMThreadID threadID;
    
    TVMMemoryPoolID memoryPoolID = 1;
    
    vector<memPool*> memBucket;
    
    class Mutex{
    public:
        TVMMutexID Mutex_id;
        TVMThreadID Boss;
        unsigned int wait;
        vector<Thread*> high_wait;
        vector<Thread*> medium_wait;
        vector<Thread*> low_wait;
        
        Mutex(TVMMutexID ID,unsigned int Lock, TVMThreadID BOSS){
            Mutex_id = ID;
            Boss = BOSS;
        }
    };
    
    class BPB{
    public:
        
        char BS_OEMName[8];
        uint16_t BPB_BytsPerSec;
        uint8_t BPB_SecPerClus;
        uint16_t BPB_RsvdSecCnt;
        uint8_t BPB_NumFATs;
        uint16_t BPB_RootEntCnt;
        uint16_t BPB_TotSec16;
        uint8_t BPB_Media;
        uint16_t BPB_FATSz16;
        uint16_t BPB_SecPerTrk;
        uint16_t BPB_NumHeads;
        uint32_t BPB_HiddSec;
        uint32_t BPB_TotSec32;
        //uint16_t BPB_BytsPerSec = *(uint16_t *)(sector + 3);
    };
    
    class Root_entry{
    public:
        int descriptor = -1;
        int fileFlag;
        int fileMode;
        uint16_t firstbyte;
        SVMDirectoryEntry Entries;
        vector<SVMDirectoryEntry*> subDirectory;
    };
    
    class FAT{
    public:
        unsigned int fatSize;
    };
    
    BPB* bpb = new BPB;
    FAT* fat = new FAT;
    unsigned int FirstRootSector;
    unsigned int RootDirectorySectors;
    unsigned int FirstDataSector;
    unsigned int ClusterCount;
    unsigned int rootSize;
    int assignfiledescriptor = 3;
    vector<Root_entry*> allEntries;
    vector<Root_entry*> openedEntries;
    vector<Root_entry*> allFileEntries;
    char* fatArray;
    char* rootArray;
    
    
    void rootEntry(char* BasePtr){
        uint16_t DIR_CrtTime;
        uint16_t DIR_CrtDate;
        uint16_t DIR_lstAccDate;
        uint16_t DIR_WrtTime;
        uint16_t DIR_WrtDate;
        char firstbyte;
        
        for(unsigned int i = 0; i < bpb->BPB_RootEntCnt ; i++){
            //cout << "value of i "<<i<<endl;
            memcpy(&firstbyte,BasePtr+i*32,1);
            //if((int)BasePtr[0+i*32] != 0){
            
            if((int)firstbyte != 0 && (int)firstbyte != 229 && (int)firstbyte != 32){
                //cout <<"first byte "<< (int)firstbyte <<endl;
                Root_entry* new_root = new Root_entry();
                SVMDirectoryEntry* new_entry = new SVMDirectoryEntry();
                SVMDateTime* crtSVM = new SVMDateTime();
                SVMDateTime* lastSVM = new SVMDateTime();
                SVMDateTime* wrtSVM = new SVMDateTime();
                int j,z;
                if(BasePtr[i*32+11] != 15){
                    for(j=0; j<8;j++){
                        if((int)BasePtr[j+i*32] != 32){
                            memcpy(&new_entry->DShortFileName[j],&BasePtr[j+i*32],1);
                        }
                        else{
                            break;
                        }
                    }
                    
                    if((unsigned int)BasePtr[8+i*32] != 32){//checking spaces if spaces are 32 then no extention
                        new_entry->DShortFileName[j] = '.';
                    }
                    
                    for(z = 0; z < 3; z++){
                        if((int)BasePtr[8+i*32+z] != 32){
                            memcpy(&new_entry->DShortFileName[j+1+z],&BasePtr[8+i*32+z],1);
                        }else{
                            break;
                        }
                        //memcpy(new_entry->DShortFileName[i],BasePtr+i*32,8);
                    }
                    new_entry->DShortFileName[j+1+z] = '\0';
                    
                    //cout<<"short name "<<new_entry->DShortFileName<<endl;
                    new_entry->DAttributes = BasePtr[i*32+11]; //0 means it's file
                    //cout << "attributes "<<(int)new_entry->DAttributes <<endl;
                    
                    
                    memcpy(&DIR_CrtTime,BasePtr+14+i*32,2);
                    memcpy(&DIR_CrtDate,BasePtr+16+i*32,2);
                    crtSVM->DSecond = DIR_CrtTime & 0x1F;
                    crtSVM->DMinute = (DIR_CrtTime & 0x7E0)>>5;
                    crtSVM->DHour = (DIR_CrtTime & 0xF800)>>11;
                    crtSVM->DDay = DIR_CrtDate & 0x1F;
                    crtSVM->DMonth = (DIR_CrtDate & 0x1E0) >>5;
                    crtSVM->DYear = ((DIR_CrtDate & 0xFE00)>>9);
                    crtSVM->DYear = crtSVM->DYear + 0x7BC;
                    memcpy(&crtSVM->DHundredth,BasePtr+13+i*32,1);
                    /*cout << "second "<< (DIR_CrtTime & 0x1F) <<endl;
                    cout << "minute "<< ((DIR_CrtTime & 0x7E0)>>5) <<endl;
                    cout << "hour "<< ((DIR_CrtTime & 0xF800)>>11) <<endl;
                    cout << "day "<< (DIR_CrtDate & 0x1F) <<endl;
                    cout << "month "<< ((DIR_CrtDate & 0x1E0) >>5) <<endl;
                    cout << "year "<< ((DIR_CrtDate & 0xFE00)>>9)+1980 <<endl;*/
                    
                    memcpy(&DIR_lstAccDate,BasePtr+18+i*32,2);
                    lastSVM->DDay = DIR_lstAccDate & 0x1F;
                    lastSVM->DMonth = (DIR_lstAccDate & 0x1E0) >>5;
                    lastSVM->DYear = (((DIR_lstAccDate & 0xFE00)>>9)+198);
                    lastSVM->DYear = lastSVM->DYear + 0x7BC;
                    lastSVM->DHundredth = 0;
                    
                    /*cout << "day "<< (DIR_lstAccDate & 0x1F) <<endl;
                    cout << "month "<< ((DIR_lstAccDate & 0x1E0) >>5) <<endl;
                    cout << "year "<< ((DIR_lstAccDate & 0xFE00)>>9)+1980 <<endl;*/
                    
                    memcpy(&DIR_WrtTime,BasePtr+22+i*32,2);
                    memcpy(&DIR_WrtDate,BasePtr+24+i*32,2);
                    wrtSVM->DSecond = DIR_WrtTime & 0x1F;
                    wrtSVM->DMinute = (DIR_WrtTime & 0x7E0)>>5;
                    wrtSVM->DHour = (DIR_WrtTime & 0xF800)>>11;
                    wrtSVM->DDay = DIR_WrtDate & 0x1F;
                    wrtSVM->DMonth = (DIR_WrtDate & 0x1E0) >>5;
                    wrtSVM->DYear = ((DIR_WrtDate & 0xFE00)>>9);
                    wrtSVM->DYear = wrtSVM->DYear + 0x7BC;
                    wrtSVM->DHundredth = 0;
                    /*cout << "second "<< (DIR_WrtTime & 0x1F) <<endl;
                    cout << "minute "<< ((DIR_WrtTime & 0x7E0)>>5) <<endl;
                    cout << "hour "<< ((DIR_WrtTime & 0xF800)>>11) <<endl;*/
                    
                    memcpy(&new_root->firstbyte,BasePtr+26+i*32,4);
                    //cout << "first byte "<<(int)new_root->firstbyte <<endl;
                    
                    memcpy(&new_entry->DSize,BasePtr+28+i*32,4);
                    //cout << "size "<<(int)new_entry->DSize <<endl;
                    
                    new_entry->DCreate = *crtSVM;
                    new_entry->DAccess = *lastSVM;
                    new_entry->DModify = *wrtSVM;
                    new_root->Entries = *new_entry;
                    
                    
                    //new_root->descriptor = assignfiledescriptor;
                    //assignfiledescriptor++;
                    if((int)new_entry->DAttributes == 0){
                        
                        allFileEntries.push_back(new_root);
                        
                    }
                    
                    allEntries.push_back(new_root);
                }else{
                }
            }//if first byte is valid
        }//end of for
    }
    /*
     typedef struct{
     unsigned int DYear;
     unsigned char DMonth;
     unsigned char DDay;
     unsigned char DHour;
     unsigned char DMinute;
     unsigned char DSecond;
     unsigned char DHundredth;
     } SVMDateTime, *SVMDateTimeRef;
     
     typedef struct{
     char DLongFileName[VM_FILE_SYSTEM_MAX_PATH];
     char DShortFileName[VM_FILE_SYSTEM_SFN_SIZE];
     unsigned int DSize;
     unsigned char DAttributes;
     SVMDateTime DCreate;
     SVMDateTime DAccess;
     SVMDateTime DModify;
     } SVMDirectoryEntry, *SVMDirectoryEntryRef;*/
    
    
    void parseBPB(char* BasePtr){
        memcpy(bpb->BS_OEMName,BasePtr+3,8);
        bpb->BPB_BytsPerSec = BasePtr[11] + (BasePtr[12] << 8);
        bpb->BPB_SecPerClus = BasePtr[13];
        bpb->BPB_RsvdSecCnt = BasePtr[14] + (BasePtr[15] << 8);
        bpb->BPB_NumFATs = BasePtr[16];
        bpb->BPB_RootEntCnt = BasePtr[17] + (BasePtr[18] << 8);
        bpb->BPB_TotSec16 = BasePtr[19] + (BasePtr[20] << 8);
        bpb->BPB_Media = BasePtr[21];
        bpb->BPB_FATSz16 = BasePtr[22] + (BasePtr[23] << 8);
        bpb->BPB_SecPerTrk = BasePtr[24] + (BasePtr[25] << 8);
        bpb->BPB_NumHeads = BasePtr[26] + (BasePtr[27] << 8);
        bpb->BPB_HiddSec = BasePtr[28] + (BasePtr[29] << 8) + (BasePtr[30] << 16) + (BasePtr[31] << 24);
        bpb->BPB_TotSec32 = BasePtr[32] + (BasePtr[33] << 8) + (BasePtr[34] << 16) + (BasePtr[35] << 24);;
        FirstRootSector = bpb->BPB_RsvdSecCnt + bpb->BPB_NumFATs * bpb->BPB_FATSz16;
        RootDirectorySectors = (bpb->BPB_RootEntCnt * 32) / 512;
        FirstDataSector = FirstRootSector + RootDirectorySectors;
        ClusterCount = (bpb->BPB_TotSec32 - FirstDataSector) / bpb->BPB_SecPerClus;
        fat->fatSize = bpb->BPB_FATSz16 * bpb->BPB_BytsPerSec;
        rootSize = bpb->BPB_RootEntCnt * (unsigned int)32;
        
        /*cout << "BPB_BS_OEMName " << bpb->BS_OEMName<<endl;
        cout << "BPB_BytsPerSec " << bpb->BPB_BytsPerSec<<endl;
        cout << "BPB_SecPerClus " << (int)bpb->BPB_SecPerClus<<endl;
        cout << "BPB_RsvdSecCnt " << bpb->BPB_RsvdSecCnt<<endl;
        cout << "BPB_NumFATs " << (int)bpb->BPB_NumFATs<<endl;
        cout << "BPB_RootEntCnt " << bpb->BPB_RootEntCnt<<endl;
        cout << "BPB_TotSec16 " << bpb->BPB_TotSec16<<endl;
        cout << "BPB_Media " <<(int)bpb->BPB_Media<<endl;
        cout << "BPB_FATSz16 " << bpb->BPB_FATSz16<<endl;
        cout << "BPB_TotSec32 " << bpb->BPB_TotSec32<<endl;
        cout << "FirstRootSector " << FirstRootSector << endl;
        cout << "RootDirectorySectors " << RootDirectorySectors <<endl;
        cout << "FirstDataSector " << FirstDataSector << endl;
        cout << "ClusterCount " << ClusterCount << endl;
        cout << "fatSize" << fat->fatSize << endl;
        cout << "rootSize" << rootSize << endl;*/
        
    }
    
    /*****************ticks multi thread sleep count********************/
    //VMTickMS(int *ticks);
    
    /*****************single thread sleep count********************/
    volatile int sleepCount;
    volatile int flag = 0;
    volatile unsigned int tickCount;
    volatile unsigned int tickMS;
    
    /*****************number of threads********************/
    int threadCount = 0;
    //void MachineRequestAlarm(useconds_t usec, TMachineAlarmCallback callback, void *calldata);
    
    
    
    /********************************Threads***************************************/
    vector<Mutex*> Mutex_Q;
    vector<Thread*> threads_Q;
    vector<Thread*> ready_Q;
    vector<Thread*> sleeping_Q;
    /******************************************************************************/
    
    void callBack(void* calldata){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        //cout << "callBack"<<endl;
        tickCount++;
        
        for(unsigned int i = 0; i < sleeping_Q.size(); i++){
            if(sleeping_Q[i] -> sleeptick != (unsigned int)-2){
                sleeping_Q[i] -> sleeptick--;
            }
            if(sleeping_Q[i] -> sleeptick == 0){
                sleeping_Q[i] -> state = VM_THREAD_STATE_READY;
                
                ready_Q.push_back(sleeping_Q[i]);
                sleeping_Q.erase(sleeping_Q.begin() + i);
            }
        }//end of for
        if(ready_Q.size()!= 0){
            Scheduler();
        }
        MachineResumeSignals(&sigToSave);
    }
    
    void fileCallBack(void *calldata, int result){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        
        tickCount++;
        ((Thread*)calldata) -> state = VM_THREAD_STATE_READY;
        ready_Q.push_back((Thread*)calldata);
        ((Thread*)calldata) -> result_Return = &result;
        
        if(curThread->priority == VM_THREAD_PRIORITY_NORMAL){
            for(unsigned int i =0; i< ready_Q.size(); i++){
                if(ready_Q[i]->priority == VM_THREAD_PRIORITY_HIGH){
                    Scheduler();
                    break;
                }//If any higher priority found
            }
        }else if(curThread->priority == VM_THREAD_PRIORITY_LOW){
            for(unsigned int i =0; i< ready_Q.size(); i++){
                if(ready_Q[i]->priority == VM_THREAD_PRIORITY_HIGH || ready_Q[i]->priority == VM_THREAD_PRIORITY_NORMAL){
                    Scheduler();
                    break;
                }//If any higher priority found
            }
        }
        
        if(((Thread*)calldata)->priority <= curThread->priority)
            Scheduler();
        
        MachineResumeSignals(&sigToSave);
    }
    
    
    void idle_func( void *idle){
        MachineEnableSignals();
        while(true);
    }
    
    void Scheduler(){
        Thread* newThread = NULL;
        Thread* tempThread = NULL;
        
        if(ready_Q.size() == 0){
            if(curThread != idle_thread){
                newThread = idle_thread;
            }
        }
        else if(ready_Q.size() != 0) {
            int count = 0;
            
            for(unsigned int i = 0; i < ready_Q.size(); i++){
                if(ready_Q[i]->priority == VM_THREAD_PRIORITY_HIGH){
                    newThread = ready_Q[i];
                    ready_Q.erase(ready_Q.begin() + i);
                    count++;
                    break;
                }
            }// end of for
            //look for normal priority in the ready_Q to make it running
            
            if(count == 0) {
                for(unsigned int i = 0; i < ready_Q.size(); i++){
                    if(ready_Q[i]->priority == VM_THREAD_PRIORITY_NORMAL){
                        newThread = ready_Q[i];
                        ready_Q.erase(ready_Q.begin() + i);
                        count++;
                        break;
                    }
                }//end of for
            }
            if(count == 0) {
                for(unsigned int i = 0; i < ready_Q.size(); i++){
                    if(ready_Q[i]->priority == VM_THREAD_PRIORITY_LOW){
                        newThread = ready_Q[i];
                        ready_Q.erase(ready_Q.begin() + i);
                        count++;
                        break;
                    }
                }//end of for
            }
        }
        
        if(curThread -> state == VM_THREAD_STATE_RUNNING) {
            if(curThread != idle_thread){
                curThread -> state = VM_THREAD_STATE_READY;
                ready_Q.push_back(curThread);
            }
        }
        else if(curThread -> state == VM_THREAD_STATE_WAITING && curThread -> sleeptick != 0){
            sleeping_Q.push_back(curThread);
        }
        
        newThread -> state = VM_THREAD_STATE_RUNNING;
        
        tempThread = curThread;
        curThread = newThread;
        MachineContextSwitch(&(tempThread -> MC), &(curThread -> MC));
    }
    
    
    
    void readSector(int filedescriptor, char* data, int offset, int length){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        void* ptr = new void*;
        int length_transfer = 0;
        int value_store = 0;
        int newoffset = 0;
        VMMemoryPoolAllocate((unsigned int)1, 512, &ptr);
        
        curThread -> state = VM_THREAD_STATE_WAITING;
        MachineFileSeek(filedescriptor, offset, SEEK_SET, fileCallBack, curThread);
        Scheduler();
        
        while(length != 0){
            if(length > 512){
                length -= 512;
                length_transfer = 512;
            }else{
                length_transfer = length;
                length = 0;
            }
            
            curThread -> state = VM_THREAD_STATE_WAITING;
            MachineFileRead(filedescriptor, (void*)ptr, length_transfer, fileCallBack, curThread);
            Scheduler();
            value_store += *(curThread -> result_Return);
            memcpy((char*)data + newoffset, ptr, length_transfer);
            newoffset += length_transfer;
        }
        length += value_store;
        //cout << "length trasfered "<< length <<endl;
        VMMemoryPoolDeallocate((unsigned int)1, (uint8_t*)ptr);
        MachineResumeSignals(&sigToSave);
    }
    
    TVMStatus VMStart(int tickms, TVMMemorySize heapsize, TVMMemorySize sharedsize, const char *mount, int argc, char *argv[]) {
        MachineEnableSignals();
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        
        void* shareAddress;
        int filedescriptor;
        char* bpbArray = new char[512];
        
        TVMMainEntry Begin = VMLoadModule(argv[0]);
        int tickmsTosec = tickms * 1000;
        tickMS = tickms;
        
        if(Begin == NULL){
            return VM_STATUS_FAILURE;
        }
        else{
            shareAddress = MachineInitialize(sharedsize);
            if(!shareAddress){
                exit(1);
            }
            
            //MachineEnableSignals();
            MachineRequestAlarm(tickmsTosec,callBack,NULL);
            
            
            uint8_t* base = new uint8_t[heapsize];
            memPool *system_memory = new memPool(VM_MEMORY_POOL_ID_SYSTEM, heapsize, base);
            memPool *shared_memory = new memPool(memoryPoolID, sharedsize, (uint8_t*) shareAddress);
            memoryPoolID++;
            memBucket.push_back(system_memory);
            memBucket.push_back(shared_memory);
            
            Thread *thread_first= new Thread(NULL,NULL,0,VM_THREAD_PRIORITY_NORMAL,(unsigned int)1,VM_THREAD_STATE_DEAD);
            threadCount++;
            threadID = 1;
            thread_first->state = VM_THREAD_STATE_RUNNING;
            curThread = thread_first;
            idle_thread = new Thread(NULL, NULL, 0x100000, (TVMThreadPriority)0x00, (unsigned int)0, VM_THREAD_STATE_READY);
            MachineContextCreate(&(idle_thread->MC), idle_func, NULL, idle_thread->stack_addr, idle_thread->memsize);
            
            /*This is open the file from FAT image system*/
            curThread->state =  VM_THREAD_STATE_WAITING;
            MachineFileOpen(mount,O_RDWR,0600,fileCallBack,curThread); //read from mounted file
            Scheduler();
            filedescriptor = *(curThread->result_Return);
            //cout <<"filedescriptor ****************************************************"<<filedescriptor <<endl;
            /*This is read the file from FAT image system*/
            readSector(filedescriptor,bpbArray, 0,512);
            
            parseBPB(bpbArray);
            fatArray = new char[fat->fatSize];
            
            rootArray = new char[rootSize];
            ///cout <<"root size"<<rootSize<<endl;
            readSector(filedescriptor,fatArray, 512, fat->fatSize);
            
            uint16_t fat_begin = fatArray[0];// + ((uint16_t)fatArray[1]<<8);
            //cout << "fat_begin " << (unsigned int)fat_begin <<endl;
            uint16_t fat2 = fatArray[2];// + ((uint16_t)fatArray[3]<<8);
            //cout << "fat2 " << (int)fat2 <<endl;
            readSector(filedescriptor,rootArray, 17920,rootSize);
            
            rootEntry(rootArray);
            VMFileClose(filedescriptor);
            (*Begin)(argc, argv);
            //cout << "start done";
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_SUCCESS;
        }
    }
    
    /*Opens a directory for reading in the mounted FAT file system.*/
    
    TVMStatus VMDirectoryOpen(const char *dirname, int *dirdescriptor){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        if(dirname == NULL || dirdescriptor == NULL){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        
        if(strcmp(dirname,"/") == 0 || strcmp(dirname,"./") == 0 || strcmp(dirname,".") == 0){ //use string compare
            //SVMDirectoryEntry *openEnt = new SVMDirectoryEntry();
            
            *dirdescriptor = 0;
        }else{
            
        }
        
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
    }
    
    
    TVMStatus VMDirectoryClose(int dirdescriptor){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        
        
        
        
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
    }
    
    TVMStatus VMDirectoryCurrent(char *abspath){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        
        if(abspath == NULL){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        
        const char* s = current_path;
        //abspath = current_path;
        strcpy(abspath,s);
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
    }
    
    int Entryposition = 0;
    
    
    TVMStatus VMDirectoryRead(int dirdescriptor, SVMDirectoryEntryRef dirent){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        
        if(dirent == NULL){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        if(dirdescriptor ==0){
            
            if(allEntries[Entryposition] == NULL){
                Entryposition = 0;
                MachineResumeSignals(&sigToSave);
                return VM_STATUS_FAILURE;
            }
            *dirent = allEntries[Entryposition]->Entries;
            
            Entryposition++;
        }
        
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
    }
    
    TVMStatus VMDirectoryRewind(int dirdescriptor){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        
        
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
    }
    
    TVMStatus VMDirectoryChange(const char*path){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        if(strcmp(current_path,"/")==0 && strcmp(path,"..")==0){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_FAILURE;
        }
        int found = 0;
        if(path == NULL){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        if(strcmp(current_path,"/")==0){
            for(int i=0; i<allEntries.size();i++){
                if(strcmp(path,allEntries[i]->Entries.DShortFileName)==0){
                    if(allEntries[i]->Entries.DAttributes ==0){
                        MachineResumeSignals(&sigToSave);
                        return VM_STATUS_ERROR_INVALID_ID;
                    }
                    found = 1;
                    break;
                }
            }
            if(found ==0){
                MachineResumeSignals(&sigToSave);
                return VM_STATUS_ERROR_INVALID_ID;
            }
        }
        
        char abspath[100];
        VMFileSystemGetAbsolutePath(abspath, current_path, path);
        strcpy(current_path,abspath);
        
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
    }
    // TVMStatus VMDirectoryCreate(const char *dirname);
    // TVMStatus VMDirectoryUnlink(const char *path);
    
    TVMStatus VMMemoryPoolCreate(void *base, TVMMemorySize size, TVMMemoryPoolIDRef memory){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        
        if(base == NULL || memory == NULL || size == 0){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }else {
            memPool *new_memory = new memPool(memoryPoolID, size, (uint8_t*) base);
            *memory = memoryPoolID;
            memoryPoolID++;
            memBucket.push_back(new_memory);
        }
        
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
    }
    
    TVMStatus VMMemoryPoolDelete(TVMMemoryPoolID memory){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        int memFound = 0;
        //cout <<"begin delete"<<endl;
        for(unsigned int i = 0; i < memBucket.size(); i++){
            if(memBucket[i]->MemID == memory){
                memFound = 1;
                if(memBucket[i]->alloc.size() != 0){
                    //cout << "2nd" <<endl;
                    MachineResumeSignals(&sigToSave);
                    return VM_STATUS_ERROR_INVALID_STATE;
                }
                else{
                    memBucket.erase(memBucket.begin()+i);
                }
            }
        }
        
        if(memFound == 0){
            //cout << "1st"<<endl;
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        //cout <<"end of delete"<<endl;
        
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
    }
    
    TVMStatus VMMemoryPoolQuery(TVMMemoryPoolID memory, TVMMemorySizeRef bytesleft){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        int memFound = 0;
        
        if(bytesleft == NULL){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        
        for(unsigned int i = 0; i < memBucket.size(); i++){
            if(memBucket[i]->MemID == memory){
                memFound = 1;
                *bytesleft = memBucket[i]->MemSize;
                break;
            }
        }
        
        if(memFound == 0){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
    }
    
    TVMStatus VMMemoryPoolAllocate(TVMMemoryPoolID memory, TVMMemorySize size, void **pointer){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        int memFound = 0;
        TVMMemorySize len = 0;
        
        if(size == 0 || pointer == NULL ){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }//check size and pointer
        
        if(size%64==0){
            len = size;
        }
        else{
            len = (size/64)*64+64;
        }//know how many size to allocate
        for(unsigned int i = 0; i < memBucket.size(); i++){
            if(memBucket[i]->MemID == memory){
                memFound = 1;
                if(memBucket[i]->MemSize < size){
                    //cout <<"not enough size" <<endl;
                    MachineResumeSignals(&sigToSave);
                    return VM_STATUS_ERROR_INSUFFICIENT_RESOURCES;
                }
                else{
                    if(memBucket[i]->alloc.empty() == 0){ //mempool is empty
                        memBlock *allocMemBlock= new memBlock(memBucket[i]->base,len);
                        memBucket[i]->alloc.push_back(allocMemBlock);
                        *(pointer) = memBucket[i]->base;
                        
                        memBucket[i]->MemSize -= len;
                        memBucket[i]->base += len;
                        /*we need to figure out how to move mempool base ptr*/
                    }//if no memblock allocated empty of dealloc
                    else{
                        int succeed = 0;
                        for(unsigned int j = 0; j < memBucket[i]->dealloc.size(); j++){
                            
                            if(memBucket[i]->dealloc[j]->blockSize == len){
                                *(pointer) = memBucket[i]->dealloc[j]->base;
                                memBucket[i]->alloc.push_back(memBucket[i]->dealloc[j]);
                                memBucket[i]->dealloc.erase(memBucket[i]->dealloc.begin()+j);
                                memBucket[i]->MemSize -= len;
                                succeed = 1;
                                break;
                            }
                        }
                        
                        if (succeed == 0){
                            for(unsigned int j = 0; j < memBucket[i]->dealloc.size(); j++){
                                
                                if(memBucket[i]->dealloc[j]->blockSize > len){
                                    *(pointer) = memBucket[i]->dealloc[j]->base;
                                    
                                    memBlock *allocMemBlock = new memBlock(memBucket[i]->dealloc[j]->base,len);
                                    memBucket[i]->alloc.push_back(allocMemBlock);
                                    
                                    memBucket[i]->dealloc[j]->blockSize -= len;
                                    memBucket[i]->dealloc[j]->base += len;
                                    
                                    memBucket[i]->MemSize -= len;
                                    succeed = 1;
                                    break;
                                }
                            }//for
                        }//size larger
                        
                        if(succeed == 0){
                            memBlock *allocMemBlock= new memBlock(memBucket[i]->base,len);
                            memBucket[i]->alloc.push_back(allocMemBlock);
                            
                            *(pointer) = memBucket[i]->base;
                            //cout << *(pointer) <<endl;
                            memBucket[i]->MemSize -= len;
                            memBucket[i]->base += len;
                        }//create a new one
                    }//end of else, dealloc is not empty
                }
                break;
            }//if found
        }//end of for loop
        
        
        if(memFound == 0){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
    }
    
    TVMStatus VMMemoryPoolDeallocate(TVMMemoryPoolID memory, void *pointer){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        memBlock* previous = NULL;
        memBlock* current = NULL;
        memBlock* post = NULL;
        
        int memFound = 0;
        if(pointer == NULL){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        //cout << "begin of dealloc" <<endl;
        for(unsigned int i = 0; i < memBucket.size(); i++){
            if(memBucket[i]->MemID == memory){
                memFound = 1;
                for(unsigned int j = 0; j < memBucket[i]->alloc.size(); j++){
                    //cout <<"for loop to find current block"<<endl;
                    //cout << "find current memblock " <<*(memBucket[i]->alloc[j]->base) <<endl;
                    //cout << "find current memblock " <<*((uint8_t*) pointer) <<endl;
                    if(memBucket[i]->alloc[j]->base == (void*) pointer){
                        //cout << "find current memblock" <<endl;
                        current = memBucket[i]->alloc[j];
                        memBucket[i]->alloc.erase(memBucket[i]->alloc.begin()+j);
                        break;
                    }
                }
                
                if(current == NULL){
                    MachineResumeSignals(&sigToSave);
                    return VM_STATUS_ERROR_INVALID_PARAMETER;
                }
                
                //cout << "start of merging" <<endl;
                //cout << "size " << memBucket[i]->dealloc.size() <<endl;
                //cout << "pointer " << *((uint8_t*) pointer) <<endl;
                for(unsigned int j = 0; j < memBucket[i]->dealloc.size(); j++){
                    //out <<"1"<<endl;
                    //cout << memBucket[i]->dealloc[j]->blockSize<<endl;
                    if(memBucket[i]->dealloc[j]->base+memBucket[i]->dealloc[j]->blockSize ==(uint8_t*) pointer){
                        previous = memBucket[i]->dealloc[j];
                    }
                    //cout <<"2"<<endl;
                    if(memBucket[i]->dealloc[j]->base == (current->base + current->blockSize)){
                        post = memBucket[i]->dealloc[j];
                    }
                    //cout <<"find previous and post"<<endl;
                }//find previous and post
                
                if(post == NULL && previous == NULL){
                    memBucket[i]->dealloc.push_back(current);
                    //cout << "current memBlock size" << current->blockSize <<endl;
                    //cout << "found allocated one to deallocate" <<endl;
                }
                else if(post != NULL && previous != NULL){
                    previous->blockSize += current->blockSize + post->blockSize;
                    for(unsigned int j = 0; j < memBucket[i]->dealloc.size(); j++){
                        if(memBucket[i]->dealloc[j]->base == post->base){
                            memBucket[i]->dealloc.erase(memBucket[i]->dealloc.begin()+j);
                            break;
                        }
                    }
                    //merge top and bottom
                }
                else if(post != NULL && previous == NULL){
                    post->blockSize += current->blockSize;
                    //cout << "change blockSize of post " <<endl;
                    post->base = current->base;
                    //cout << "passed if there is no previous" <<endl;
                    //merge bottom with current
                }
                else if(post == NULL && previous != NULL){
                    previous->blockSize += current->blockSize;
                    //cout << "passed if there is no post " <<endl;
                    //merge top with current
                }
                
                //cout << "end of merging " <<endl;
                //cout << "MemSize" << memBucket[i]->MemSize <<endl;
                //cout << "current memBlock size" << current->base <<endl;
                
                memBucket[i]->MemSize += current->blockSize;
                
                //cout << "end of dealloc" <<endl;
            }//if find the memoryPool
        }
        
        
        if(memFound == 0){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
    }
    
    
    TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        //cout << "create thread function" <<endl;
        if(entry == NULL || tid == NULL){
            //cout << "null"<<endl;
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        } else {
            threadID++;
            threadCount++;
            *tid = threadID;
            Thread *th = new Thread(entry, param, memsize, prio, *tid, VM_THREAD_STATE_DEAD);
            
            threads_Q.push_back(th);
            MachineContextCreate(&(th->MC), Skeleton,th, th->stack_addr, th->memsize);
        }
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
        
    }//create one thread
    
    TVMStatus VMThreadSleep(TVMTick tick){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        if (tick == VM_TIMEOUT_INFINITE){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        else if(tick == VM_TIMEOUT_IMMEDIATE){
            curThread -> state = VM_THREAD_STATE_READY;
            ready_Q.push_back(curThread);
            Scheduler();
        }
        else{
            curThread -> sleeptick = tick;
            curThread -> state = VM_THREAD_STATE_WAITING;
            Scheduler();
        }
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
    }
    
    TVMStatus VMThreadDelete(TVMThreadID thread){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        int thread_found = 0;
        for(unsigned int i = 0; i < threads_Q.size(); i++){
            if (threads_Q[i]->id == thread){
                thread_found = 1;
                if(threads_Q[i]->state != VM_THREAD_STATE_DEAD){
                    MachineResumeSignals(&sigToSave);
                    return VM_STATUS_ERROR_INVALID_STATE;
                }
                else if(threads_Q[i]->state == VM_THREAD_STATE_DEAD){
                    threads_Q.erase(threads_Q.begin()+i);
                }
            }
        }//end of for loop
        MachineResumeSignals(&sigToSave);
        if (thread_found == 0) {
            return VM_STATUS_ERROR_INVALID_ID;
        }
        return VM_STATUS_SUCCESS;
    }
    
    void Skeleton(void *param_func) {
        //MachineResumeSignals(sigToSave);
        //cout << "skeleton"<<endl;
        MachineEnableSignals();
        Thread *hold = (Thread*)param_func;
        hold->entry(hold->param);
        VMThreadTerminate(hold->id);
    }//void SkeletonEntry
    
    TVMStatus VMThreadActivate(TVMThreadID thread){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        int thread_found = 0;
        for(unsigned int i = 0; i < threads_Q.size(); i++){
            if(threads_Q[i] -> id == thread){
                thread_found = 1;
                if(threads_Q[i] -> state == VM_THREAD_STATE_DEAD){
                    threads_Q[i] -> state = VM_THREAD_STATE_READY;
                    ready_Q.push_back(threads_Q[i]);
                    if(curThread->state == VM_THREAD_STATE_RUNNING && curThread->priority < threads_Q[i]->priority){
                        Scheduler();
                    }
                }
                else{
                    MachineResumeSignals(&sigToSave);
                    return VM_STATUS_ERROR_INVALID_STATE;
                }
            }
        }// end of for loop
        
        MachineResumeSignals(&sigToSave);
        if(thread_found == 0)
            return VM_STATUS_ERROR_INVALID_ID;
        return VM_STATUS_SUCCESS;
    }
    
    TVMStatus VMThreadTerminate(TVMThreadID thread){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        Thread* newThread = NULL;
        
        int thread_found = 0;
        for(unsigned int i = 0; i < threads_Q.size(); i++){
            if(threads_Q[i] -> id == thread){
                thread_found = 1;
                if(threads_Q[i] -> state == VM_THREAD_STATE_DEAD){
                    MachineResumeSignals(&sigToSave);
                    return VM_STATUS_ERROR_INVALID_STATE;
                }
                threads_Q[i] -> state = VM_THREAD_STATE_DEAD;
                for(unsigned int i = 0; i < ready_Q.size(); i++){
                    if(ready_Q[i] -> id == thread){
                        ready_Q.erase(ready_Q.begin() + i);
                        break;
                    }
                }//end of for loop ready_Q
                for(unsigned int i = 0; i < sleeping_Q.size(); i++){
                    if(sleeping_Q[i] -> id == thread){
                        sleeping_Q.erase(sleeping_Q.begin() + i);
                        break;
                    }
                }
                
                for(unsigned int i= 0; i<threads_Q[i]->hold.size();i++){
                    threads_Q[i]->hold.erase(threads_Q[i]->hold.begin() + i);
                    
                }
                
                for(unsigned int j = 0; j < Mutex_Q.size(); j++){
                    int thread_found = 0;
                    if(Mutex_Q[j] -> Boss == threads_Q[i] -> id){
                        
                        Mutex_Q[j] -> Boss = (unsigned int)-1;
                        
                        if(Mutex_Q[j]->high_wait.size() == 0 && Mutex_Q[j]->medium_wait.size()== 0 && Mutex_Q[j]->low_wait.size()== 0){
                            
                            continue;
                        }
                        if(Mutex_Q[j]->high_wait.size()!= 0){
                            thread_found = 1;
                            Mutex_Q[j]->Boss = (Mutex_Q[j]->high_wait[0])->id;
                            (Mutex_Q[j]->high_wait[0])-> hold.push_back(Mutex_Q[j]);
                            newThread = Mutex_Q[i]->high_wait[0];
                            Mutex_Q[j]->high_wait.erase(Mutex_Q[j]->high_wait.begin());
                            
                        }
                        if(Mutex_Q[j]->medium_wait.size()!= 0 && thread_found == 0){
                            thread_found = 1;
                            
                            Mutex_Q[j]->Boss = (Mutex_Q[j]->medium_wait[0])->id;
                            (Mutex_Q[j]->medium_wait[0])-> hold.push_back(Mutex_Q[j]);
                            newThread = Mutex_Q[j]->medium_wait[0];
                            Mutex_Q[j]->medium_wait.erase(Mutex_Q[j]->medium_wait.begin());
                            
                        }
                        
                        if(Mutex_Q[j]->medium_wait.size()!= 0 && thread_found == 0){
                            thread_found = 1;
                            Mutex_Q[j]->Boss = (Mutex_Q[j]->low_wait[0])->id;
                            (Mutex_Q[j]->low_wait[0])-> hold.push_back(Mutex_Q[j]);
                            newThread = Mutex_Q[j]-> low_wait[0];
                            Mutex_Q[j]-> low_wait.erase(Mutex_Q[j]->low_wait.begin());
                            
                        }
                        
                        newThread->state = VM_THREAD_STATE_READY;
                        ready_Q.push_back(newThread);
                        
                    }//end of Mutex for loop
                    
                }//end of for loop
                Scheduler();
                break;
            }//if find the thread
        }//for
        
        MachineResumeSignals(&sigToSave);
        if(thread_found == 0)
            return VM_STATUS_ERROR_INVALID_ID;
        return VM_STATUS_SUCCESS;
    }
    
    TVMStatus VMThreadID(TVMThreadIDRef threadref) {
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        if (threadref == NULL) {
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        *(threadref) = curThread->id;
        
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
    }
    
    TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef stateref){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        int thread_found = 0;
        if (stateref == NULL) {
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        
        for(unsigned int i = 0; i < threads_Q.size(); i++){
            //cout<<(threads_Q[i]->id)<<endl;
            if (threads_Q[i]->id == thread){
                thread_found = 1;
                *stateref = threads_Q[i]->state;
            }
        }
        //Scheduler();
        MachineResumeSignals(&sigToSave);
        if(thread_found == 0){
            return VM_STATUS_ERROR_INVALID_ID;
        }
        return VM_STATUS_SUCCESS;
    }
    
    /* Retrieves milliseconds between ticks of the virtual machine. */
    TVMStatus VMTickMS(int *tickmsref){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        if(tickmsref == NULL){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        else {
            //cout<<"tick is here!"<<endl;
            *tickmsref = tickMS;
        }
        
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
    }
    
    TVMStatus VMTickCount(TVMTickRef tickref) {
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        if(tickref == NULL){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        else{
            *tickref = tickCount;
        }
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
    }
    
    
    TVMStatus VMFileOpen(const char *filename, int flags, int mode, int *filedescriptor) {
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        //cout << "here" <<endl;
        if(filename == NULL || filedescriptor == NULL){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        //should check the current directory first
        //wait if  writing back out, or if you are doing subdirectories.
        //curThread->state = VM_THREAD_STATE_WAITING;
        int found = 0;
        if(flag & O_CREAT){
            //create a file
            Root_entry* new_root = new Root_entry();
            SVMDirectoryEntry* new_entry = new SVMDirectoryEntry();
            //new_entry->DShortFileName = *filename;
            strcpy(new_entry->DShortFileName,filename);
            new_root->descriptor = assignfiledescriptor;
            *filedescriptor = assignfiledescriptor;
            assignfiledescriptor++;
            //allEntries[i]->fileFlag = flags; ???
            new_root->fileMode = mode;
            //store time & date use util functions
            //assign the initial cluster
        }
        else{
            cout << allFileEntries.size() <<endl;
            for(int i = 0; i < allFileEntries.size(); i++){
                //cout <<"value of i "<< i <<endl;
                //string str = string(allEntries[i]->Entries.DShortFileName);
                
                const char* str = allEntries[i]->Entries.DShortFileName;
                cout << *str <<endl;
                cout <<allEntries[i]->Entries.DShortFileName<<endl;
                cout << *filename <<endl;
                //cout <<"value of i "<< i <<endl;
                if(!strcmp(str,filename)){
                    found = 1;
                    //cout <<"find the file"<<endl;
                    allEntries[i]->descriptor = assignfiledescriptor;
                    assignfiledescriptor++;
                    *filedescriptor = allEntries[i]->descriptor;
                    allEntries[i]->fileFlag = flags;
                    allEntries[i]->fileMode = mode;
                    openedEntries.push_back(allEntries[i]);
                    //cout <<"value of i "<< i <<endl;
                    
                    break;
                }
                else{
                    //cout <<"not the same string"<<endl;
                }
            
            }//end of for loop
            if(found ==0){
                *filedescriptor = assignfiledescriptor;
                assignfiledescriptor ++;
                
                /*curThread->state = VM_THREAD_STATE_WAITING;
                MachineFileOpen(filename, flags, mode, fileCallBack, curThread);
                Scheduler();
                
                *filedescriptor = *(curThread->result_Return);
                
                MachineResumeSignals(&sigToSave);
                if(*filedescriptor > 0){
                    return VM_STATUS_SUCCESS;
                }
                else {
                    return VM_STATUS_FAILURE;
                }*/
            }
        }
        cout <<"open file "<< *filedescriptor <<endl;
        
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
        
        /*curThread->state = VM_THREAD_STATE_WAITING;
         MachineFileOpen(filename, flags, mode, fileCallBack, curThread);
         Scheduler();
         
         *filedescriptor = *(curThread->result_Return);
         
         MachineResumeSignals(&sigToSave);
         if(*filedescriptor > 0){
         return VM_STATUS_SUCCESS;
         }
         else {
         return VM_STATUS_FAILURE;
         }*/
    }
    
    int filePointer;
    void ReadCluster(int filedescriptor, int currenClusterOffset, int ClusterOffset, void* data, int ReadSize, int length){
        //ReadClusters just reads in data from a single cluster it doesn't need to know anything about the FAT.
        //    Not sure about how you have things structured, but you probably want to do that in VMFR. If ReadClusters just reads in data from a single cluster it doesn't need to know anything about the FAT. Then you can do the calculations dealing with the FAT in another function.
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        int tempLength = ReadSize;
        int length_transfer;
        int offset = 0;
        void* ptr = new void*;
        int value_store = 0;
        //cout <<"READSIZE "<<ReadSize<<endl;
        if(ReadSize > 512){
            VMMemoryPoolAllocate((unsigned int)1, 512, &ptr);
        }else{
            VMMemoryPoolAllocate((unsigned int)1, ReadSize, &ptr);
        }
        curThread->state = VM_THREAD_STATE_WAITING;
        MachineFileSeek(filedescriptor, currenClusterOffset, SEEK_SET, fileCallBack, curThread);
        Scheduler();
        filePointer = *(curThread -> result_Return);
        //cout <<"filepointer after seek"<< filePointer<<endl;
        
        while(tempLength != 0){
            if(tempLength >512){
                tempLength -= 512;
                length_transfer = 512;
            }else{
                length_transfer = tempLength;
                tempLength = 0;
            }
        //allocate memory for 1 cluster
            
            curThread->state = VM_THREAD_STATE_WAITING;
            MachineFileRead(filedescriptor, ptr, length_transfer,fileCallBack,curThread);
            Scheduler();
            
            value_store += *(curThread -> result_Return);
        
            if(value_store < 0){
                //cout <<"read fail"<<endl;
                MachineResumeSignals(&sigToSave);
            }
        
            memcpy((char*)data + offset, ptr, length_transfer);
            offset += length_transfer;
        }//end of while
        length = value_store;
        //cout <<"length read "<<length <<endl;
        VMMemoryPoolDeallocate((unsigned int)1, (uint8_t*)ptr);
        MachineResumeSignals(&sigToSave);
    }
    
    void FATFindCluster(int currenCluster,int ClustersToHop, int nextClusterOffset, uint16_t nextClusterNo){
        //firstcluster is the offset for firstcluster
        
        int index = (currenCluster - 2)*2+4;//start with fatArray4
        nextClusterNo = fatArray[index] + ((uint16_t)fatArray[index+1]<<8);//get next cluster number
        //cout << "nexnextClusterNo" <<nextClusterNo <<endl;
        if(nextClusterNo >=  65528){
            filePointer = 65528;//mark it's the end of file
        }else{
            nextClusterOffset = FirstDataSector * bpb->BPB_BytsPerSec + (nextClusterNo-2)*(bpb->BPB_BytsPerSec) * (bpb->BPB_SecPerClus);
        }
        //readSector(filedescriptor,fatArray, 512, fat->fatSize);
        //uint16_t fat_begin = fatArray[0] + ((uint16_t)fatArray[1]<<8);
    }
    
    TVMStatus VMFileRead(int filedescriptor, void *data, int *length){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        if(data == NULL || length == NULL){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        //cout << "read file "<<filedescriptor<<endl;
        void* ptr = new void*;
        int tempLength = *length;
        
        int length_transfer;
        int lengthReturn= 0;
        int offset = 0;
        int FirstCluster = 0;
        int value_store = 0;
        int clusterNumber = 0;
        int currentClusterNo;
        int currentClusterOffset;
        int nextClusterNo;
        int nextClusterOffset = 0;
        int length_transfered;
        int ClusterOffset;
        //check if data value is a location in the shared memorycout << "here" << tempLength << endl;
        
        //cout <<"read length "<<tempLength<<endl;
        
        //cout << "filedescriptor" << filedescriptor<<endl;
        if(filedescriptor >= 3){
            
            /*if(tempLength > 512){
             length_transfer = 512;
             VMMemoryPoolAllocate((unsigned int)1, length_transfer, &ptr);
             }else{
             VMMemoryPoolAllocate((unsigned int)1, tempLength, &ptr);
             }//allocare shared_memory at ptr */
            
            for(unsigned int i = 0;i < openedEntries.size();i++){
                //cout <<"file descriptor "<< allEntries[i]->descriptor <<endl;
                if(openedEntries[i]->descriptor == filedescriptor){
                    
                    /*cout << "find the file "<<openedEntries[i]->descriptor <<endl;
                    cout << "find the file "<<openedEntries[i]->fileFlag <<endl;
                    cout << "find the file "<<openedEntries[i]->Entries.DShortFileName<<endl;
                    */
                    clusterNumber = (int)openedEntries[i]->firstbyte;//initial
                    //cast to uint16_t;
                    //cout << "clusterNumber " <<clusterNumber << endl;
                    
                    currentClusterNo = clusterNumber;
                    FirstCluster = FirstDataSector * bpb->BPB_BytsPerSec + (clusterNumber - 2) * (bpb->BPB_BytsPerSec) * (bpb->BPB_SecPerClus);
                    currentClusterOffset = FirstCluster;
                    
                    int ClusterSize = (bpb->BPB_BytsPerSec) * (bpb->BPB_SecPerClus);
                    
                    ClusterOffset = openedEntries[i]->Entries.DSize % ClusterSize;
                    //cout <<"after seek"<<endl;
                    while(tempLength != 0 && filePointer < 65528){ //stop either when it's the end of file or buffer is full
                        if(tempLength >= ClusterSize){
                            tempLength -= ClusterSize;
                            length_transfer = ClusterSize;
                        }
                        
                        int ClustersToHop = filePointer/ClusterSize; //0 for the first cluster
                        
                        //rest of cluster
                        
                        FATFindCluster(currentClusterNo, ClustersToHop, nextClusterOffset, nextClusterNo);
                        //cout << "currentClusterOffset" <<currentClusterOffset <<endl;
                        if(filePointer == 65528){
                            //cout <<"end of file"<<endl;
                            if(ClusterOffset != 0){
                                currentClusterOffset = FirstDataSector * bpb->BPB_BytsPerSec + (openedEntries[i]->Entries.DSize/ClusterSize)*ClusterSize;//last readable cluster
                                length_transfer = ClusterOffset;//we are calculating, 2.5 clusters then 2 had been read and now .5 need to be read
                            }else{
                                currentClusterOffset = FirstDataSector * bpb->BPB_BytsPerSec + (openedEntries[i]->Entries.DSize/ClusterSize - 1)*ClusterSize;
                                length_transfer = ClusterSize; // we are calculating, 3 clusters then 2 had been read and now 1 need to be read
                            }
                            
                        }
                        //cout <<"length_transfer "<<length_transfer<<endl;
                        ReadCluster(filedescriptor, currentClusterOffset, ClusterOffset, (char*)data + length_transfered,length_transfer, lengthReturn);
                        
                        if(lengthReturn < 0){
                            //cout <<"read fail"<<endl;
                            MachineResumeSignals(&sigToSave);
                            return VM_STATUS_FAILURE;
                        }
                        *length = lengthReturn;
                        length_transfered = length_transfer;
                        
                        currentClusterNo = nextClusterNo;
                        currentClusterOffset = nextClusterOffset;
                        
                        //ClusterOffset = filePointer % ClusterSize; //0 for the first cluster
                        //need to have cluster structure to store clusters
                        
                        //Do check for running off end of cluster adjust read size
                        //You could have something like the above in a loop that does multiple reads from clusters.
                        
                        
                    }//while loop
                    //*length = length_transfered;
                    /*if(allEntries[i]->Entries.DSize >= (value_store+track)){
                     *length = value_store;
                     track += *length;
                     }else{
                     *length = allEntries[i]->Entries.DSize - track;
                     track = 0;
                     }*/
                    MachineResumeSignals(&sigToSave);
                    return VM_STATUS_SUCCESS;
                }//if find the file
            }//for
        }//if filedescriptor>3
        else if(filedescriptor < 3) {
            if(tempLength > 512){
                length_transfer = 512;
                VMMemoryPoolAllocate((unsigned int)1, length_transfer, &ptr);
            }else{
                VMMemoryPoolAllocate((unsigned int)1, tempLength, &ptr);
            }//allocare shared_memory at ptr
            
            
            while(tempLength != 0){
                
                if(tempLength > 512){
                    tempLength -= 512;
                    length_transfer = 512;
                }else{
                    length_transfer = tempLength;
                    tempLength = 0;
                }
                
                curThread -> state = VM_THREAD_STATE_WAITING;
                MachineFileRead(filedescriptor, ptr, length_transfer, fileCallBack, curThread);
                Scheduler();
                value_store += *(curThread -> result_Return);
                if(value_store < 0){
                    //cout << "failure" <<endl;
                    MachineResumeSignals(&sigToSave);
                    return VM_STATUS_FAILURE;
                }
                memcpy((int*)data + offset, ptr, length_transfer);
                offset += length_transfer;//move position to be copied
            }//end of while
            //cout << "deallocate destination at " << *((uint8_t*)ptr) << endl;
            
            *length = value_store;
            VMMemoryPoolDeallocate((unsigned int)1, (uint8_t*)ptr);
            //cout << "end of filewrite " <<endl;
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_SUCCESS;
        }
    }
    
    TVMStatus VMFileWrite(int filedescriptor, void *data, int *length){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        if (data == NULL || length == NULL){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        
        void* ptr = new void*;
        int tempLength = *length;
        int length_transfer;
        int offset = 0;
        int value_store = 0;
        
        //cout << "write length "<<tempLength <<endl;
        if(tempLength > 512){
            length_transfer = 512;
            VMMemoryPoolAllocate((unsigned int)1, length_transfer, &ptr);
        }else{
            VMMemoryPoolAllocate((unsigned int)1, tempLength, &ptr);
        }//allocare shared_memory at ptr
        
        while(tempLength != 0){
            
            if(tempLength > 512){
                tempLength -= 512; //1024-512 = 512
                length_transfer = 512;
            }else{
                length_transfer = tempLength;
                tempLength = 0;
            }
            
            curThread -> state = VM_THREAD_STATE_WAITING;
            memcpy(ptr, (char*)data+offset, length_transfer); //copy data to shared_memory at ptr
            
            //write to file from ptr of shared_memory
            MachineFileWrite(filedescriptor, ptr, length_transfer, fileCallBack, curThread);
            
            Scheduler();
            
            value_store  += *(curThread -> result_Return);
            
            //cout << "return result" << *length <<endl;
            
            if(value_store < 0){
                MachineResumeSignals(&sigToSave);
                return VM_STATUS_FAILURE;
            }
            
            offset += length_transfer;//move position to be copied
            
            
        }//end of while
        
        *length = value_store;
        //cout << "deallocate destination at " << *((uint8_t*)ptr) << endl;
        VMMemoryPoolDeallocate((unsigned int)1, (uint8_t*)ptr);
        //cout << "end of filewrite " <<endl;
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
        
    }
    
    TVMStatus VMFileSeek(int filedescriptor, int offset, int whence, int *newoffset) {
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        //When offset + whence > end of file, what should happen? Do we just return failure?yes
        
        
        curThread -> state = VM_THREAD_STATE_WAITING;
        MachineFileSeek(filedescriptor, offset, whence, fileCallBack, curThread);
        
        Scheduler();
        *newoffset = *(curThread -> result_Return);
        
        /*if(filePointer < (int)0xFFF8){
            filePointer = *newoffset;
            //cout <<"filepointer after seek "<<filePointer<<endl;
        }*/
        
        MachineResumeSignals(&sigToSave);
        if(newoffset != NULL)
            return VM_STATUS_SUCCESS;
        else
            return VM_STATUS_FAILURE;
    }
    
    TVMStatus VMFileClose(int filedescriptor) {
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        /*if(filedescriptor <3){
            curThread -> state = VM_THREAD_STATE_WAITING;
            MachineFileClose(filedescriptor, fileCallBack, curThread);
            Scheduler();
        }else{*/
        for(int i=0; i<openedEntries.size();i++){
            
            if(openedEntries[i]->descriptor == filedescriptor){
                openedEntries[i]->descriptor = -1;
                //update access time
                filePointer = (unsigned int)0;
                openedEntries.erase(openedEntries.begin()+i);
                break;
            }
        }
        
        
        MachineResumeSignals(&sigToSave);
        if(*(curThread->result_Return) < 0)
            return VM_STATUS_FAILURE;
        else
            return VM_STATUS_SUCCESS;
    }// end of VMFileClose
    
    TVMStatus VMMutexCreate(TVMMutexIDRef mutexref) {
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        
        if(mutexref == NULL){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        
        Mutex *MT = new Mutex(Mutex_Count, 0, (unsigned int)-1);
        *mutexref = Mutex_Count;
        Mutex_Count++;
        Mutex_Q.push_back(MT);
        
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
    }//Mutex Create end
    
    TVMStatus VMMutexDelete(TVMMutexID mutex){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        int mutex_found = 0;
        
        for(unsigned int i = 0; i < Mutex_Q.size(); i++){
            if(Mutex_Q[i]->Mutex_id == mutex){
                mutex_found = 1;
                if(Mutex_Q[i]->Boss != (unsigned int)-1){
                    MachineResumeSignals(&sigToSave);
                    return VM_STATUS_ERROR_INVALID_STATE;
                }
                else{
                    Mutex_Q.erase(Mutex_Q.begin() + i);
                    break;
                }
            }
        }
        
        if(mutex_found == 0){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_ID;
        }
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
    }//Mutex Delete end
    
    TVMStatus VMMutexQuery(TVMMutexID mutex, TVMThreadIDRef ownerref){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        int mutex_found = 0;
        
        if(ownerref == NULL){
            //cout << "owner is null"<<endl;
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        
        for(unsigned int i = 0; i < Mutex_Q.size(); i++){
            if(Mutex_Q[i]->Mutex_id == mutex){
                mutex_found = 1;
                
                if(Mutex_Q[i]->Boss == (unsigned int)-1){
                    *ownerref = VM_THREAD_ID_INVALID;
                }//ID of mutex has no owner, is unlocked
                else{
                    *ownerref = Mutex_Q[i]->Boss;
                    break;
                }
            }
        }
        if(mutex_found == 0){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_ID;
        }
        
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
    }//Mutex Query end
    
    /*VMMutexAcquire – Locks the mutex.*/
    //VMMutexAcquire() attempts to lock the mutex specified by mutex
    //waiting up to timeout ticks.
    TVMStatus VMMutexAcquire(TVMMutexID mutex, TVMTick timeout){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        int mutex_found = 0;
        //cout <<curThread-> id<<" want mutex " <<mutex <<endl;
        for(unsigned int i = 0; i < Mutex_Q.size(); i++){
            if(Mutex_Q[i]->Mutex_id == mutex){
                mutex_found = 1;
                
                if( Mutex_Q[i]->Boss == (unsigned int)-1){ // unlocked
                    
                    Mutex_Q[i]->Boss = curThread->id;
                    //cout<<"Get in here right now2-1"<<endl;
                    MachineResumeSignals(&sigToSave);
                    return VM_STATUS_SUCCESS;
                }
                else if(Mutex_Q[i]->Boss != (unsigned int)-1){ //locked
                    if(timeout == VM_TIMEOUT_IMMEDIATE){
                        //cout << "mutex Boss is "<<Mutex_Q[i]->Boss<<endl;
                        //cout <<"mutex is locked and time is VM_TIMEOUT_IMMEDIATE"<<endl;
                        MachineResumeSignals(&sigToSave);
                        return VM_STATUS_FAILURE;
                    }
                    else if(timeout == VM_TIMEOUT_INFINITE){
                        curThread->state = VM_THREAD_STATE_WAITING;
                        if(curThread-> priority == VM_THREAD_PRIORITY_HIGH){
                            Mutex_Q[i]->high_wait.push_back(curThread);
                        }else if(curThread-> priority == VM_THREAD_PRIORITY_NORMAL){
                            Mutex_Q[i]->medium_wait.push_back(curThread);
                        }else if(curThread-> priority == VM_THREAD_PRIORITY_LOW){
                            Mutex_Q[i]->low_wait.push_back(curThread);
                        }
                        //thread needs to wait and sleep, and don't decrement sleepsick
                        //cout << "here in the timeout inifinit" <<endl;
                        VMThreadSleep((unsigned int)-2);
                        
                    }
                    else{ //timeout is value greater than 0
                        
                        curThread->state = VM_THREAD_STATE_WAITING;
                        if(curThread-> priority == VM_THREAD_PRIORITY_HIGH){
                            Mutex_Q[i]->high_wait.push_back(curThread);
                        }else if(curThread-> priority == VM_THREAD_PRIORITY_NORMAL){
                            Mutex_Q[i]->medium_wait.push_back(curThread);
                        }else if(curThread-> priority == VM_THREAD_PRIORITY_LOW){
                            Mutex_Q[i]->low_wait.push_back(curThread);
                        }
                        //thread needs to wait and sleep for that long
                        VMThreadSleep(timeout);//put current thread to sleep for that amount of time
                    }
                    
                    
                    
                    int expired = 1;
                    if(curThread -> sleeptick == 0){
                        /*for(unsigned int i=0; i< curThread->hold.size();i++){
                         if((curThread->hold[i])->Boss == curThread->id){
                         expired = 0;
                         break;
                         }
                         }*/
                        if(Mutex_Q[i]->Boss == curThread->id){
                            expired = 0;
                            break;
                        }
                    }
                    if(expired == 1){ //if time expires
                        //remove from mutex's waiting queue
                        if(curThread->priority == VM_THREAD_PRIORITY_HIGH){
                            for(unsigned int j = 0; Mutex_Q[i]->high_wait.size(); j++){
                                if(Mutex_Q[i]->high_wait[j]->id == curThread->id){
                                    Mutex_Q[i]->high_wait.erase( Mutex_Q[i]->high_wait.begin()+j);
                                }
                            }
                        }
                        else if(curThread->priority == VM_THREAD_PRIORITY_NORMAL){
                            for(unsigned int j = 0; Mutex_Q[i]->medium_wait.size(); j++){
                                if(Mutex_Q[i]->medium_wait[j]->id == curThread->id){
                                    Mutex_Q[i]->medium_wait.erase( Mutex_Q[i]->medium_wait.begin()+j);
                                }
                            }
                        }
                        else if(curThread->priority == VM_THREAD_PRIORITY_LOW){
                            for(unsigned int j = 0; Mutex_Q[i]->low_wait.size(); j++){
                                if(Mutex_Q[i]->low_wait[j]->id == curThread->id){
                                    Mutex_Q[i]->low_wait.erase( Mutex_Q[i]->low_wait.begin()+j);
                                }
                            }
                        }
                        
                        
                        //cout << "expire"<<endl;
                        MachineResumeSignals(&sigToSave);
                        return VM_STATUS_FAILURE;
                    }
                    //cout<<"mutex acquired is done here"<<endl;
                }//end of else if locked
                break;
            }//if find the Mutex
        }//for loop
        
        
        //need to have a sleep list??
        
        if(mutex_found == 0){
            //cout << "not found mutex"<<endl;
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_ID;
        }
        
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
    }//Mutex Acquire end
    
    
    /*VM() releases the mutex specified by the mutex parameter that is currently held by the running thread.*/
    TVMStatus VMMutexRelease(TVMMutexID mutex){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        
        int newThread_found = 0;
        int mutex_found = 0;
        Thread* newThread = NULL;
        Thread* tempThread = NULL;
        
        //cout << "release mutex"<<mutex<<endl;
        for(unsigned int i = 0; i < Mutex_Q.size(); i++){
            if(Mutex_Q[i]->Mutex_id == mutex){
                mutex_found = 1;
                
                if(Mutex_Q[i]->Boss != curThread->id){
                    MachineResumeSignals(&sigToSave);
                    return VM_STATUS_ERROR_INVALID_STATE;
                }
                
                if(Mutex_Q[i]->Boss == (unsigned int)-1){ //not hold by the running thread
                    return VM_STATUS_ERROR_INVALID_STATE;
                }
                else if(Mutex_Q[i]->Boss != (unsigned int)-1){ //hold by the running thread
                    Mutex_Q[i]->Boss = (unsigned int)-1;
                    
                    for(unsigned int i= 0; i<curThread->hold.size();i++){
                        if((curThread->hold[i])->Mutex_id == mutex){
                            curThread->hold.erase(curThread->hold.begin() + i);
                            break;
                        }
                    }
                    
                    if(Mutex_Q[i]->high_wait.size()!= 0){
                        newThread_found = 1; //found high wait thread in the queue
                        Mutex_Q[i]->Boss = (Mutex_Q[i]->high_wait[0])->id; //assign the owner to the thread
                        (Mutex_Q[i]->high_wait[0])-> hold.push_back(Mutex_Q[i]);
                        newThread = Mutex_Q[i]->high_wait[0];
                        Mutex_Q[i]->high_wait.erase(Mutex_Q[i]->high_wait.begin());
                    }
                    
                    if(Mutex_Q[i]->medium_wait.size()!= 0 && newThread_found == 0){
                        newThread_found = 1;
                        Mutex_Q[i]->Boss = (Mutex_Q[i]->medium_wait[0])->id;
                        (Mutex_Q[i]->medium_wait[0])-> hold.push_back(Mutex_Q[i]);
                        newThread = Mutex_Q[i]->medium_wait[0];
                        Mutex_Q[i]->medium_wait.erase(Mutex_Q[i]->medium_wait.begin());
                    }
                    if(Mutex_Q[i]->low_wait.size()!= 0 && newThread_found == 0){
                        Mutex_Q[i]->Boss = (Mutex_Q[i]->low_wait[0])->id;
                        (Mutex_Q[i]->low_wait[0])-> hold.push_back(Mutex_Q[i]);
                        newThread = Mutex_Q[i]-> low_wait[0];
                        Mutex_Q[i]-> low_wait.erase(Mutex_Q[i]->low_wait.begin());
                    }
                    
                    if(Mutex_Q[i]->high_wait.size() == 0 && Mutex_Q[i]->medium_wait.size()!= 0 && Mutex_Q[i]->low_wait.size()!= 0){
                        Mutex_Q[i]->Boss = (unsigned int)-1;  //no thread is waiting
                    }
                    
                    if (newThread != NULL){
                        if(newThread->priority > curThread->priority){
                            for(unsigned int i = 0; i < sleeping_Q.size(); i++){
                                if(newThread->id == sleeping_Q[i]->id){
                                    newThread->sleeptick = 0;
                                    sleeping_Q.erase(sleeping_Q.begin()+i);
                                    break;
                                }
                            }
                            
                            curThread->state = VM_THREAD_STATE_READY;
                            newThread -> state = VM_THREAD_STATE_RUNNING;
                            tempThread = curThread;
                            
                            ready_Q.push_back(tempThread);
                            curThread = newThread;
                            MachineContextSwitch(&(tempThread -> MC), &(curThread -> MC));
                            
                        }else{
                            for(unsigned int i = 0; i < sleeping_Q.size(); i++){
                                if(newThread->id == sleeping_Q[i]->id){
                                    newThread->sleeptick = 0;
                                    sleeping_Q.erase(sleeping_Q.begin()+i);
                                    break;
                                }
                            }
                            newThread->state = VM_THREAD_STATE_READY;
                            ready_Q.push_back(newThread);
                            //should be in sleepque or about to wake up
                            //so it can be ready and put back to ready queue
                        }
                    }//newThread not equal to null
                    
                    
                }//hold by the running thread
                break;
            }//if find the Mutex
        }//end of for loop
        
        if(mutex_found == 0){
            //cout << "mutex not found"<<endl;
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_ID;
        }
        
        //cout <<"end of release"<<endl;
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
        
    }//Mutex Release end
    
}
