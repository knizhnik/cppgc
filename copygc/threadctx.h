#ifndef __THREADCTX_H__
#define __THREADCTX_H__


namespace GC
{
    class ThreadContextImpl 
    {
      public:
        void  set(void* val);

      protected:
        void* get();
        
        ThreadContextImpl();
        ~ThreadContextImpl();

        unsigned int key;
    };

    /**
     * Class implementing thread local (thread specific in terms of Posix) memory
     */
    template<class T>
    class ThreadContext : public ThreadContextImpl 
    {
      public:
        T* get() { 
            return (T*)ThreadContextImpl::get();
        }
    };
};

#endif


