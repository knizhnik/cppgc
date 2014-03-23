#include <new>
#include "gc.h"

namespace GC 
{ 
    const size_t BLACK_MARK = 1;
    
    ThreadContext<MemoryAllocator> MemoryAllocator::ctx;
    
    Object* MemoryAllocator::_allocate(size_t size) 
    {     
        if (allocated > autoStartThreshold) {
            gc();
        }
        size = (size + 7) & ~7; // align on 8
        if (used + size > segmentSize) { 
            if (throwException) { 
                throw std::bad_alloc();
            }
            return NULL;
        }
        Object* obj = (Object*)(segments[currSegment] + used);
        used += size;
        allocated += size;
        return obj;
    }

    void MemoryAllocator::_registerRoot(Root* root)
    {
        root->next = roots;
        roots = root;
    }
    
    void MemoryAllocator::_unregisterRoot(Root* root)
    {
        Root *rp, **rpp;
        for (rpp = &roots; (rp = *rpp) != root; rpp = &rp->next) { 
            assert(rp != NULL);
        }
        *rpp = rp->next;
    }

    Object* MemoryAllocator::_mark(Object* obj)
    {
        if (obj != NULL) { 
            if (size_t((char*)obj - (char*)segments[1-currSegment]) < segmentSize) { // self object
                ObjectHeader* hdr = (ObjectHeader*)obj;
                if (!hdr->isMarked()) { 
                    hdr->setCopy(obj->clone(this));
                }
                return hdr->getCopy();
            }
        }
        return obj;
    }

    void MemoryAllocator::_mark(Object** refs, size_t nRefs) 
    {  
        for (size_t i = 0; i < nRefs; i++) { 
            refs[i] = _mark(refs[i]);
        }
    }

    void MemoryAllocator::_allowGC()
    {
        if (allocated > startThreshold) {
            _gc();
        }
    }

    MemoryAllocator* MemoryAllocator::getCurrent() 
    { 
        MemoryAllocator* allocator = ctx.get();
        assert(allocator != NULL);
        return allocator;
    }

    size_t MemoryAllocator::totalAllocated() { 
        return getCurrent()->_totalAllocated();
    }

    Object* MemoryAllocator::allocate(size_t size) 
    {
        return getCurrent()->_allocate(size);
    }

    Object* MemoryAllocator::mark(Object* obj) 
    { 
        if (obj != NULL) { 
            MemoryAllocator* curr = ctx.get();
            if (curr != NULL) { 
                obj = curr->_mark(obj);
            }
        }
        return obj;
    } 

    void MemoryAllocator::mark(Object** refs, size_t nRefs) 
    {  
        MemoryAllocator* curr = ctx.get();
        if (curr != NULL) {                 
            curr->_mark(refs, nRefs);
        }
    }

    void MemoryAllocator::registerRoot(Root* root) 
    {
        getCurrent()->_registerRoot(root);
    }
            
    void MemoryAllocator::unregisterRoot(Root* root) 
    {
        getCurrent()->_unregisterRoot(root);
    }

    void MemoryAllocator::gc() 
    { 
        getCurrent()->_gc();
    }
    
    void MemoryAllocator::allowGC()
    { 
        getCurrent()->_allowGC();
    } 

    MemoryAllocator::MemoryAllocator(size_t memorySegmentSize, size_t gcStartThreshold, size_t gcAutoStartThreshold, bool throwExceptionOnNoMemory)
    {
        segments[0] = new char[memorySegmentSize*2];
        segments[1] = segments[0] + memorySegmentSize;
        segmentSize = memorySegmentSize;
        currSegment = 0;
        used = 0;
        allocated = 0;
        roots = NULL;
        objects = NULL;
        startThreshold = gcStartThreshold;
        autoStartThreshold = gcAutoStartThreshold;
        throwException = throwExceptionOnNoMemory;
        ctx.set(this);
    }

    MemoryAllocator::~MemoryAllocator()
    {
        delete[] segments[0];
    }

    void MemoryAllocator::_gc() 
    {
        size_t saveStartThreshold = autoStartThreshold;
        autoStartThreshold = (size_t)-1; // disable recusrive start of GC
        currSegment ^= 1;
        used = 0;
        for (Root* root = roots; root != NULL; root = root->next) { 
            root->mark(this); 
        }
        allocated = 0;
        autoStartThreshold = saveStartThreshold;
    }
}
