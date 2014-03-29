#ifndef __GC_H__
#define __GC_H__

#include <stdlib.h>
#include <assert.h>

#include "threadctx.h"

namespace GC
{
    class MemoryAllocator;
    class Object;
    class Root;
    class Pin;

    /**
     * Memory allocation segment.
     * Segment is a unit of memory taken from OS by memory allocator.
     * Most of segments have fixed size (specified in MemoryAllocator constructor), but if user object
     * is larger than this size, then larger segment is created.
     * Unused segments are not deallocated, but linked in list to be reused in future.
     * But it is true only for segments of standard size: large segments are not reused.
     */
    struct MemorySegment
    {
        enum Bitmask 
        { 
            PINNED_SEGMENT = 1, // segment contains pinned object
            LARGE_SEGMENT  = 2, // segment of non-standard size
            MASK = 3
        };   
        MemorySegment*   next;      // L1-list of segment | Bitmask
        MemoryAllocator* owner;     // owner is needed to distinguish self objects from "foreign" objects.
    };

    /**
     * Object header used by allocator to mark objects and set reference to object copy.
     * Header is allocated by allocator BEFORE object.
     */
    union ObjectHeader 
    { 
        enum { GC_COPIED = 1 }; // mark set during GC
        size_t copy; // pointer to object copy | GC_COPIED
        MemorySegment* segment; // reference to segment is needed to distinguish self objects from "foreign" objects.
        double  align; // just for alignment of header
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
        static Object* allocate(size_t size);

        /**
         * Total size of allocated objects
         */
        static size_t totalAllocated();

        /**
         * Register root object. Registering root protects it and all referenced objects from GC.
         */
        static void registerRoot(Root* root);
            
        /**
         * Unregister root object. Make this object tree available for GC.
         */
        static void unregisterRoot(Root* root);

        /**
         * Pin object. Protect it from deallocation and copying by GC.
         */
        static void registerPin(Pin* pin);
            
        /**
         * Unpin object. Make this object available for GC.
         */
        static void unregisterPin(Pin* pin);

        /**
         * Deep copy
         * @param obj cloned object
         * @return cloned object 
         */
        static Object* copy(Object* obj);

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
         * @param segmentSize default size of memory allocation segment
         * @param gcStartThreshold total size of objects allocated since last GC after which allowGC() method initiates garbage collection
         * @param gcAutoStartThreshold  total size of objects allocated since last GC after which garbage collection is automatically started. 
         * Please notice that you should not have any unpinned direct (C++) pointers if you enable automatic start
         * of copying garbage collection, because object can be mmoved during any allocation request.
         */
        MemoryAllocator(size_t segmentSize = 1024*1024, size_t gcStartThreshold = 1024*1024, size_t gcAutoStartThreshold = (size_t)-1);

        /**
         * Deallocate all objects create by GC.
         */
        ~MemoryAllocator();
    
    
        /**
         * Deep copy
         * @param obj cloned object
         * @return cloned object 
         */
        Object* _copy(Object* obj);
        
        /**
         * Deep copy of object array elements
         * @param refs pointer to array of references
         * @param nRefs number of references
         */
        void _copy(Object** refs, size_t nRefs);


        // internal instance methods
        void  _registerRoot(Root* root);     
        void  _unregisterRoot(Root* root);        
        void  _registerPin(Pin* pin);     
        void  _unregisterPin(Pin* pin);        
        Object* _allocate(size_t size);
        void  _gc();
        void  _allowGC();
        size_t _totalAllocated() const { 
            return used;
        }


      private:
        size_t  defaultSegmentSize;
        size_t  used;               // Size used in the current segment
        MemorySegment* freeSegment; // L1 list of free segments
        MemorySegment* usedSegment; // L1 list of used segments
        size_t  allocated;          // Total allocated since last GC
        Root*   roots;              // Object roots
        Pin*    pinnedObjects;      // Pinned objects
        size_t  startThreshold;     // Total size of allocated objects since last GC after which allocGC() method start garbage collection
        size_t  autoStartThreshold; // Total size of allocated objects since last GC after GC is automatically started
        Object* clonedObject;       // Not null and points to original object when object is cloned during GC

        static ThreadContext<MemoryAllocator> ctx; // Context to locate current memory allocator
    };

    /**
     * Base class for all garbage collected classes. 
     */
    class Object 
    {
      public:
        /**
         * Redefined operator new for all derived classes
         * @param allocator allocator to be used
         */
        void* operator new(size_t size, MemoryAllocator* allocator) 
        { 
            return allocator->_allocate(size);
        }

        /**
         * Redefined operator new for all derived classes
         */
        void* operator new(size_t size) 
        { 
            return MemoryAllocator::allocate(size);
        }

        /**
         * Redefined operator new for all derived classes with varying size
         * @param allocator allocator to be used
         */
        void* operator new(size_t fixedSize, size_t varyingSize, MemoryAllocator* allocator)
        { 
            return allocator->_allocate(fixedSize + varyingSize);
        }

        /**
         * Redefined operator new for all derived classes with varying size
         */
        void* operator new(size_t fixedSize, size_t varyingSize)
        { 
            return MemoryAllocator::allocate(fixedSize + varyingSize);
        }

        /**
         * Objects should not be explicitly deleted.
         * Unreachable object is deleted by garbage collector.
         */
        void operator delete(void*) {}
        void operator delete(void*, size_t) {}

      protected:
        friend class MemoryAllocator;
        
        ObjectHeader* getHeader() const { 
            return (ObjectHeader*)this - 1;
        }

        /**
         * Recursively clone object
         * @param allocator used allocator
         */
        virtual Object* clone(MemoryAllocator* allocator) = 0;
    };

    /**
     * Smart pointer class.
     * This class is used to recursively copy referenced objects.
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
         * Copy constructor performs deep copy of referenced objects using current allocator (if any)
         */         
        Ref(Ref<T>& ref) 
        {
            obj = ref.obj = (T*)MemoryAllocator::copy(ref.obj); // we need to update original copy for pinned objects
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
         * Deep copy of root object
         * @param allocator memory allocator
         */
        virtual void copy(MemoryAllocator* allocator) = 0;

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
        
        virtual void copy(MemoryAllocator* allocator) { 
            obj = (T*)allocator->_copy(obj);
        }


        Var(T* ptr = NULL) : obj(ptr) {}

        Var(Var<T> const& var) : obj(var.obj) {}
    };
    
    /**
     * Class protecting object referenced by direct C++ pointer (like "this") from GC.
     * Pinned object can not be deallocated or copied.
     */
    class Pin 
    {
        friend class MemoryAllocator;

        Pin* next;
        Object* obj;
        
      public:
        Pin(Object* ptr) : obj(ptr) 
        {
            MemoryAllocator::registerPin(this);
        }
        ~Pin()
        {
            MemoryAllocator::unregisterPin(this);
        }
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

        virtual void copy(MemoryAllocator* allocator) { 
            allocator->_copy((Object**)array, size);
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

        virtual void copy(MemoryAllocator* allocator) { 
            allocator->_copy((Object**)array, used);
        }
    };
};    

#endif
        
