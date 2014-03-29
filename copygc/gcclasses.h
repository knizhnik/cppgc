#ifndef __GCCLASSES_H__
#define  __GCCLASSES_H__

#include <string.h>

#include "gc.h"

namespace GC 
{
    /**
     * Fixed size array of scalars
     */
    template<class T>
    class ScalarArray : public Object
    {
      protected:
        size_t length;
        T body[1];

      public:
        static ScalarArray* create(size_t len) { 
            return new ((len-1)*sizeof(T)) ScalarArray(len);
        }
        
        T& operator[](size_t index) { 
            assert(index < length);
            return body[index];
        }

        T operator[](size_t index) const{ 
            assert(index < length);
            return body[index];
        }

        size_t size() const { 
            return length;
        }

        operator T*() { 
            return body;
        }

        operator T const*() const { 
            return body;
        }

      protected:  
        ScalarArray(size_t len) : length(len)
        {
            memset(body, 0, len*sizeof(T));
        }

        Object* clone(MemoryAllocator* allocator) 
        { 
            ScalarArray* copy = new ((length-1)*sizeof(T), allocator) ScalarArray(length);
            memcpy(copy->body, body, length*sizeof(T));
            return copy;
        }
    };

    /**
     * Fixed size array of garbage collected objects 
     */
    template<class T>
    class ObjectArray : public Object
    {
        size_t length;
        T* body[1];

      public:
        static ObjectArray* create(size_t len) { 
            return new ((len-1)*sizeof(T*)) ObjectArray(len);
        }
        
        T*& operator[](size_t index) { 
            assert(index < length);
            return body[index];
        }

        T* operator[](size_t index) const{ 
            assert(index < length);
            return body[index];
        }

        size_t size() const { 
            return length;
        }

        operator T**() { 
            return body;
        }

        operator T* const*() const { 
            return body;
        }

      protected:
        ObjectArray(size_t len) : length(len) 
        {
            memset(body, 0, len*sizeof(T*));
        }

        Object* clone(MemoryAllocator* allocator) 
        { 
            ObjectArray* copy = new ((length-1)*sizeof(T*), allocator) ObjectArray(*this);
            memcpy(copy->body, body, length*sizeof(T*));
            allocator->_copy((Object**)copy->body, length);
            return copy;
        }        
    };
        
        
    /**
     * Varying size vector of scalar
     */
    template<class T>
    class ScalarVector : public Object
    {
        GC::Ref< ScalarArray<T> > body;
        size_t length;
        
        Object* clone(MemoryAllocator* allocator) 
        { 
            return new (allocator) ScalarVector(*this);
        }

      public:
        ScalarVector(size_t reserve = 8) { 
            body = ScalarArray<T>::create(reserve);
            length = 0;
        }

        void resize(size_t newSize) { 
            size_t i;
            size_t allocated = body->size();
            if (newSize > allocated) { 
                allocated = allocated*2 > newSize ? allocated*2 : newSize;
                ScalarArray<T>* newBody = ScalarArray<T>::create(allocated);
                for (i = 0; i < length; i++) { 
                    newBody[i] = body[i];
                }
                body = newBody;
            }
            length = newSize;
        }

        void push(T val) { 
            resize(length+1);
            body[length-1] = val;
        }

        T pop() { 
            assert(length != 0);
            return body[--length];
        }

        T top() const { 
            assert(length != 0);
            return body[length-1];
        }
            
        T& operator[](size_t index) { 
            assert(index < length);
            return body[index];
        }

        T operator[](size_t index) const{ 
            assert(index < length);
            return body[index];
        }
        
        size_t size() const { 
            return length;
        }

        operator T*() { 
            return body;
        }

        operator T const*() const { 
            return body;
        }
    };

    /**
     * Varying size vector of garbage collected objects
     */
    template<class T>
    class ObjectVector : public Object
    {
        GC::Ref< ObjectArray<T> > body;
        size_t length;
        
        Object* clone(MemoryAllocator* allocator) 
        { 
            return new (allocator) ObjectVector(*this);
        }

      public:
        ObjectVector(size_t reserve = 8) { 
            body = ObjectArray<T>::create(reserve);
            length = 0;
        }

        void resize(size_t newSize) { 
            size_t i;
            size_t allocated = body->size();
            if (newSize > allocated) { 
                allocated = allocated*2 > newSize ? allocated*2 : newSize;
                ObjectArray<T>* newBody = ObjectArray<T>::create(allocated);
                for (i = 0; i < length; i++) { 
                    newBody[i] = body[i];
                }
                body = newBody;
            }
            length = newSize;
        }

        void push(T const* obj) { 
            resize(length+1);
            (*body)[length-1] = (T*)obj;
        }

        T* pop() { 
            assert(length != 0);
            return (*body)[--length];
        }

        T* top() const { 
            assert(length != 0);
            return (*body)[length-1];
        }
            
        T*& operator[](size_t index) { 
            assert(index < length);
            return (*body)[index];
        }

        T* operator[](size_t index) const{ 
            assert(index < length);
            return (*body)[index];
        }
        
        size_t size() const { 
            return length;
        }

        operator T**() { 
            return body;
        }

        operator T* const*() const { 
            return body;
        }
    };

        
    /**
     * Fixed size string class
     */
    class String : public Object
    {
        size_t length;
        char body[1]; 

      public:
        static String* create(size_t len) { 
            return new (len) String(len);
        }
        static String* create(char const* str) { 
            return create(str, strlen(str));
        }

        static String* create(char const* str, size_t len) {
            return new (len) String(str, len);
        }

        int compare(char const* other) { 
            return strcmp(body, other);
        }

        bool equals(char const* other) { 
            return compare(other) == 0;
        }

         int compare(GC::Ref<String> const& other) { 
            return compare(other->body);
        }

        bool equals(GC::Ref<String> const& other) { 
            return compare(other) == 0;
        }
        
        size_t size() const { 
            return length;
        }

        operator char*() { 
            return body;
        }

        operator char const*() const { 
            return body;
        }

      protected:
        Object* clone(MemoryAllocator* allocator) { 
            return new (length, allocator) String(body, length);
        }
        
        String(char const* str, size_t len) { 
            memcpy(body, str, len);
            length = len;
            body[len] = '\0';
        }
        String(size_t len) { 
            length = len;
            body[len] = '\0';
        }
     };

     /**
      * Garbage collectable wrapper class for T.
      */
    template<class T>
    class Wrapper : public Object, public T
    {
      protected:
        Object* clone(MemoryAllocator* allocator) { 
            return new (allocator) Wrapper(*this);
        }
    };

};


#endif
