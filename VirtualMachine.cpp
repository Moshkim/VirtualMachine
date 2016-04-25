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
    void scheduler();



    class Thread{

    public:
        TVMThreadIDRef id;
        TVMThreadState state;
        TVMThreadPriority priority;
        TVMThreadEntry entry;
        TVMMemorySize memsize;
        TVMTick sleeptick;
        void *param;
        SMachineContext MC;
        char *stack_addr;
        int *result_Return;

        Thread(TVMThreadEntry Entry, void *Param, TVMMemorySize Memsize, TVMThreadPriority Priority, TVMThreadIDRef ID, TVMThreadState State){
            id = ID;
            state = State;
            priority = Priority;
            entry = Entry;
            param = Param;
            memsize = Memsize;
            stack_addr = new char[memsize];
            //MC = new SMachineContext;
            //sleeptick = 1;
        }

    };




    Thread *idle_thread;
    /*****************ticks multi thread sleep count********************/
    //VMTickMS(int *ticks);

    /*****************single thread sleep count********************/
    volatile int sleepCount;
    volatile int flag = 0;
    /*****************signal********************/
    TMachineSignalState sigToSave;

    /*****************number of threads********************/
    int threadCount = 0;
    Thread *curThread;
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
        cout << "callBack"<<endl;
        if(threadCount == 1){
            sleeping_Q[0] -> sleeptick--;
            if(sleeping_Q[0] -> sleeptick == 0){
                cout << "sleeptick is zero"<<endl;
                sleeping_Q[0] -> state = VM_THREAD_STATE_READY;
                ready_Q.push_back(sleeping_Q[0]);
                sleeping_Q.pop_back();
                scheduler();
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
                scheduler();
            }
        }
      }

    }

    void fileCallBack(void *calldata, int result){
        //cerr << "fileCallBack" <<endl;
        //cout << "enter fileCallBack"<<endl;
        ((Thread*)calldata) -> result_Return = &result;
        cout << "set thread in fileCallBack"<<endl;
        ((Thread*)calldata) -> state = VM_THREAD_STATE_READY;
        //cout << "result stored is " << *(((Thread*)calldata) -> result_Return) << endl;
        ready_Q.push_back((Thread*)calldata);
        //flag = 1;

        scheduler();
    }

    void scheduler(){
        Thread* newThread = NULL;
        Thread* tempThread = NULL;
        //if the high Priority & and the state is ready
        cout << "size of ready_Q " << ready_Q.size()<<endl;
        cout << "enter scheduler" <<endl;
        // if(high.size() != 0){
        //     //if(high[0] -> state == VM_THREAD_STATE_READY)
        //     newThread = high.front();
        //     high.erase(high.begin());
        // }
        // else if(medium.size() != 0){
        //     newThread = medium.front();
        //     high.erase(medium.begin());
        // }
        // else if(low.size() != 0){
        //     newThread = low.front();
        //     high.erase(low.begin());
        // }
        if(ready_Q.size() == 0 && curThread != idle_thread){
            newThread = idle_thread;
        }
        else if(ready_Q.size() != 0) {
            int count = 0;
            //cout << "scheduling when ready queue has thread in it" <<endl;
            //cout << ready_Q[0]->priority << endl;
            //look for highest priority in the ready_Q to make it running
            for(unsigned int i = 0; i < ready_Q.size(); i++){
                if(ready_Q[i]->priority == VM_THREAD_PRIORITY_HIGH){
                    newThread = ready_Q[i];
                    ready_Q.erase(ready_Q.begin() + i);
                    count++;
                    break;
                }
            }// end of for
            //look for normal priority in the ready_Q to make it running
            //cout << "switch to normal ***_______----------------cout "<< count <<endl;
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
            //cout << "switch to low ***_______----------------cout "<< count <<endl;
            //look for low priority in the ready_Q to make it running
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
       //cout << "check Priority" <<endl;
       //cout << "current thread state " << curThread-> state <<endl;
       //cout << "current thread sleeptick " << curThread -> sleeptick << endl;


        if(curThread -> state != VM_THREAD_STATE_WAITING) {
            if(curThread != idle_thread){
                curThread -> state = VM_THREAD_STATE_READY;
                ready_Q.push_back(curThread);
            }
        }
        else if(curThread -> state == VM_THREAD_STATE_WAITING && curThread -> sleeptick != 0){
            sleeping_Q.push_back(curThread);
            //cout << "size of sleeping_Q "<< sleeping_Q.size() << endl;
        }
/*        else if(curThread -> state == VM_THREAD_STATE_WAITING && curThread -> sleeptick == 0){
            curThread -> state = VM_THREAD_STATE_READY;
            ready_Q.push_back(curThread);
            cout << "size of sleeping_Q "<< sleeping_Q.size() << endl;
        }*/
        //cout << "***************" <<endl;

        newThread -> state = VM_THREAD_STATE_RUNNING;
        //cout << "***************" <<endl;
        tempThread = curThread;
        //cout << "***************" <<endl;
        curThread = newThread;
        cout << *(curThread ->id) << endl;
        cout << "end of scheduler" <<endl;

        MachineContextSwitch(&(tempThread -> MC), &(curThread -> MC));
        cerr<< "here ????? passed MCS" <<endl;
    }

    void idle_func( void *idle){

        cout << "loop"<<endl;
        while(true);
    }

    TVMStatus VMStart(int tickms, int argc, char *argv[]) {
        /* code */
        TVMMainEntry Begin = VMLoadModule(argv[0]);
        int tickmsTosec = tickms * 1000;

        if(Begin == NULL){
            return VM_STATUS_FAILURE;
        }
        else{
            cout << "start begin" << endl;
            MachineInitialize();
            MachineEnableSignals();
            MachineRequestAlarm(tickmsTosec,callBack,NULL);
            cout << "here1" <<endl;
            //VMThreadCreate(entry, void *param, memsize, prio, tid)
            //VMThreadCreate(NULL, NULL, 0, VM_THREAD_PRIORITY_NORMAL, (TVMThreadIDRef)1);
            Thread *thread_first= new Thread(NULL,NULL,0,VM_THREAD_PRIORITY_NORMAL,(TVMThreadIDRef)1,VM_THREAD_STATE_DEAD);
            threadCount++;
            ready_Q.push_back(thread_first);
            //medium.push_back(thread_first);
            thread_first->state = VM_THREAD_STATE_RUNNING;
            //Thread(TVMThreadEntry Entry, void *Param, TVMMemorySize Memsize, TVMThreadPriority Priority, TVMThreadIDRef ID, TVMThreadState State)
            cout << "here2" <<endl;
            //cout << medium.size() <<endl;
            curThread = ready_Q.front();
            ready_Q.erase(ready_Q.begin());
            //curThread = medium.front();
            //medium.erase(medium.begin());
            cout << "here3" <<endl;
            idle_thread = new Thread(NULL, NULL, 0x100000, (TVMThreadPriority)0x00, (TVMThreadIDRef)0, VM_THREAD_STATE_READY);
            //idle_thread->priority = VM_THREAD_PRIORITY_NORMAL;
            cout << "here4" <<endl;
            MachineContextCreate(&(idle_thread->MC), idle_func, NULL, idle_thread->stack_addr, idle_thread->memsize);
            cout << "here5" <<endl;
            (*Begin)(argc, argv);
            cout << "start done";
            return VM_STATUS_SUCCESS;
        }
    }


    TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid){
        MachineSuspendSignals(&sigToSave);
        cout << "create thread function" <<endl;
        if(entry == NULL || tid == NULL){
            cout << "null"<<endl;
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        } else {
            Thread *th = new Thread(entry, param, memsize, prio, tid, VM_THREAD_STATE_DEAD);
            cout << "thread created" <<endl;

            threadCount++;
            *(th -> id) = threadCount;
            *tid = *(th -> id);
            threads_Q.push_back(th);
            th -> state = VM_THREAD_STATE_READY;
            ready_Q.push_back(th);

        }
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;

    }//create one thread

    TVMStatus VMThreadSleep(TVMTick tick){
        if (tick == VM_TIMEOUT_INFINITE){
            //cout << "sleep infinite" << endl;
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        else{

            curThread -> sleeptick = tick;
            curThread -> state = VM_THREAD_STATE_WAITING;
            //cout << "set sleep state and tick" << endl;
            scheduler();
            //cout << "come back from scheduler" << endl;
            return VM_STATUS_SUCCESS;
        }
    }

    TVMStatus VMThreadDelete(TVMThreadID thread){
        MachineSuspendSignals(&sigToSave);
        int thread_found = 0;
        for(unsigned int i = 0; i < threads_Q.size(); i++){
            if (*(threads_Q[i]->id) == thread){
                thread_found = 1;
                if(threads_Q[i]->state != VM_THREAD_STATE_DEAD){
                    MachineResumeSignals(&sigToSave);
                    return VM_STATUS_ERROR_INVALID_STATE;
                }
            }
        }//end of for loop
        if (thread_found == 0) {
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_ID;
        }
        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
    }

    TVMStatus VMThreadActivate(TVMThreadID thread){
        // MachineSuspendSignals(&sigToSave);
        // for(unsigned int i = 0; i < threads_Q.size(); i++){
        //     if(*threads_Q[i])

        // }

        return VM_STATUS_SUCCESS;
    }

    TVMStatus VMThreadTerminate(TVMThreadID thread){
        return VM_STATUS_SUCCESS;
    }

    TVMStatus VMThreadID(TVMThreadIDRef threadref) {
        if (threadref == NULL) {
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        *(threadref) = *(curThread->id);

        return VM_STATUS_SUCCESS;
    }

    TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef stateref){
        cout << "get state " <<endl;
        int thread_found = 0;
        if (stateref == NULL) {
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }

        for(unsigned int i = 0; i < threads_Q.size(); i++){
            if (*(threads_Q[i]->id) == thread){
                thread_found = 1;
                *stateref = threads_Q[i]->state;
            }
        }

        if(thread_found == 0){
            return VM_STATUS_ERROR_INVALID_ID;
        }

        return VM_STATUS_SUCCESS;
    }

    /* Retrieves milliseconds between ticks of the virtual machine. */
    TVMStatus VMTickMS(int *tickmsref){
        if(tickmsref == NULL)
            return VM_STATUS_ERROR_INVALID_PARAMETER;


        return VM_STATUS_SUCCESS;
    }

    TVMStatus VMTickCount(TVMTickRef tickref) {
        if(tickref == NULL)
            return VM_STATUS_ERROR_INVALID_PARAMETER;

        return VM_STATUS_SUCCESS;
    }


    TVMStatus VMFileOpen(const char *filename, int flags, int mode, int *filedescriptor) {
        MachineSuspendSignals(&sigToSave);
        cout << "here" <<endl;
        if(filename == NULL || filedescriptor == NULL){
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        curThread->state = VM_THREAD_STATE_WAITING;

        MachineFileOpen(filename, flags, mode, fileCallBack, curThread);
        MachineResumeSignals(&sigToSave);
        scheduler();
        cout << "result after callback " << *(curThread -> result_Return) << endl;
        *filedescriptor = *(curThread->result_Return);

        if(*filedescriptor > 0){
            return VM_STATUS_SUCCESS;
        }
        else {
            return VM_STATUS_FAILURE;
        }
    }


    TVMStatus VMFileRead(int filedescriptor, void *data, int *length){
        MachineSuspendSignals(&sigToSave);
        if(data == NULL || length == NULL){
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        curThread -> state = VM_THREAD_STATE_WAITING;
        MachineFileRead(filedescriptor, data, *length, fileCallBack, curThread);
        MachineResumeSignals(&sigToSave);
        scheduler();
        *length = *(curThread -> result_Return);


        if(*length > 0)
            return VM_STATUS_SUCCESS;
        else
            return VM_STATUS_FAILURE;

    }
    //void MachineFileWrite(int fd, void *data, int length, TMachineFileCallback callback, void *calldata);

    TVMStatus VMFileWrite(int filedescriptor, void *data, int *length){
        MachineSuspendSignals(&sigToSave);
        //cout << "here write" <<endl;
        if (data == NULL || length == NULL)
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        curThread -> state = VM_THREAD_STATE_WAITING;

        //cout << "threadCount " << threadCount << endl;

        MachineFileWrite(filedescriptor, data, *length, fileCallBack, curThread);
        MachineResumeSignals(&sigToSave);
        //cout << "context switch in VFW" << endl;
        scheduler();
        //cout << "come out of scheduler,wait to get ready" <<endl;
        *length = *(curThread -> result_Return);

        if(*length > 0)
            return VM_STATUS_SUCCESS;
        else
            return VM_STATUS_FAILURE;
    }

    TVMStatus VMFileSeek(int filedescriptor, int offset, int whence, int *newoffset) {
        MachineSuspendSignals(&sigToSave);

        curThread -> state = VM_THREAD_STATE_WAITING;

        MachineFileSeek(filedescriptor, offset, whence, fileCallBack, curThread);
        MachineResumeSignals(&sigToSave);
        scheduler();
        *newoffset = *(curThread -> result_Return);

        if(newoffset != NULL)
            return VM_STATUS_SUCCESS;
        else
            return VM_STATUS_FAILURE;
    }

    TVMStatus VMFileClose(int filedescriptor) {
        MachineSuspendSignals(&sigToSave);

        curThread -> state = VM_THREAD_STATE_WAITING;

        MachineFileClose(filedescriptor, fileCallBack, curThread);
        MachineResumeSignals(&sigToSave);
        scheduler();

        if(*(curThread->result_Return) < 0)
            return VM_STATUS_FAILURE;
        else
            return VM_STATUS_SUCCESS;
    }// end of VMFileClose

}
