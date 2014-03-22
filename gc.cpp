#include "gc.h"

namespace GC 
{ 
    const size_t BLACK_MARK = 1;
    
    ThreadContext<MemoryAllocator> MemoryAllocator::ctx;

    void* MemoryAllocator::_allocate(size_t size) 
    {
        
        ObjectHeader* hdr = (ObjectHeader*)malloc(sizeof(ObjectHeader) + size);        
        if (hdr != NULL) { 
            if (allocated > autoStartThreshold) {
                gc();
            }
            hdr->next = objects;
            objects = hdr;
            allocated += sizeof(ObjectHeader) + size;
            return hdr->getObject();
        }
        return NULL;
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

    void MemoryAllocator::_mark(Object* obj)
    {
        if (obj != NULL) { 
            ObjectHeader* hdr = obj->getHeader();
            size_t next = (size_t)hdr->next;
            if ((next & BLACK_MARK) == 0) { 
                hdr->next = (ObjectHeader*)(next + BLACK_MARK);
                obj->mark(this); // mark referenced objects
            }
        }
    }

    void MemoryAllocator::_mark(Object** refs, size_t nRefs) 
    {  
        for (size_t i = 0; i < nRefs; i++) { 
            _mark(refs[i]);
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

    void* MemoryAllocator::allocate(size_t size) 
    {
        return getCurrent()->_allocate(size);
    }

    void MemoryAllocator::mark(Object* obj) 
    { 
        if (obj != NULL) { 
            MemoryAllocator* curr = ctx.get();
            if (curr != NULL) { 
                curr->_mark(obj);
            }
        }
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

    MemoryAllocator::MemoryAllocator(size_t gcStartThreshold, size_t gcAutoStartThreshold)
    {
        allocated = 0;
        roots = NULL;
        objects = NULL;
        startThreshold = gcStartThreshold;
        autoStartThreshold = gcAutoStartThreshold;
        ctx.set(this);
    }

    MemoryAllocator::~MemoryAllocator()
    {
        ObjectHeader *hdr, *next;
        for (hdr = objects; hdr != NULL; hdr = next) { 
            next = (ObjectHeader*)((size_t)hdr->next & ~BLACK_MARK);
            free(hdr);
        }
    }

    void MemoryAllocator::_gc() 
    {
        markPhase();
        sweepPhase();
    }
    
    void MemoryAllocator::markPhase() 
    {
        for (Root* root = roots; root != NULL; root = root->next) { 
            root->mark(this); 
        }
    }
    
    void MemoryAllocator::sweepPhase() 
    {
        ObjectHeader *op, **opp = &objects; 
        while ((op = *opp) != NULL) { 
            size_t next = (size_t)op->next;
            if (next & BLACK_MARK) { 
                op->next = (ObjectHeader*)(next - BLACK_MARK);
                opp = &op->next;
            } else { 
                *opp = (ObjectHeader*)next;
                delete op->getObject();
            }
        }
        allocated = 0;
    }
}
