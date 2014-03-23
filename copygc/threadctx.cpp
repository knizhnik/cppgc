#include "threadctx.h"

#ifdef _WIN32    

#include <windows.h>

namespace GC
{

    ThreadContextImpl::ThreadContextImpl() 
    { 
        key = TlsAlloc();
    }

    ThreadContextImpl::~ThreadContextImpl() 
    { 
         TlsFree(key);
    }
    
    void* ThreadContextImpl::get() 
    {
        return TlsGetValue(key);
    }
    
    void ThreadContextImpl::set(void* value) 
    {
        TlsSetValue(key, value);
    }
};

#else

#include <pthread.h>

namespace GC
{

    ThreadContextImpl::ThreadContextImpl() 
    { 
        pthread_key_create(&key, NULL);
    }

    ThreadContextImpl::~ThreadContextImpl() 
    { 
        pthread_key_delete(key);
    }

    void* ThreadContextImpl::get() 
    {
        return pthread_getspecific(key);
    }
    
    void ThreadContextImpl::set(void* value) 
    {
         pthread_setspecific(key, value);
    }
};

#endif

