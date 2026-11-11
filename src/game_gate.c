// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdint.h>
#include <stddef.h>

#define TAC_GAME_GATE_MAGIC UINT64_C(0x54414347414d4531)
#define TAC_GAME_MODULE_SLOTS 8
#define TAC_GAME_GATE_VERSION 2

enum {
    TAC_MODULE_PENDING  = 0,
    TAC_MODULE_STARTING = 1,
    TAC_MODULE_STARTED  = 2,
    TAC_MODULE_FAILED   = 3,
    TAC_MODULE_REJECTED = 4
};

enum {
    TAC_MODULE_PHASE_IDLE     = 0,
    TAC_MODULE_PHASE_THREAD   = 1,
    TAC_MODULE_PHASE_LOAD     = 2,
    TAC_MODULE_PHASE_EXPORTS  = 3,
    TAC_MODULE_PHASE_ACTIVATE = 4,
    TAC_MODULE_PHASE_COMPLETE = 5
};

typedef struct {
    uint32_t enabled, state;
    int32_t handle, startResult;
    char path[256];
    uint32_t phase, activation;
    int32_t threadResult, detachResult, activationResult, loadSymbolResult, unloadSymbolResult;
} TacGameModule;

typedef struct {
    uint64_t magic, cookie, loadStartModule;
    uint32_t version, bytes, enabled, busy;
    uint64_t callbacks[2];
    uint64_t threadCreate, threadDetach, dlsym;
    char sym_plugin_load[32];
    char sym_plugin_unload[32];
    char thread_name[32];
    TacGameModule modules[TAC_GAME_MODULE_SLOTS];
} TacGameGate;

_Static_assert(sizeof(TacGameModule)==300,"Unexpected native module slot ABI");
_Static_assert(offsetof(TacGameGate,modules)==176,"Unexpected native game gate ABI");
_Static_assert(sizeof(TacGameGate)<=0x1000,"Game module control overlaps pad bridge data");

typedef int (*TacGameLoadModule)(const char*,uint64_t,const void*,unsigned,const void*,int*);
typedef void* (*TacGameThreadEntry)(void*);
typedef int (*TacGameThreadCreate)(uintptr_t*,const void*,TacGameThreadEntry,void*,const char*);
typedef int (*TacGameThreadDetach)(uintptr_t);
typedef int (*TacGameDlsym)(int,const char*,void**);
typedef int (*TacGamePluginLoad)(int,const char**);

static void* module_worker(void *arg);

__attribute__((section(".text.entry"),noinline))
void tac_game_gate(TacGameGate *control,uint64_t cookie,unsigned lane){
  if(!control||lane>1||control->magic!=TAC_GAME_GATE_MAGIC||control->version!=TAC_GAME_GATE_VERSION||
     control->bytes!=sizeof *control||!cookie||control->cookie!=cookie)return;

  __atomic_fetch_add(&control->callbacks[lane],1,__ATOMIC_RELAXED);
  if(!__atomic_load_n(&control->enabled,__ATOMIC_ACQUIRE)||!control->loadStartModule||
     !control->threadCreate||!control->threadDetach||!control->dlsym)return;

  unsigned free=0;
  if(!__atomic_compare_exchange_n(&control->busy,&free,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return;

  for(unsigned i=0;i<TAC_GAME_MODULE_SLOTS;i++){
    TacGameModule *slot=&control->modules[i];
    unsigned pending=TAC_MODULE_PENDING;
    if(!__atomic_load_n(&slot->enabled,__ATOMIC_ACQUIRE)||
       !__atomic_compare_exchange_n(&slot->state,&pending,TAC_MODULE_STARTING,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED))continue;

    // Check that path is not empty and is an absolute path
    unsigned len=0;
    while(len<sizeof(slot->path)&&slot->path[len]) len++;
    int valid = (len>4 && slot->path[0]=='/');

    if(!valid){
      slot->handle=-1;
      slot->startResult=-1;
      __atomic_store_n(&slot->state,TAC_MODULE_REJECTED,__ATOMIC_RELEASE);
      break;
    }

    slot->handle=-1;
    __atomic_store_n(&slot->phase,TAC_MODULE_PHASE_THREAD,__ATOMIC_RELEASE);
    uintptr_t thread=0;
    int result=((TacGameThreadCreate)(uintptr_t)control->threadCreate)(&thread,0,module_worker,control,control->thread_name);
    __atomic_store_n(&slot->threadResult,result,__ATOMIC_RELEASE);
    if(!result){
      int detached=((TacGameThreadDetach)(uintptr_t)control->threadDetach)(thread);
      __atomic_store_n(&slot->detachResult,detached,__ATOMIC_RELEASE);
      return;
    }
    __atomic_store_n(&slot->state,TAC_MODULE_REJECTED,__ATOMIC_RELEASE);
    break;
  }
  __atomic_store_n(&control->busy,0,__ATOMIC_RELEASE);
}

static void* module_worker(void *arg){
  TacGameGate *control=arg;
  for(unsigned i=0;i<TAC_GAME_MODULE_SLOTS;i++){
    TacGameModule *slot=&control->modules[i];
    if(__atomic_load_n(&slot->state,__ATOMIC_ACQUIRE)!=TAC_MODULE_STARTING)continue;

    __atomic_store_n(&slot->phase,TAC_MODULE_PHASE_LOAD,__ATOMIC_RELEASE);
    int result=0;
    TacGameLoadModule load_fn = (TacGameLoadModule)(uintptr_t)control->loadStartModule;
    int handle = load_fn(slot->path, 0, 0, 0, 0, &result);

    // If primary module in slot 0 returned an error, try alternative slots configured in control data
    if(handle < 0 && i == 0){
      for(unsigned alt = 1; alt < TAC_GAME_MODULE_SLOTS; alt++){
        TacGameModule *alt_slot = &control->modules[alt];
        if(!__atomic_load_n(&alt_slot->enabled, __ATOMIC_ACQUIRE)) continue;
        if(alt_slot->path[0] != '/') continue;

        int alt_result = 0;
        int alt_handle = load_fn(alt_slot->path, 0, 0, 0, 0, &alt_result);
        if(alt_handle >= 0){
          handle = alt_handle;
          result = alt_result;
          unsigned p = 0;
          while(p < sizeof(slot->path) - 1 && alt_slot->path[p]){
            slot->path[p] = alt_slot->path[p];
            p++;
          }
          slot->path[p] = 0;
          break;
        }
      }
    }

    slot->handle=handle;
    slot->startResult=result;
    unsigned state=TAC_MODULE_FAILED;

    if(handle>=0 && result>=0){
      __atomic_store_n(&slot->phase,TAC_MODULE_PHASE_EXPORTS,__ATOMIC_RELEASE);
      void *load=0, *unload=0;
      TacGameDlsym dlsym=(TacGameDlsym)(uintptr_t)control->dlsym;
      slot->loadSymbolResult=dlsym(handle,control->sym_plugin_load,&load);
      slot->unloadSymbolResult=dlsym(handle,control->sym_plugin_unload,&unload);

      // GoldHEN-style paired activation contract
      if(!slot->loadSymbolResult&&load&&!slot->unloadSymbolResult&&unload){
        slot->activation=1;
        __atomic_store_n(&slot->phase,TAC_MODULE_PHASE_ACTIVATE,__ATOMIC_RELEASE);
        slot->activationResult=((TacGamePluginLoad)load)(0,0);
        if(!slot->activationResult) state=TAC_MODULE_STARTED;
      } else {
        state=TAC_MODULE_STARTED;
      }
    }

    __atomic_store_n(&slot->phase,TAC_MODULE_PHASE_COMPLETE,__ATOMIC_RELEASE);
    __atomic_store_n(&slot->state,state,__ATOMIC_RELEASE);
    break;
  }
  __atomic_store_n(&control->busy,0,__ATOMIC_RELEASE);
  return 0;
}
