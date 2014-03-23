#include <stdio.h>
#include <time.h>
#include "gcclasses.h"

const size_t Mb = 1024*1024;

class Tree : public GC::Object
{
  public:
    GC::Ref<GC::String> label;
    GC::Ref<Tree> left;    
    GC::Ref<Tree> right;

    static Tree* build(size_t heigth) { 
        size_t nNodes = 0;
        return build(nNodes, 0, heigth);
    }
    static bool check(Tree* root, size_t height)
    {
        size_t nNodes = 0;
        return height == 0 ? root == NULL : root->check(nNodes, 0, height);
    }

  protected:
    virtual GC::Object* clone(GC::MemoryAllocator* allocator) { 
        return new (allocator) Tree(*this); 
    }

    bool check(size_t& nNodes, size_t level, size_t height) {
        char buf[16];
        sprintf(buf, "Node %d", (int)++nNodes);
        if (!label->equals(buf)) { 
            return false;
        }
        return ++level < height ? left->check(nNodes, level, height) && right->check(nNodes, level, height) : left == NULL && right == NULL;
    }
        
    static Tree* build(size_t& nNodes, size_t level, size_t height) { 
        if (level < height) { 
            GC::Var<Tree> root = new Tree();
            char buf[16];
            sprintf(buf, "Node %d", (int)++nNodes);
            root->label = GC::String::create(buf);
            root->left = build(nNodes, level+1, height);
            root->right = build(nNodes, level+1, height);
            return root;
        } else { 
            return NULL;
        }
    }
};

typedef GC::ObjectArray<Tree> Wood;
    
int main(int argc, char* argv[]) 
{ 
    int nTrees = argc > 1 ? atoi(argv[1]) : 100;
    int maxHeight = argc > 2 ? atoi(argv[2]) : 15;
    time_t start = time(NULL);
    { 
        GC::MemoryAllocator mem(1000*Mb, 1*Mb, -1, true);
        GC::Var<Wood> wood = Wood::create(nTrees);
        
        for (int height = 1; height < maxHeight; height++) {     
            for (int tree = 0; tree < nTrees; tree++) { 
                if (!Tree::check((*wood)[tree], height-1)) { 
                    fprintf(stderr, "Check failed for height=%d tree=%d\n", height, tree);
                    return EXIT_FAILURE;
                } 
                (*wood)[tree] = Tree::build(height);
            }
            mem.allowGC();
        }
    }
    printf("Elapsed time %d\n", (int)(time(NULL) - start));
    return EXIT_SUCCESS;
}
