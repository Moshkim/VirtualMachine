//KWANIL KIM
//QIANHUI FAN

#include "VirtualMachine.h"
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

/* Order recommended by the professor
    * 1.VMLoadModule
    * 2.MachineInitialize
    * 3.MachineRequestAlarm
    * 4.MachineEnableSignals
    * 5.VMMain (whatever VMLoadModule returned)
*/
volatile int sleepCount;




extern "C" {
    TVMMainEntry VMLoadModule(const char *module);

    void VMUnloadModule(void);
    //void MachineFileOpen(const char *filename, int flags, int mode, TMachineFileCallback callback, void *calldata);
    void MachineInitialize(void);
    void MachineEnableSignals(void);
    //void MachineRequestAlarm(useconds_t usec, TMachineAlarmCallback callback, void *calldata);




    class ThreadControlBlock{
        vector<ThreadControlBlock*> high;
        vector<ThreadControlBlock*> medium;
        vector<ThreadControlBlock*> low;
        vector<ThreadControlBlock*> ready_Q;

    };

    class Thread{
        public:
            TVMThreadIDRef ID;
            TVMThreadState State;
            TVMThreadPriority Priority;
            TVMThreadEntry entry;
            TVMMemorySize memsize;
            void *param;

            Thread(TVMThreadEntry entry1, void *param1, TVMMemorySize memsize1, TVMThreadPriority Priority1, TVMThreadIDRef ID1, TVMThreadState State1){
                ID = ID1;
                State = State1;
                Priority = Priority1;
                entry = entry1;
                param = param1;
                memsize = memsize1;
            }

    };
    TVMStatus VMStart(int tickms, int argc, char *argv[]) {
        /* code */
        TVMMainEntry Begin = VMLoadModule(argv[0]);
        int tickmsTosec = tickms * 1000;

        if(Begin == NULL){
            return VM_STATUS_FAILURE;
        }
        else{
            MachineInitialize();
            MachineEnableSignals();
            (*Begin)(argc, argv);
            //VMThreadCreate(entry, void *param, memsize, prio, tid)
            VMThreadCreate(NULL, NULL, 0, VM_THREAD_PRIORITY_NORMAL, 0);
            //MachineRequestAlarm(tickmsTosec,)

            return VM_STATUS_SUCCESS;
        }

    }

    // void callBack(void* call_data, int result){
    //
    // }

    TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid){

        if(entry == NULL || tid == NULL){
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        } else {
            Thread *th1 = new Thread(entry, param, memsize, prio, tid, VM_THREAD_STATE_DEAD);
        }
        return VM_STATUS_SUCCESS;

    }

    TVMStatus VMThreadSleep(TVMTick tick){
        if (tick == 0){
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        else{
            return VM_STATUS_SUCCESS;
        }
    }
    TVMStatus VMFileRead(int filedescriptor, void *data, int *length){
        return VM_STATUS_SUCCESS;
    }
    //void MachineFileWrite(int fd, void *data, int length, TMachineFileCallback callback, void *calldata);
    //void MachineFileOpen(const char *filename, int flags, int mode, TMachineFileCallback callback, void *calldata);

    TVMStatus VMFileWrite(int filedescriptor, void *data, int *length){
        int n = write(filedescriptor, (char*)data, *length);
        if (data == NULL || length == NULL)
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        else if (n)
            return VM_STATUS_SUCCESS;
        else
            return VM_STATUS_FAILURE;
    }
}
