//KWANIL KIM
//QIANHUI FAN

#include "VirtualMachine.h"
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <sys.msg.h>
#include <string.h>
#include <iostream>
#include <cstring>
#include <sys/wait.h>
#include <sys/stat.h>


extern "C" {
    TVMMainEntry VMLoadModule(const char *module);
    void VMUnloadModule(void);
    void MachineFileOpen(const char *filename, int flags, int mode, TMachineFileCallback callback, void *calldata);
}

void MachineInitialize(void);
void MachineEnableSignals(void);
void MachineRequestAlarm(useconds_t usec, TMachineAlarmCallback callback, void *calldata);


TVMStatus VMStart(int tickms, int argc, char *argv[]) {
    /* code */
    TVMMainEntry Begin = VMLoadModule(argv[0]);
    int tickmsTosec = tickms * 1000;

    /*  Order recommended by the professor

*  1. VMLoadModule

*  2. MachineInitialize

*  3. MachineRequestAlarm

*  4. MachineEnableSignals

*  5. VMMain (whatever VMLoadModule returned)

*/

    if(Begin == NULL){
        return VM_STATUS_FAILURE;
    }
    else{
        MachineInitialize();
        MachineEnableSignals();
        MachineRequestAlarm(tickmsTosec,)

        return VM_STATUS_SUCCESS;
    }




}

TVMStatus VMFileRead(int filedescriptor, void *data, int *length){



}
void MachineFileOpen(const char *filename, int flags, int mode, TMachineFileCallback callback, void *calldata);
TVMStatus VMFileWrite(int filedescriptor, void *data, int *length);
TVMStatus VMFilePrint(int filedescriptor, const char *format, ...){
