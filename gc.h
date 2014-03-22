#ifndef __GC_H__
#define __GC_H__

#include <stdlib.h>
#include <assert.h>

#include "threadctx.h"

namespace GC
{
    class Object;
    class Root;

    /**
     * Invoke copy constructors of Ref<T> class to mark referenced objects
     */
    #define GC_MARK(Class) Class(*this)

    /**
     * Object header used by allocator to link all allocated objects.
     * It is assumed that virtual table pointer is first field of the class.
     */
    struct ObjectHeader 
    { 
        ObjectHeader* next;

        Object* getObject() const { 
            return (Object*)(this + 1);
        }
    };

    /**
     * Memory allocator class with implicit memory deallocation (garbage collector). 
     * Each thread should have its own allocator. So each thread is allocating and deallocating only its own objects.
     * But it is possible to refer object created by some global allocator, taken in account that this objects will not be protected from GC.
     */
    class MemoryAllocator
    {
        friend class Object;
      public:
        /**
         * Get allocator for the current thread. Each thread should have its own allocator.
         */
        static MemoryAllocator* getCurrent();

        /**
         * Allocate object 
         * @param object size
         */
        static void* allocate(size_t size);

        /**
         * Mark object as reachable and recursively mark all references objects
         * @param obj marked objects (may be NULL)
         */
        static void mark(Object* obj);

        /**
         * Mark array of objects.
         * @param refs pointer to array of references
         * @param nRefs number of references
         */
        static void mark(Object** refs, size_t nRefs);

        /**
         * Register root object. Registering root protects it and all referenced objects from GC.
         */
        static void registerRoot(Root* root);
            
        /**
         * Unregister root object. Make this object tree available for GC.
         */
        static void unregisterRoot(Root* root);

        /**
         * Explicitly starts garbage collection.
         */
        static void gc();

        /**
         * Start garbage collection if number of allocated objects since last GC exceeds StartThreshold 
         */
        static void allowGC();
        
        /**
         * Create instance of memory allocator 
         * @param gcStartThreshold total size of objects allocated since last GC after which allowGC() method initiates garbage collection
         * @param gcAutoStartThreshold  total size of objects allocated since last GC after which garbage collection is automatically started. 
         * Please notice that all used objects should be protected from GC in this case by registering their roots.
         */
        MemoryAllocator(size_t gcStartThreshold = 1024*1024, size_t gcAutoStartThreshold = (size_t)-1);

        /**
         * Deallocate all objects create by GC.
         */
        ~MemoryAllocator();
    
        // internal instance methods
        void  _registerRoot(Root* root);     
        void  _unregisterRoot(Root* root);        
        void  _mark(Object* obj);
        void  _mark(Object** refs, size_t nRefs);
        void* _allocate(size_t size);
        void  _gc();
        void  _allowGC();

      private:
        void markPhase();
        void sweepPhase();

      private:
        size_t  allocated;
        Root*   roots;
        ObjectHeader* objects;
        size_t  startThreshold;
        size_t  autoStartThreshold;

        static ThreadContext<MemoryAllocator> ctx;
    };

    /**
     * Base class for all garbage collected classes. 
     */
    class Object 
    {
      public:
        /**
         * Redefined operator new for all derived classes
         */
        void* operator new(size_t size) 
        { 
            return MemoryAllocator::allocate(size);
        }

        /**
         * Redefined operator new for all derived classes with varying  size
         */
        void* operator new(size_t fixedSize, size_t varyingSize)
        { 
            return MemoryAllocator::allocate(fixedSize + varyingSize);
        }

        /**
         * Objects should not be explicitly deleted.
         * Unreachable object is deleted by garbage collector.
         */
        void operator delete(void* obj) {
            free((ObjectHeader*)obj - 1); 
        } 
        void operator delete(void* obj, size_t) { 
            free((ObjectHeader*)obj - 1);             
        } 

      protected:
        friend class MemoryAllocator;
        
        /**
         * Mark referenced objects
         */
        virtual void mark(MemoryAllocator* allocator) {}

        /**
         * Virtual destructor used by memory allocator to finilize object
         */
        virtual~Object() {}

        ObjectHeader* getHeader() { 
            return (ObjectHeader*)this - 1;
        }
    };

    /**
     * Smart pointer class.
     * This class is used to implicitly mark accessible objects.
     * You should use this class instead of normal C++ pointers and references in all garbage collected classes 
     */
    template<class T>
    class Ref 
    {
        T* obj;

      public:
        T& operator*() { 
            return *obj;
        }
        T const& operator*() const { 
            return *obj;
        }
        T* operator->() { 
            return obj; 
        }
        T const* operator->() const { 
            return obj; 
        }
        operator T*() { 
            return obj;
        }
        operator T const*() const { 
            return obj;
        }
        T* operator = (T const* val) {
            return obj = (T*)val;            
        }
        bool operator == (T const* other) { 
            return obj == other;
        }
        bool operator == (Ref<T> const& other) { 
            return obj == other.obj;
        }
        bool operator != (T const* other) { 
            return obj != other;
        }
        bool operator != (Ref<T> const& other) { 
            return obj != other.obj;
        }
        
        Ref(T const* ptr = NULL) : obj((T*)ptr) {}

        /**
         * Copy constructor marks referenced objects using current allocator (if any)
         */         
        Ref(Ref<T> const& ref) : obj(ref.obj) {
            MemoryAllocator::mark(obj);
        }
    };

    /**
     * Base class for variables referencing objects and protecting them from GC.
     */
    class Root
    {
        friend class MemoryAllocator;

      protected:
        Root* next; // roots are linked in L1 list
        
        /**
         * Mark root object
         * @param allocator memory allocator
         */
        virtual void mark(MemoryAllocator* allocator) = 0;

        /**
         * Register objects tree root in the current memory allocator
         */
        Root() 
        { 
            MemoryAllocator::registerRoot(this);
        }
        
        /**
         * Unregister objects tree root in the current memory allocator
         */
        ~Root() 
        { 
            MemoryAllocator::unregisterRoot(this);
        }        
    };

    /**
     * Class for variable, protecting object tree from GC. 
     * It should be used instead of normal C++ pointers.
     * If GC is invoked explicitly by gc() or allowGC() methods, then it is necessary to protect accessed objects only 
     * before invocation of these methods. If GC is started automatically, then application should be ready that GC can be started at any moment of time
     * and so should always protect all live object from GC by referencing them from Var<T> variables.
     */
    template<class T>
    class Var : Root
    {
        T* obj;

      public:
        T& operator*() { 
            return *obj;
        }
        T const& operator*() const { 
            return *obj;
        }
        T* operator->() { 
            return obj; 
        }
        T const* operator->() const { 
            return obj; 
        }
        operator T*() { 
            return obj;
        }
        operator T const*() const { 
            return obj;
        }
        T* operator = (T const* val) {
            obj = (T*)val;
            return val;
        }
        bool operator == (T const* other) { 
            return obj == other;
        }
        bool operator == (Var<T> const& other) { 
            return obj == other.obj;
        }
        bool operator != (T const* other) { 
            return obj != other;
        }
        bool operator != (Var<T> const& other) { 
            return obj != other.obj;
        }
        
        virtual void mark(MemoryAllocator* allocator) { 
            allocator->_mark(obj);
        }

        Var(T* ptr = NULL) : obj(ptr) {}

        Var(Var<T> const& var) : obj(var.obj) {}
    };
    
    /**
     * Fixed size array of references variable protected from GC
     */
    template<class T, size_t size>
    class ArrayVar : Root
    {
        T* array[size];

      public:
        T*& operator[](size_t index) 
        { 
            assert(index < size);
            return array[index];
        }
        
        T* operator[](size_t index) const
        { 
            assert(index < size);
            return array[index];
        }        

        ArrayVar() {
            for (size_t i = 0; i < size; i++) { 
                array[i] = NULL;
            }
        }

        virtual void mark(MemoryAllocator* allocator) { 
            allocator->_mark((Object**)array, size);
        }
    };

    /**
     * Dynamic vector of references variable protected from GC
     */
    template<class T>
    class VectorVar : Root
    {
        T** array;
        size_t used;
        size_t allocated;

      public:
        T*& operator[](size_t index) 
        { 
            assert(index < used);
            return array[index];
        }
        
        T* operator[](size_t index) const
        { 
            assert(index < used);
            return array[index];
        }        

        size_t size() const
        {
            return used;
        }

        void resize(size_t newSize) 
        { 
            size_t i;
            if (newSize > allocated) { 
                allocated = allocated*2 > newSize ? allocated*2 : newSize;
                T** newArray = new T*[allocated];
                for (i = 0; i < used; i++) { 
                    newArray[i] = array[i];
                }
                delete[] array;
                array = newArray;
            }
            for (i = used; i < newSize; i++) { 
                array[i] = NULL;
            }
            used = newSize;
        }

        void push(T const* obj) { 
            resize(used+1);
            array[used-1] = (T*)obj;
        }

        T* pop() { 
            assert(used != 0);
            return array[--used];
        }

        T* top() const { 
            assert(used != 0);
            return array[used-1];
        }

        VectorVar(size_t reserve = 8) {
            used = 0;
            allocated = reserve;
            array = new T*[reserve];
        }

        ~VectorVar() {
            delete[] array;
        }

        virtual void mark(MemoryAllocator* allocator) { 
            allocator->_mark((Object**)array, used);
        }
    };
};    

#endif
        
