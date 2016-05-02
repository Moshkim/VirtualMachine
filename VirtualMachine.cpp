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
    void MachineInitialize(void);
    void MachineEnableSignals(void);
    void Scheduler();
    void Skeleton(void *param);

    class Mutex;

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
            stack_addr = new char[memsize];
        }

    };

    TVMMutexID Mutex_Count;
    Thread *idle_thread;
    Thread *curThread;
    TVMThreadID threadID;



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

        // if(((Thread*)calldata)->priority <= curThread->priority)

        if(((Thread*)calldata)->priority <= curThread->priority)
            Scheduler();

        // for(unsigned int i=0; i<ready_Q.size(); i++){
        //     if(ready_Q[i]->priority > curThread->priority){
        //         Scheduler();
        //         break;
        //     }
        //
        // }

        //cerr << "leave fileCallBack"<<endl;
        MachineResumeSignals(&sigToSave);
    }

    void Scheduler(){
        Thread* newThread = NULL;
        Thread* tempThread = NULL;

        //cerr<<"In "<<__FILE__<<" at "<<__LINE__<<": curThread "<< curThread->id<<endl;

        //cerr<<"In "<<__FILE__<<" at "<<__LINE__<<": curThread state "<< curThread->state<<endl;


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
                        //cout << "switch to normal "<<ready_Q[i]->id <<endl;
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

        //cerr<<"In "<<__FILE__<<" at "<<__LINE__<<": switch to  "<< newThread->id<<endl;
        MachineContextSwitch(&(tempThread -> MC), &(curThread -> MC));
    }

    void idle_func( void *idle){
        MachineEnableSignals();
        while(true);
    }

    TVMStatus VMStart(int tickms, int argc, char *argv[]) {
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        TVMMainEntry Begin = VMLoadModule(argv[0]);
        int tickmsTosec = tickms * 1000;
        tickMS = tickms;
        if(Begin == NULL){
            return VM_STATUS_FAILURE;
        }
        else{
            //cout << "start begin" << endl;
            MachineInitialize();
            MachineEnableSignals();
            MachineRequestAlarm(tickmsTosec,callBack,NULL);
            //cout << "here1" <<endl;

            Thread *thread_first= new Thread(NULL,NULL,0,VM_THREAD_PRIORITY_NORMAL,(unsigned int)1,VM_THREAD_STATE_DEAD);
            threadCount++;
            threadID = 1;
            threads_Q.push_back(thread_first);
            ready_Q.push_back(thread_first);
            thread_first->state = VM_THREAD_STATE_RUNNING;

            curThread = ready_Q.front();
            ready_Q.erase(ready_Q.begin());

            idle_thread = new Thread(NULL, NULL, 0x100000, (TVMThreadPriority)0x00, (unsigned int)0, VM_THREAD_STATE_READY);
            MachineContextCreate(&(idle_thread->MC), idle_func, NULL, idle_thread->stack_addr, idle_thread->memsize);

            (*Begin)(argc, argv);
            cout << "start done";
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_SUCCESS;
        }
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
        //cout << "sleep for " << tick <<endl;
        //cout<< "sleeeeeeeeeeeeeeeeeeeeepp" <<endl;
        //cout << "thread sleeped ###################"<< curThread -> id << endl;
        if (tick == VM_TIMEOUT_INFINITE){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        else{

            curThread -> sleeptick = tick;
            curThread -> state = VM_THREAD_STATE_WAITING;
            //cout << "set sleep state and tick" << endl;
            //sleeping_Q.push_back(curThread);
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
        //cout << "terminate"<<endl;
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

                for(unsigned int i = 0; i < Mutex_Q.size(); i++){
                    int thread_found = 0;
                    if(Mutex_Q[i] -> Boss == threads_Q[i] -> id){


                        Mutex_Q[i] -> Boss = (unsigned int)-1;
                        if(Mutex_Q[i]->high_wait.size() == 0 && Mutex_Q[i]->medium_wait.size()== 0 && Mutex_Q[i]->low_wait.size()== 0){
                            //Mutex_Q[i]-> Boss == NULL;
                            continue;
                        }
                        if(Mutex_Q[i]->high_wait.size()!= 0){
                            thread_found = 1;
                            Mutex_Q[i]->Boss = (Mutex_Q[i]->high_wait[0])->id;
                            //*((Mutex_Q[i]->high_wait[0])-> hold) = Mutex_Q[i]->Mutex_id;
                            (Mutex_Q[i]->high_wait[0])-> hold.push_back(Mutex_Q[i]);
                            newThread = Mutex_Q[i]->high_wait[0];
                            Mutex_Q[i]->high_wait.erase(Mutex_Q[i]->high_wait.begin());
                        }
                        if(Mutex_Q[i]->medium_wait.size()!= 0 && thread_found == 0){
                            thread_found = 1;
                            Mutex_Q[i]->Boss = (Mutex_Q[i]->medium_wait[0])->id;
                            //*((Mutex_Q[i]->medium_wait[0])-> hold) = Mutex_Q[i]->Mutex_id;
                            (Mutex_Q[i]->medium_wait[0])-> hold.push_back(Mutex_Q[i]);
                            newThread = Mutex_Q[i]->medium_wait[0];
                            Mutex_Q[i]->medium_wait.erase(Mutex_Q[i]->medium_wait.begin());
                        }

                        if(Mutex_Q[i]->medium_wait.size()!= 0 && thread_found == 0){
                            thread_found = 1;
                            Mutex_Q[i]->Boss = (Mutex_Q[i]->low_wait[0])->id;
                            //*((Mutex_Q[i]->low_wait[0])-> hold) = Mutex_Q[i]->Mutex_id;
                            (Mutex_Q[i]->low_wait[0])-> hold.push_back(Mutex_Q[i]);
                            newThread = Mutex_Q[i]-> low_wait[0];
                            Mutex_Q[i]-> low_wait.erase(Mutex_Q[i]->low_wait.begin());
                        }

                        newThread->state = VM_THREAD_STATE_READY;
                        ready_Q.push_back(newThread);
                        //remove from waiting queue
                    }//end of Mutex for loop

                }//end of for loop sleeping_Q
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
        //cout <<"curThread is &&&"<< curThread->id<<endl;
        //cout << "get state &&&&&&&&&&&&&&" <<endl;
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
        curThread->state = VM_THREAD_STATE_WAITING;
        MachineFileOpen(filename, flags, mode, fileCallBack, curThread);
        Scheduler();

        *filedescriptor = *(curThread->result_Return);

        MachineResumeSignals(&sigToSave);
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
        Scheduler();
        *length = *(curThread -> result_Return);

        MachineResumeSignals(&sigToSave);
        if(*length > 0)
            return VM_STATUS_SUCCESS;
        else
            return VM_STATUS_FAILURE;

    }

    TVMStatus VMFileWrite(int filedescriptor, void *data, int *length){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        //cout << "here write*********************"<< *length <<endl;
        if (data == NULL || length == NULL){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        curThread -> state = VM_THREAD_STATE_WAITING;

        MachineFileWrite(filedescriptor, data, *length, fileCallBack, curThread);
        Scheduler();


        *length = *(curThread -> result_Return);

        if(*length > 0){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_SUCCESS;
        }
        else{
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_FAILURE;
        }
    }

    TVMStatus VMFileSeek(int filedescriptor, int offset, int whence, int *newoffset) {
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);

        curThread -> state = VM_THREAD_STATE_WAITING;
        MachineFileSeek(filedescriptor, offset, whence, fileCallBack, curThread);
        Scheduler();
        *newoffset = *(curThread -> result_Return);
        MachineResumeSignals(&sigToSave);
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
        Scheduler();

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
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }

        for(unsigned int i = 0; i < Mutex_Q.size(); i++){
            if(Mutex_Q[i]->Mutex_id == mutex){
                mutex_found = 1;

                if(Mutex_Q[i]->Boss == (unsigned int)-1){
                    MachineResumeSignals(&sigToSave);
                    return VM_THREAD_ID_INVALID;
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

        //cerr<<"In "<<__FILE__<<" at "<<__LINE__<<": Get in here right now"<<endl;

        for(unsigned int i = 0; i < Mutex_Q.size(); i++){
            if(Mutex_Q[i]->Mutex_id == mutex){
                //cout<<"Get in here right now!"<<endl;
                mutex_found = 1;
                if( Mutex_Q[i]->Boss == (unsigned int)-1){ // unlocked

                    //cerr<<"In "<<__FILE__<<" at "<<__LINE__<<": Get in here right now2"<<endl;

                    // cout<<"the curThread id is"<< curThread->id<<endl;
                    // cout<<"the Mutex_Q[i] is"<< Mutex_Q[i]->Mutex_id<<endl;
                    //cerr<<"In "<<__FILE__<<" at "<<__LINE__<<": the curThread id is"<< curThread->id <<endl;
                    //cerr<<"In "<<__FILE__<<" at "<<__LINE__<<": the mutex id is"<< Mutex_Q[i]->Mutex_id <<endl;

                    Mutex_Q[i]->Boss = curThread->id;
                    // cout<<"Get in here right now2-1"<<endl;
                    MachineResumeSignals(&sigToSave);
                    return VM_STATUS_SUCCESS;
                }
                else if(Mutex_Q[i]->Boss != (unsigned int)-1){ //locked
                    if(timeout == VM_TIMEOUT_IMMEDIATE){
                        //cerr<<"In "<<__FILE__<<" at "<<__LINE__<<": Get in here right now3"<<endl;

                        return VM_STATUS_FAILURE;

                    }
                    else if(timeout == VM_TIMEOUT_INFINITE){
                        //enqueue it in the waiting queue
                        //cerr<<"In "<<__FILE__<<" at "<<__LINE__<<": Get in here right now3-2"<<endl;
                        //cout<<"Get in here right now3-2"<<endl;
                        curThread->state = VM_THREAD_STATE_WAITING;
                        if(curThread-> priority == VM_THREAD_PRIORITY_HIGH){
                            Mutex_Q[i]->high_wait.push_back(curThread);
                        }else if(curThread-> priority == VM_THREAD_PRIORITY_NORMAL){
                            Mutex_Q[i]->medium_wait.push_back(curThread);
                        }else if(curThread-> priority == VM_THREAD_PRIORITY_LOW){
                            Mutex_Q[i]->low_wait.push_back(curThread);
                        }
                        //thread needs to wait and sleep, and don't decrement sleepsick
                        //cout << "here" <<endl;
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
                        for(unsigned int i=0; i< curThread->hold.size();i++){
                            if((curThread->hold[0])->Mutex_id == curThread->id){
                                expired = 0;
                                break;
                            }
                        }//for
                    }
                    if(expired == 1){
                        return VM_STATUS_FAILURE;
                    }
                }//end of else if locked
                break;
            }//if find the Mutex
        }//for loop


        //need to have a sleep list??

        if(mutex_found == 0){
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_ID;
        }

        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;
    }//Mutex Acquire end


    /*VMMutexRelease() releases the mutex specified by the mutex parameter that is currently held by the running thread.*/
    TVMStatus VMMutexRelease(TVMMutexID mutex){
        TMachineSignalState sigToSave;
        MachineSuspendSignals(&sigToSave);
        //cerr<<"In "<<__FILE__<<" at "<<__LINE__<<": start to release"<<endl;
        int newThread_found = 0;
        int mutex_found = 0;
        Thread* newThread = NULL;
        Thread* tempThread = NULL;

        for(unsigned int i = 0; i < Mutex_Q.size(); i++){
            if(Mutex_Q[i]->Mutex_id == mutex){

                mutex_found = 1;
                if(Mutex_Q[i]->Boss == (unsigned int)-1){ //not hold by the running thread
                    return VM_STATUS_ERROR_INVALID_STATE;
                }
                else{ //hold by the running thread
                    Mutex_Q[i]->Boss = (unsigned int)-1;

                    for(unsigned int i= 0; i<curThread->hold.size();i++){
                        if((curThread->hold[i])->Mutex_id == mutex){
                            curThread->hold.erase(curThread->hold.begin() + i);
                            break;
                        }
                    }

                    if(Mutex_Q[i]->high_wait.size()!= 0){
                        //cerr<<"In "<<__FILE__<<" at "<<__LINE__<<": find newThread in high_wait"<<endl;

                        Mutex_Q[i]->Boss = (Mutex_Q[i]->high_wait[0])->id;
                        newThread_found = 1;
                        //cerr<<"In "<<__FILE__<<" at "<<__LINE__<<": high_wait assign id"<<endl;

                        (Mutex_Q[i]->high_wait[0])-> hold.push_back(Mutex_Q[i]);
                        newThread = Mutex_Q[i]->high_wait[0];
                        Mutex_Q[i]->high_wait.erase(Mutex_Q[i]->high_wait.begin());

                    }
                    //cerr<<"In "<<__FILE__<<" at "<<__LINE__<<": thread found?"<< newThread_found<<endl;

                    if(Mutex_Q[i]->medium_wait.size()!= 0 && newThread_found == 0){
                        Mutex_Q[i]->Boss = (Mutex_Q[i]->medium_wait[0])->id;
                        newThread_found = 1;
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
                    //cerr<<"In "<<__FILE__<<" at "<<__LINE__<<": got newThread?"<<endl;

                    if (newThread != NULL){
                        if(newThread->priority > curThread->priority){
                            //have to remove it from sleepqueue and set sleeptick to 0
                            //switch to it directly
                            //cerr<<"In "<<__FILE__<<" at "<<__LINE__<<": high release"<<endl;

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
                            //Scheduler();
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
            MachineResumeSignals(&sigToSave);
            return VM_STATUS_ERROR_INVALID_ID;
        }

        MachineResumeSignals(&sigToSave);
        return VM_STATUS_SUCCESS;

    }//Mutex Release end

}
