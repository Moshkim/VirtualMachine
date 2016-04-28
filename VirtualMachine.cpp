//KWANIL KIM
//QIANHUI FAN

#include "VirtualMachine.h"
#include "Machine.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <sys/msg.h>
#include <termios.h>
#include <sys/types.h>
#include <string.h>
#include <iostream>
#include <cstring>
#include <sys/wait.h>
#include <sys/stat.h>

using namespace std;

extern "C" {
    TVMMainEntry VMLoadModule(const char *module);
    void VMUnloadModule(void);
    //void MachineFileOpen(const char *filename, int flags, int mode, TMachineFileCallback callback, void *calldata);
    void MachineInitialize(void);
    void MachineEnableSignals(void);
    void Scheduler();
    void Skeleton(void *param);



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
        uint8_t *stack_addr;
        int *result_Return;

        Thread(TVMThreadEntry Entry, void *Param, TVMMemorySize Memsize, TVMThreadPriority Priority, TVMThreadID ID, TVMThreadState State){
            id = ID;
            state = State;
            priority = Priority;
            entry = Entry;
            param = Param;
            memsize = Memsize;
            stack_addr = new uint8_t[memsize];
            //MC = new SMachineContext;
            //sleeptick = 1;
        }

    };

    Thread *idle_thread;
    Thread *curThread;
    TVMThreadID threadID;
    /*****************ticks multi thread sleep count********************/
    //VMTickMS(int *ticks);

    /*****************single thread sleep count********************/
    volatile int sleepCount;
    volatile int flag = 0;
    volatile unsigned int tickCount;
    volatile int tickMS;
    /*****************number of threads********************/
    volatile int threadCount = 0;
    //void MachineRequestAlarm(useconds_t usec, TMachineAlarmCallback callback, void *calldata);



    /********************************Threads***************************************/
    vector<Thread*> high;
    vector<Thread*> medium;
    vector<Thread*> low;
    vector<Thread*> threads_Q;
    vector<Thread*> ready_Q;
    vector<Thread*> sleeping_Q;
    /******************************************************************************/


    void callBack(void* calldata){
        tickCount++;
        if(threadCount == 1){
            sleeping_Q[0] -> sleeptick--;
            if(sleeping_Q[0] -> sleeptick == 0){
                cout << "sleeptick is zero"<<endl;
                sleeping_Q[0] -> state = VM_THREAD_STATE_READY;
                ready_Q.push_back(sleeping_Q[0]);
                sleeping_Q.pop_back();
                Scheduler();
            }
            //Not sure if we have to count sleep count for only one thread
        }
        else {
        for(unsigned int i = 0; i < sleeping_Q.size(); i++){
            sleeping_Q[i] -> sleeptick--;

            if(sleeping_Q[i] -> sleeptick == 0){
                sleeping_Q[i] -> state = VM_THREAD_STATE_READY;
                ready_Q.push_back(sleeping_Q[i]);
                sleeping_Q.erase(sleeping_Q.begin() + i);
                Scheduler();
            }
        }
      }

    }

    void fileCallBack(void *calldata, int result){
        tickCount++;
        ((Thread*)calldata) -> result_Return = &result;
        ((Thread*)calldata) -> state = VM_THREAD_STATE_READY;
        ready_Q.push_back((Thread*)calldata);
        //flag = 1;

        Scheduler();
    }

    void Scheduler(){
        Thread* newThread = NULL;
        Thread* tempThread = NULL;

        if(ready_Q.size() == 0 && curThread != idle_thread){
            newThread = idle_thread;
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
        if(curThread -> state == VM_THREAD_STATE_RUNNING && curThread -> state != VM_THREAD_STATE_DEAD) {
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
        //cerr<< "here ????? passed MCS" <<endl;
    }

    void idle_func( void *idle){
        while(true);
    }

    TVMStatus VMStart(int tickms, int argc, char *argv[]) {

        TVMMainEntry Begin = VMLoadModule(argv[0]);
        int tickmsTosec = tickms * 1000;
        tickMS = tickms;
        if(Begin == NULL){
            return VM_STATUS_FAILURE;
        }
        else{
            cout << "start begin" << endl;
            MachineInitialize();
            MachineEnableSignals();
            MachineRequestAlarm(tickmsTosec,callBack,NULL);

            Thread *thread_first= new Thread(NULL,NULL,0,VM_THREAD_PRIORITY_NORMAL,(TVMThreadID)0,VM_THREAD_STATE_DEAD);
            curThread = thread_first;
            threadCount++;
            threads_Q.push_back(thread_first);

            //Thread(TVMThreadEntry Entry, void *Param, TVMMemorySize Memsize, TVMThreadPriority Priority, TVMThreadIDRef ID, TVMThreadState State)

            idle_thread = new Thread(NULL, NULL, 0x100000, (TVMThreadPriority)0x00, (TVMThreadID)1, VM_THREAD_STATE_READY);
            //idle_thread->priority = VM_THREAD_PRIORITY_NORMAL;
            threads_Q.push_back(idle_thread);

            MachineContextCreate(&(idle_thread->MC), idle_func, NULL, idle_thread->stack_addr, idle_thread->memsize);
            threadID = 1;
            (*Begin)(argc, argv);
            return VM_STATUS_SUCCESS;
        }
    }


    TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        cout << "create thread function" <<endl;
        if(entry == NULL || tid == NULL){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        } else {
            Thread *th = new Thread(entry, param, memsize, prio, *tid, VM_THREAD_STATE_DEAD);


            threadID++;
            threadCount++;
            th -> id = threadID;
            *tid = threadID;
            // cout << "thread created" <<endl;
            // cout << "threadID is " <<threadID <<endl;
            // cout << "tid is " <<*tid <<endl;
            // cout << "id is " <<th -> id <<endl;
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
        else{

            curThread -> sleeptick = tick;
            curThread -> state = VM_THREAD_STATE_WAITING;
            Scheduler();
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_SUCCESS;
        }
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
        cout<<"skeleton"<<endl;
        Thread *hold = (Thread*)param_func;
        hold->entry(hold->param);
        VMThreadTerminate(hold->id);
    }//skeleton

    TVMStatus VMThreadActivate(TVMThreadID thread){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        int thread_found = 0;

        for(unsigned int i = 0; i < threads_Q.size(); i++){
            if(threads_Q[i]->id == thread){
                thread_found = 1;
                if(threads_Q[i]->state == VM_THREAD_STATE_DEAD){
                    cout<<"activate thread here with "<<threads_Q[i]->id<<endl;
                    threads_Q[i]->state = VM_THREAD_STATE_READY;
                    ready_Q.push_back(threads_Q[i]);
                }
                else if(curThread-> state == VM_THREAD_STATE_RUNNING && curThread->priority < threads_Q[i]->priority){
                    Scheduler();
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
        int thread_found = 0;
        for(unsigned int i = 0; i < threads_Q.size(); i++){
            if(threads_Q[i]->id == thread){
                thread_found = 1;
                if(threads_Q[i] -> state == VM_THREAD_STATE_DEAD){
                    MachineResumeSignals(&sigToSave);
                    return VM_STATUS_ERROR_INVALID_STATE;
                }
                if(threads_Q[i]->state == VM_THREAD_STATE_RUNNING){
                    thread_found = 1;
                }
                for(unsigned int i = 0; i < ready_Q.size(); i++){
                    if(ready_Q[i] -> id == thread){
                        ready_Q.erase(ready_Q.begin() + i);
                    }
                }//end of for loop ready_Q
                for(unsigned int i = 0; i < sleeping_Q.size(); i++){
                    if(sleeping_Q[i] -> id == thread){
                        sleeping_Q.erase(sleeping_Q.begin() + i);
                    }
                }//end of for loop sleeping_Q
                threads_Q[i]->state = VM_THREAD_STATE_DEAD;
                break;
            }//if find the thread
        }//end of the for loop

        Scheduler();
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
        //cout << "get state " <<endl;
        int thread_found = 0;
        if (stateref == NULL) {
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }

        for(unsigned int i = 0; i < threads_Q.size(); i++){
            cout<<(threads_Q[i]->id)<<endl;
            if (threads_Q[i]->id == thread){
                thread_found = 1;
                *stateref = threads_Q[i]->state;
            }
        }
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
//
    TVMStatus VMTickCount(TVMTickRef tickref) {
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        if(tickref == NULL){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        else{
            //cout<<"tickcount is here!"<<endl;
            *tickref = tickCount;
        }
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
    }


    TVMStatus VMFileOpen(const char *filename, int flags, int mode, int *filedescriptor) {
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        if(filename == NULL || filedescriptor == NULL){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        curThread->state = VM_THREAD_STATE_WAITING;

        MachineFileOpen(filename, flags, mode, fileCallBack, curThread);
        MachineResumeSignals(&sigToSave);
        Scheduler();
        *filedescriptor = *(curThread->result_Return);


        if(*filedescriptor > 0){
            return VM_STATUS_SUCCESS;
        }
        else {
            return VM_STATUS_FAILURE;
        }
    }


    TVMStatus VMFileRead(int filedescriptor, void *data, int *length){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        if(data == NULL || length == NULL){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        curThread -> state = VM_THREAD_STATE_WAITING;
        MachineFileRead(filedescriptor, data, *length, fileCallBack, curThread);
        MachineResumeSignals(&sigToSave);
        Scheduler();
        *length = *(curThread -> result_Return);


        if(*length > 0)
            return VM_STATUS_SUCCESS;
        else
            return VM_STATUS_FAILURE;

    }
    //void MachineFileWrite(int fd, void *data, int length, TMachineFileCallback callback, void *calldata);

    TVMStatus VMFileWrite(int filedescriptor, void *data, int *length){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        //cout << "here write" <<endl;
        if (data == NULL || length == NULL){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }

        curThread -> state = VM_THREAD_STATE_WAITING;

        MachineFileWrite(filedescriptor, data, *length, fileCallBack, curThread);
        MachineResumeSignals(&sigToSave);
        Scheduler();
        //cout << "come out of scheduler,wait to get ready" <<endl;
        *length = *(curThread -> result_Return);

        if(*length > 0)
            return VM_STATUS_SUCCESS;
        else
            return VM_STATUS_FAILURE;
    }

    TVMStatus VMFileSeek(int filedescriptor, int offset, int whence, int *newoffset) {
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);

        curThread -> state = VM_THREAD_STATE_WAITING;

        MachineFileSeek(filedescriptor, offset, whence, fileCallBack, curThread);
        MachineResumeSignals(&sigToSave);
        Scheduler();
        *newoffset = *(curThread -> result_Return);


        if(newoffset != NULL)
            return VM_STATUS_SUCCESS;
        else
            return VM_STATUS_FAILURE;
    }

    TVMStatus VMFileClose(int filedescriptor) {
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);

        curThread -> state = VM_THREAD_STATE_WAITING;

        MachineFileClose(filedescriptor, fileCallBack, curThread);
        MachineResumeSignals(&sigToSave);
        Scheduler();


        if(*(curThread->result_Return) < 0)
            return VM_STATUS_FAILURE;
        else
            return VM_STATUS_SUCCESS;
    }// end of VMFileClose

}
