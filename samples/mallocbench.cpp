#include <stdio.h>
#include <string.h>
#include <time.h>
#include <boost/shared_ptr.hpp>
#include <memory>
#include "gc.h"

const size_t Mb = 1024*1024;

template<class T>
class FixedAllocator
{
    struct FreeItem { 
        FreeItem* next;
    };
    FreeItem* freeChain;
  public:
    FixedAllocator() { 
        freeChain = NULL;
    }
    ~FixedAllocator() { 
        FreeItem *elem, *next;
        for (elem = freeChain; elem != NULL; elem = next) { 
            next = elem->next;
            delete elem;
        }
    }
    
    T* allocate() { 
        FreeItem* obj = freeChain;
        if (obj != NULL) { 
            freeChain = obj->next;
            return new ((T*)obj) T(); // reinitialize the object
        }
        return new T();
    }
    
    void free(T* obj) {
        if (obj != NULL) { 
            obj->~T(); // destroy object
            FreeItem* elem = (FreeItem*)obj;
            elem->next = freeChain;
            freeChain = elem;
        }
    }
};

class StackAllocator
{
    struct Chunk { 
        Chunk* next;
        char   body[1];

        void* operator new(size_t headerSize, size_t bodySize) { 
            return new char[bodySize + sizeof(Chunk*)];
        }
        void operator delete(void* ptr) { 
            delete[] (char*)ptr;
        }            
    };

    Chunk* usedChunks;
    Chunk** usedChunksTail;
    Chunk* freeChunks;
    const size_t chunkSize;
    size_t used;

  public:
    StackAllocator(size_t size) : chunkSize(size) 
    { 
        usedChunks = NULL;
        freeChunks = NULL;
        usedChunksTail = &usedChunks;
        used = chunkSize;
    }

    ~StackAllocator() { 
        Chunk *chunk, *next;
        reset();
        for (chunk = freeChunks; chunk != NULL; chunk = next) { 
            next = chunk->next;
            delete chunk;
        }
    }
         
    template<class T>
    T* allocate() { 
        T* obj;
        Chunk* chunk = usedChunks;
        if (used + sizeof(T) > chunkSize) { 
            if (freeChunks != NULL) { 
                chunk = freeChunks;
                freeChunks = chunk->next;
            } else { 
                chunk = new (chunkSize) Chunk();
            }
            *usedChunksTail = chunk;
            usedChunksTail = &chunk->next;
            used = 0;
        }
        obj = new ((T*)&chunk->body[used]) T();
        used += (sizeof(T) + sizeof(void*)-1) & ~(sizeof(void*)-1);
        return obj;
    }
    
    template<class T>
    void free(T* obj) { 
        if (obj != NULL) { 
            obj->~T();
        }
    }


    void reset() { 
        *usedChunksTail = freeChunks;
        freeChunks = usedChunks;
        usedChunks = NULL;
        usedChunksTail = &usedChunks;
        used = chunkSize;
    }
};

const size_t totalObjects = 1000000000L;
const size_t liveObjects = 16*1024;
const size_t objectSize = 10*8;

struct Object { 
    char body[objectSize];
};

struct GCObject : public GC::Object
{ 
    char body[objectSize];
};

int main() { 
    Object* objects[liveObjects];
    time_t start;

    memset(objects, 0, sizeof(objects));
    start = time(NULL);
    for (size_t i = 0; i < totalObjects; i++) { 
        delete objects[i % liveObjects];
        objects[i % liveObjects] = new Object();
    }
    for (size_t i = 0; i < liveObjects; i++) { 
        delete objects[i];
    }
    printf("Elapsed time for standard new/delete: %ld\n", time(NULL) - start);

    {
        FixedAllocator<Object> fixedAlloc;
        memset(objects, 0, sizeof(objects));
        start = time(NULL);
        
        for (size_t i = 0; i < totalObjects; i++) { 
            fixedAlloc.free(objects[i % liveObjects]);
            objects[i % liveObjects] = fixedAlloc.allocate();
        }
    }
    printf("Elapsed time for fixed size allocator: %ld\n", time(NULL) - start);
    {
        StackAllocator stackAlloc(64*1024);
        GC::ThreadContext<StackAllocator> ctx;
        ctx.set(&stackAlloc);
        memset(objects, 0, sizeof(objects));
        start = time(NULL);
        for (size_t i = 0; i < totalObjects; i++) { 
            if (i % liveObjects == 0) {
                ctx.get()->reset();
            }
            objects[i % liveObjects] = ctx.get()->allocate<Object>();
        }
    }
    printf("Elapsed time for stack allocator: %ld\n", time(NULL) - start);
   
    {
        boost::shared_ptr<Object> objectRefs[liveObjects];
        start = time(NULL);
        for (size_t i = 0; i < totalObjects; i++) { 
            objectRefs[i % liveObjects] = boost::shared_ptr<Object>(new Object());
        }
    }
    printf("Elapsed time boost::shared_ptr: %ld\n", time(NULL) - start);

    {
        std::shared_ptr<Object> objectRefs[liveObjects];
        start = time(NULL);
        for (size_t i = 0; i < totalObjects; i++) { 
            objectRefs[i % liveObjects] = std::shared_ptr<Object>(new Object());
        }
    }
    printf("Elapsed time std::shared_ptr: %ld\n", time(NULL) - start);

    {
        GC::MemoryAllocator mem(1*Mb, 1*Mb);
        GC::ArrayVar<GCObject,liveObjects> objectRefs;
        start = time(NULL);
        for (size_t i = 0; i < totalObjects; i++) { 
            objectRefs[i % liveObjects] = new GCObject();
        }
    }
    printf("Elapsed time CppGC: %ld\n", time(NULL) - start);
    return 0;
}
