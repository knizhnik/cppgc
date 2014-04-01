#include <new>
#include "gc.h"

namespace GC 
{ 
    ThreadContext<MemoryAllocator> MemoryAllocator::ctx;
    
    Object* MemoryAllocator::_allocate(size_t size) 
    {     
        if (allocated > autoStartThreshold) {
            gc();
        }
        size = (size + sizeof(ObjectHeader) + 7) & ~7; // align on 8
        if (used + size > defaultSegmentSize) { 
            MemorySegment* newSegment = freeSegment;
            if (newSegment == NULL || size > defaultSegmentSize) { 
                if (size > defaultSegmentSize) { 
                    newSegment = (MemorySegment*)malloc(sizeof(MemorySegment)  + size);
                    newSegment->next = (MemorySegment*)((size_t)usedSegment + MemorySegment::LARGE_SEGMENT);
                } else { 
                    newSegment = (MemorySegment*)malloc(sizeof(MemorySegment)  + defaultSegmentSize);
                    newSegment->next = usedSegment;
                }
                newSegment->owner = this;
            } else { 
                newSegment = freeSegment;
                freeSegment = newSegment->next;
                newSegment->next = usedSegment;
            }
            usedSegment = newSegment;
            used = 0;
        }
        ObjectHeader* hdr = (ObjectHeader*)((char*)(usedSegment + 1) + used);
        Object* obj = (Object*)(hdr + 1);
        used += size;
        allocated += size;
        hdr->segment = usedSegment;
        if (clonedObject != NULL) {             
            clonedObject->getHeader()->copy = (size_t)obj | ObjectHeader::GC_COPIED;
        }
        return obj;
    }

    void MemoryAllocator::_visit(AnyWeakRef* wref)
    {
        if (wref->obj != NULL) { 
            wref->next = weakReferences;
            weakReferences = wref;
        }
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

    void MemoryAllocator::_registerPin(Pin* pin)
    {
        pin->next = pinnedObjects;
        pinnedObjects = pin;
    }
    
    void MemoryAllocator::_unregisterPin(Pin* pin)
    {
        Pin *p, **pp;
        for (pp = &pinnedObjects; (p = *pp) != pin; pp = &p->next) { 
            assert(p != NULL);
        }
        *pp = p->next;
    }

    Object* MemoryAllocator::_copy(Object* obj)
    {
        if (obj != NULL) { 
            ObjectHeader* hdr = obj->getHeader();
            if (hdr->copy & ObjectHeader::GC_COPIED) { 
                return (Object*)(hdr->copy - ObjectHeader::GC_COPIED);
            } else if ((Object*)hdr->copy == obj) { // pinned object
                hdr->copy = (size_t)obj + ObjectHeader::GC_COPIED;
                (void)obj->clone(this);
            } else if (hdr->segment->owner == this) { 
                clonedObject = obj;
                obj = obj->clone(this);
                clonedObject = NULL;
            }
        }
        return obj;
    }

    void MemoryAllocator::_copy(Object** refs, size_t nRefs) 
    {  
        for (size_t i = 0; i < nRefs; i++) { 
            refs[i] = _copy(refs[i]);
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

    void MemoryAllocator::visit(AnyWeakRef* wref) 
    {
        MemoryAllocator* curr = ctx.get();
        if (curr != NULL) {                 
            curr->_visit(wref);
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

    Object* MemoryAllocator::copy(Object* obj)
    {
        if (obj != NULL) {
            MemoryAllocator* allocator = getCurrent();
            if (allocator != NULL) { 
                obj = allocator->_copy(obj);
            }
        }
        return obj;
    }

    void MemoryAllocator::registerPin(Pin* pin) 
    {
        getCurrent()->_registerPin(pin);
    }
            
    void MemoryAllocator::unregisterPin(Pin* pin) 
    {
        getCurrent()->_unregisterPin(pin);
    }

    void MemoryAllocator::gc() 
    { 
        getCurrent()->_gc();
    }
    
    void MemoryAllocator::allowGC()
    { 
        getCurrent()->_allowGC();
    } 

    MemoryAllocator::MemoryAllocator(size_t segmentSize, size_t gcStartThreshold, size_t gcAutoStartThreshold)
    {
        usedSegment = NULL;
        freeSegment = NULL;
        used = defaultSegmentSize = segmentSize;
        allocated = 0;
        roots = NULL;
        clonedObject = NULL;
        pinnedObjects = NULL;
        startThreshold = gcStartThreshold;
        autoStartThreshold = gcAutoStartThreshold;
        ctx.set(this);
    }

    MemoryAllocator::~MemoryAllocator()
    {
        MemorySegment *curr, *next;
        for (curr = freeSegment; curr != NULL; curr = next) { 
            next = curr->next;
            delete curr;
        }
        for (curr = usedSegment; curr != NULL; curr = next) { 
            next = (MemorySegment*)((size_t)curr->next & ~MemorySegment::MASK);
            delete curr;
        }
    }

    void MemoryAllocator::_gc() 
    {
        size_t saveStartThreshold = autoStartThreshold;
        MemorySegment* old = usedSegment;

        // Garbage collector will copy accessible objects in new segments
        usedSegment = NULL;
        autoStartThreshold = (size_t)-1; // disable recusrive start of GC
        used = defaultSegmentSize;
        weakReferences = NULL;
        
        // First of all pin objects
        for (Pin* pin = pinnedObjects; pin != NULL; pin = pin->next) { 
            ObjectHeader* hdr = pin->obj->getHeader();
            hdr->segment->next = (MemorySegment*)((size_t)hdr->segment->next | MemorySegment::PINNED_SEGMENT);
            hdr->copy = (size_t)pin->obj;            
        }
        // Now clone objects referenced from pinned objects
        for (Pin* pin = pinnedObjects; pin != NULL; pin = pin->next) { 
            (void)_copy(pin->obj);
        }
        // And finally copy and adjust all roots
        for (Root* root = roots; root != NULL; root = root->next) { 
            root->copy(this); 
        }
        // Reset all weak references to dead objects
        for (AnyWeakRef* wref = weakReferences; wref != NULL; wref = wref->next) { 
            ObjectHeader* hdr = wref->obj->getHeader();
            if (hdr->copy & ObjectHeader::GC_COPIED) { 
                wref->obj = (Object*)(hdr->copy - ObjectHeader::GC_COPIED);
            } else if (hdr->segment->owner == this) {
                wref->obj = NULL;
            }
        }
        // Copy phase is done
        

        // Now traverse list of old segments
        while (old != NULL) {
            size_t next = (size_t)old->next;
            if (next & MemorySegment::PINNED_SEGMENT) { // segment contains pinned objects, reclaim it
                old->next = (MemorySegment*)((size_t)usedSegment | (next & MemorySegment::LARGE_SEGMENT));
                usedSegment = old;
            } else { 
                if (next & MemorySegment::LARGE_SEGMENT) { 
                    delete old;
                } else { 
                    old->next = freeSegment;
                    freeSegment = old;
                }
            }
            old = (MemorySegment*)(next & ~MemorySegment::MASK);
        }                
        allocated = 0;
        autoStartThreshold = saveStartThreshold;
    }
}
